// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include "core/profile_catalog.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <optional>
#include <utility>

namespace {

std::optional<QString> cleanAbsolutePath(const QString &path)
{
    if (path.isEmpty() || !QDir::isAbsolutePath(path)) {
        return std::nullopt;
    }
    const QString cleaned = QDir::cleanPath(path);
    if (cleaned.isEmpty() || !QDir::isAbsolutePath(cleaned)) {
        return std::nullopt;
    }
    return cleaned;
}

std::optional<QStringList> watchPaths(const LocatedProfileDirectory &directory,
                                      const DirectoryFingerprint &directoryFingerprint,
                                      const QList<FileFingerprint> &fingerprints)
{
    const std::optional<QString> cleanDirectory = cleanAbsolutePath(directory.hostPath);
    if (!cleanDirectory.has_value() || !cleanAbsolutePath(directory.launchPath).has_value()
        || !cleanAbsolutePath(directoryFingerprint.canonicalPath).has_value()
        || directoryFingerprint.device == 0 || directoryFingerprint.inode == 0) {
        return std::nullopt;
    }

    QSet<QString> seenPaths{*cleanDirectory};
    QStringList parents;
    parents.reserve(directoryFingerprint.symlinkParentPaths.size());
    for (const QString &parentPath : directoryFingerprint.symlinkParentPaths) {
        const std::optional<QString> cleanPath = cleanAbsolutePath(parentPath);
        if (!cleanPath.has_value()) {
            return std::nullopt;
        }
        if (!seenPaths.contains(*cleanPath)) {
            seenPaths.insert(*cleanPath);
            parents.append(*cleanPath);
        }
    }
    std::sort(parents.begin(), parents.end(), [](const QString &left, const QString &right) {
        return QString::compare(left, right, Qt::CaseSensitive) < 0;
    });

    QStringList files;
    files.reserve(fingerprints.size());
    for (const FileFingerprint &fingerprint : fingerprints) {
        const std::optional<QString> cleanPath = cleanAbsolutePath(fingerprint.path);
        if (!cleanPath.has_value()) {
            return std::nullopt;
        }
        if (!seenPaths.contains(*cleanPath)) {
            seenPaths.insert(*cleanPath);
            files.append(*cleanPath);
        }
    }
    std::sort(files.begin(), files.end(), [](const QString &left, const QString &right) {
        return QString::compare(left, right, Qt::CaseSensitive) < 0;
    });

    QStringList paths{*cleanDirectory};
    paths.append(parents);
    paths.append(files);
    return paths;
}

bool sameFingerprint(const FileFingerprint &left, const FileFingerprint &right)
{
    return left.path == right.path && left.size == right.size
        && left.modifiedMilliseconds == right.modifiedMilliseconds;
}

bool sameDirectoryFingerprint(const DirectoryFingerprint &left,
                              const DirectoryFingerprint &right)
{
    return left.canonicalPath == right.canonicalPath && left.device == right.device
        && left.inode == right.inode
        && left.symlinkParentPaths == right.symlinkParentPaths;
}

bool equivalentSnapshot(const ProfileSnapshot &left, const ProfileSnapshot &right)
{
    if (left.directory.hostPath != right.directory.hostPath
        || left.directory.launchPath != right.directory.launchPath
        || !sameDirectoryFingerprint(left.directoryFingerprint,
                                     right.directoryFingerprint)
        || left.fingerprint.size() != right.fingerprint.size()) {
        return false;
    }
    for (qsizetype index = 0; index < left.fingerprint.size(); ++index) {
        if (!sameFingerprint(left.fingerprint.at(index), right.fingerprint.at(index))) {
            return false;
        }
    }
    return true;
}

void sanitizeRecords(ProfileSnapshot &snapshot)
{
    QHash<QString, qsizetype> counts;
    for (const ProfileRecord &record : snapshot.profiles) {
        if (!record.opaqueId.isEmpty()) {
            ++counts[record.opaqueId];
        }
    }
    QList<ProfileRecord> sanitized;
    sanitized.reserve(snapshot.profiles.size());
    for (ProfileRecord &record : snapshot.profiles) {
        if (!record.opaqueId.isEmpty() && counts.value(record.opaqueId) == 1) {
            sanitized.append(std::move(record));
        }
    }
    snapshot.profiles = std::move(sanitized);
}

ProfileCatalogError mapRepositoryError(ProfileRepositoryError error)
{
    return error == ProfileRepositoryError::UnreadableDirectory
        ? ProfileCatalogError::UnreadableDirectory
        : ProfileCatalogError::NoProfileDirectory;
}

} // namespace

struct ProfileCatalog::CallbackLifetime {
    ProfileCatalog *catalog = nullptr;
    bool active = false;
};

ProfileCatalog::ProfileCatalog(ProfileRepositorySource &repository, ProfileWatcher &watcher)
    : repository_(repository)
    , watcher_(watcher)
{
}

ProfileCatalog::~ProfileCatalog()
{
    deactivateCallback();
    watcher_.clear();
}

