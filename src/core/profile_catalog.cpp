// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include "core/profile_catalog.h"

#include <QDir>
#include <QSet>

#include <algorithm>
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
                                      const QList<FileFingerprint> &fingerprints)
{
    const std::optional<QString> cleanDirectory = cleanAbsolutePath(directory.hostPath);
    if (!cleanDirectory.has_value()) {
        return std::nullopt;
    }

    QSet<QString> seenPaths{*cleanDirectory};
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
    paths.append(files);
    return paths;
}

ProfileCatalogError mapRepositoryError(ProfileRepositoryError error)
{
    return error == ProfileRepositoryError::UnreadableDirectory
        ? ProfileCatalogError::UnreadableDirectory
        : ProfileCatalogError::NoProfileDirectory;
}

} // namespace

ProfileCatalog::ProfileCatalog(ProfileRepositorySource &repository, ProfileWatcher &watcher)
    : ProfileCatalog(repository, watcher, locateProfileDirectory)
{
}

ProfileCatalog::ProfileCatalog(ProfileRepositorySource &repository,
                               ProfileWatcher &watcher,
                               ProfileDirectoryLocator locator)
    : repository_(repository)
    , watcher_(watcher)
    , locator_(std::move(locator))
{
    if (!locator_) {
        locator_ = locateProfileDirectory;
    }
}

ProfileCatalog::~ProfileCatalog()
{
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
    RepositoryLoadResult loadResult = repository_.load(instance);
    if (const auto *error = std::get_if<ProfileRepositoryError>(&loadResult)) {
        clearSnapshotAndWatches();
        selectedInstanceId_ = instance.id;
        hasSelectedInstance_ = true;
        sessionActive_ = true;
        dirty_ = true;
        return mapRepositoryError(*error);
    }

    ProfileSnapshot snapshot = std::get<ProfileSnapshot>(std::move(loadResult));
    records_ = std::move(snapshot.profiles);
    const std::optional<LocatedProfileDirectory> directory = locator_(instance);
    if (!directory.has_value()) {
        watcher_.clear();
        dirty_ = true;
        return records_;
    }

    const std::optional<QStringList> paths = watchPaths(*directory, snapshot.fingerprint);
    if (!paths.has_value()) {
        watcher_.clear();
        dirty_ = true;
        return records_;
    }

    dirty_ = !watcher_.replacePaths(*paths, [this] { markDirty(); });
    return records_;
}

const ProfileRecord *ProfileCatalog::resolve(QStringView opaqueId) const
{
    for (const ProfileRecord &record : records_) {
        if (record.opaqueId == opaqueId) {
            return &record;
        }
    }
    return nullptr;
}

void ProfileCatalog::markDirty()
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

void ProfileCatalog::clearSnapshotAndWatches()
{
    records_.clear();
    watcher_.clear();
}