CatalogResult ProfileCatalog::records(const RemminaInstance &instance)
{
    if (!hasSelectedInstance_) {
        selectedInstanceId_ = instance.id;
        hasSelectedInstance_ = true;
    } else if (selectedInstanceId_ != instance.id) {
        clearSnapshotAndWatches();
        selectedInstanceId_ = instance.id;
        hasSelectedInstance_ = true;
        sessionActive_ = false;
        dirty_ = false;
    }

    if (sessionActive_ && !dirty_) {
        return records_;
    }

    sessionActive_ = true;
    RepositoryLoadResult initialResult = repository_.load(instance);
    if (const auto *error = std::get_if<ProfileRepositoryError>(&initialResult)) {
        return repositoryError(*error, instance);
    }

    ProfileSnapshot candidate = std::get<ProfileSnapshot>(std::move(initialResult));
    sanitizeRecords(candidate);
    constexpr int maximumInstallVerifyRounds = 2;
    for (int round = 0; round < maximumInstallVerifyRounds; ++round) {
        records_ = candidate.profiles;
        hasCleanSnapshot_ = false;
        if (!installWatches(candidate)) {
            return records_;
        }

        RepositoryLoadResult verificationResult = repository_.load(instance);
        if (const auto *error =
                std::get_if<ProfileRepositoryError>(&verificationResult)) {
            return repositoryError(*error, instance);
        }
        ProfileSnapshot verified =
            std::get<ProfileSnapshot>(std::move(verificationResult));
        sanitizeRecords(verified);
        records_ = verified.profiles;
        const bool unchanged = equivalentSnapshot(candidate, verified);
        if (dirty_) {
            break;
        }
        if (unchanged) {
            installCleanSnapshot(verified);
            return records_;
        }
        candidate = std::move(verified);
    }

    deactivateCallback();
    watcher_.clear();
    hasCleanSnapshot_ = false;
    dirty_ = true;
    return records_;
}

const ProfileRecord *ProfileCatalog::resolve(QStringView opaqueId) const
{
    if (dirty_ || !hasCleanSnapshot_
        || !profile_repository_detail::directoryMatches(
            cleanDirectory_.hostPath, cleanDirectoryFingerprint_)) {
        return nullptr;
    }
    for (const ProfileRecord &record : records_) {
        if (record.opaqueId == opaqueId) {
            const QString canonicalIdentity = QFileInfo(record.sourcePath).canonicalFilePath();
            if (canonicalIdentity.isEmpty()) {
                return nullptr;
            }
            const QString currentId = profile_repository_detail::opaqueProfileId(
                QStringView(selectedInstanceId_), QStringView(canonicalIdentity));
            return currentId == record.opaqueId ? &record : nullptr;
        }
    }
    return nullptr;
}

void ProfileCatalog::markDirty() noexcept
{
    dirty_ = true;
}

void ProfileCatalog::endSession()
{
    sessionActive_ = false;
}

void ProfileCatalog::reset()
{
    clearSnapshotAndWatches();
    selectedInstanceId_.clear();
    hasSelectedInstance_ = false;
    sessionActive_ = false;
    dirty_ = false;
}

CatalogResult ProfileCatalog::repositoryError(ProfileRepositoryError error,
                                              const RemminaInstance &instance)
{
    clearSnapshotAndWatches();
    selectedInstanceId_ = instance.id;
    hasSelectedInstance_ = true;
    sessionActive_ = true;
    dirty_ = true;
    return mapRepositoryError(error);
}

bool ProfileCatalog::installWatches(const ProfileSnapshot &snapshot)
{
    const std::optional<QStringList> paths = watchPaths(
        snapshot.directory, snapshot.directoryFingerprint, snapshot.fingerprint);
    if (!paths.has_value()) {
        deactivateCallback();
        watcher_.clear();
        dirty_ = true;
        return false;
    }

    deactivateCallback();
    callbackLifetime_ = std::make_shared<CallbackLifetime>();
    callbackLifetime_->catalog = this;
    callbackLifetime_->active = true;
    const std::weak_ptr<CallbackLifetime> weakLifetime = callbackLifetime_;
    dirty_ = false;
    const bool installed = watcher_.replacePaths(*paths, [weakLifetime] {
        const std::shared_ptr<CallbackLifetime> lifetime = weakLifetime.lock();
        if (lifetime && lifetime->active && lifetime->catalog != nullptr) {
            lifetime->catalog->markDirty();
        }
    });
    if (!installed) {
        deactivateCallback();
        dirty_ = true;
        return false;
    }
    return true;
}

void ProfileCatalog::installCleanSnapshot(const ProfileSnapshot &snapshot)
{
    cleanDirectory_ = snapshot.directory;
    cleanDirectoryFingerprint_ = snapshot.directoryFingerprint;
    hasCleanSnapshot_ = true;
    dirty_ = false;
}

void ProfileCatalog::deactivateCallback() noexcept
{
    if (callbackLifetime_) {
        callbackLifetime_->active = false;
        callbackLifetime_->catalog = nullptr;
        callbackLifetime_.reset();
    }
}

void ProfileCatalog::clearSnapshotAndWatches()
{
    records_.clear();
    cleanDirectory_ = {};
    cleanDirectoryFingerprint_ = {};
    hasCleanSnapshot_ = false;
    deactivateCallback();
    watcher_.clear();
}
