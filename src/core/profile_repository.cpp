// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include "core/profile_repository.h"

#include "core/profile_locator.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace {

struct InternalFileSignature {
    quint64 device;
    quint64 inode;
    qint64 size;
    qint64 modifiedSeconds;
    qint64 modifiedNanoseconds;
    qint64 changedSeconds;
    qint64 changedNanoseconds;
};

struct Candidate {
    QString entryName;
    QString sourcePath;
    QString launchPath;
    QString canonicalIdentity;
    FileFingerprint fingerprint;
    InternalFileSignature signature;
};

using EnumerationResult = std::variant<QList<Candidate>, ProfileRepositoryError>;

struct CandidateInspection {
    std::optional<Candidate> candidate;
    bool directoryUnreadable = false;
};

QString cleanChildPath(const QString &directory, const QString &entryName)
{
    return QDir::cleanPath(QDir(directory).filePath(entryName));
}

qint64 modifiedMilliseconds(const struct stat &metadata)
{
#if defined(__APPLE__)
    return static_cast<qint64>(metadata.st_mtimespec.tv_sec) * 1000
        + metadata.st_mtimespec.tv_nsec / 1000000;
#else
    return static_cast<qint64>(metadata.st_mtim.tv_sec) * 1000
        + metadata.st_mtim.tv_nsec / 1000000;
#endif
}

InternalFileSignature internalSignature(const struct stat &metadata)
{
#if defined(__APPLE__)
    return {
        .device = static_cast<quint64>(metadata.st_dev),
        .inode = static_cast<quint64>(metadata.st_ino),
        .size = static_cast<qint64>(metadata.st_size),
        .modifiedSeconds = static_cast<qint64>(metadata.st_mtimespec.tv_sec),
        .modifiedNanoseconds = static_cast<qint64>(metadata.st_mtimespec.tv_nsec),
        .changedSeconds = static_cast<qint64>(metadata.st_ctimespec.tv_sec),
        .changedNanoseconds = static_cast<qint64>(metadata.st_ctimespec.tv_nsec),
    };
#else
    return {
        .device = static_cast<quint64>(metadata.st_dev),
        .inode = static_cast<quint64>(metadata.st_ino),
        .size = static_cast<qint64>(metadata.st_size),
        .modifiedSeconds = static_cast<qint64>(metadata.st_mtim.tv_sec),
        .modifiedNanoseconds = static_cast<qint64>(metadata.st_mtim.tv_nsec),
        .changedSeconds = static_cast<qint64>(metadata.st_ctim.tv_sec),
        .changedNanoseconds = static_cast<qint64>(metadata.st_ctim.tv_nsec),
    };
#endif
}

bool sameSignature(const InternalFileSignature &left, const InternalFileSignature &right)
{
    return left.device == right.device && left.inode == right.inode && left.size == right.size
        && left.modifiedSeconds == right.modifiedSeconds
        && left.modifiedNanoseconds == right.modifiedNanoseconds
        && left.changedSeconds == right.changedSeconds
        && left.changedNanoseconds == right.changedNanoseconds;
}

void addLength(QCryptographicHash &hash, quint64 length)
{
    char encodedLength[8];
    for (int index = 7; index >= 0; --index) {
        encodedLength[index] = static_cast<char>(length & 0xffU);
        length >>= 8U;
    }
    hash.addData(QByteArrayView(encodedLength, sizeof(encodedLength)));
}

QString hashTuple(const QList<QString> &fields)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    addLength(hash, static_cast<quint64>(fields.size()));
    for (const QString &field : fields) {
        QByteArray encoded = field.toUtf8();
        addLength(hash, static_cast<quint64>(encoded.size()));
        hash.addData(QByteArrayView(encoded));
        encoded.fill('\0');
    }
    return QString::fromLatin1(hash.result().toHex());
}

QString cacheKey(const RemminaInstance &instance,
                 const LocatedProfileDirectory &directory,
                 const QString &canonicalIdentity)
{
    return hashTuple({
        QStringLiteral("remmina-krunner-profile-cache-v1"),
        instance.id,
        directory.hostPath,
        canonicalIdentity,
    });
}

bool directoryAccessible(int descriptor)
{
    int accessResult = -1;
    do {
        accessResult = ::faccessat(descriptor, ".", R_OK | X_OK, AT_EACCESS);
    } while (accessResult < 0 && errno == EINTR);
    return accessResult == 0;
}

QString canonicalIdentityForDescriptor(int descriptor)
{
    const QByteArray descriptorPath = QByteArray("/proc/self/fd/")
        + QByteArray::number(descriptor);
    QByteArray resolved(4096, Qt::Uninitialized);
    ssize_t length = -1;
    do {
        length = ::readlink(descriptorPath.constData(), resolved.data(), resolved.size());
    } while (length < 0 && errno == EINTR);
    if (length < 0 || length == resolved.size()) {
        return {};
    }
    resolved.truncate(static_cast<qsizetype>(length));
    QString identity = QFile::decodeName(resolved);
    if (identity.endsWith(QStringLiteral(" (deleted)"), Qt::CaseSensitive)
        || !QDir::isAbsolutePath(identity)) {
        return {};
    }
    return QDir::cleanPath(identity);
}

CandidateInspection inspectCandidateAt(int directoryDescriptor,
                                       const LocatedProfileDirectory &directory,
                                       const QByteArray &encodedName,
                                       const QString &entryName)
{
    struct stat metadata {};
    int statResult = -1;
    do {
        statResult =
            ::fstatat(directoryDescriptor, encodedName.constData(), &metadata, 0);
    } while (statResult < 0 && errno == EINTR);
    const int statError = statResult < 0 ? errno : 0;
    if (statResult < 0) {
        return {
            .candidate = std::nullopt,
            .directoryUnreadable = (statError == EACCES || statError == EPERM)
                && !directoryAccessible(directoryDescriptor),
        };
    }
    if (!S_ISREG(metadata.st_mode)) {
        return {};
    }

    int fileDescriptor = -1;
    do {
        fileDescriptor = ::openat(directoryDescriptor,
                                  encodedName.constData(),
                                  O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    } while (fileDescriptor < 0 && errno == EINTR);
    const int openError = fileDescriptor < 0 ? errno : 0;
    QString canonicalIdentity;
    if (fileDescriptor >= 0) {
        struct stat openedMetadata {};
        int openedStatResult = -1;
        do {
            openedStatResult = ::fstat(fileDescriptor, &openedMetadata);
        } while (openedStatResult < 0 && errno == EINTR);
        if (openedStatResult == 0 && S_ISREG(openedMetadata.st_mode)) {
            metadata = openedMetadata;
            canonicalIdentity = canonicalIdentityForDescriptor(fileDescriptor);
        }
        ::close(fileDescriptor);
        if (openedStatResult < 0 || !S_ISREG(openedMetadata.st_mode)) {
            return {};
        }
    } else if ((openError == EACCES || openError == EPERM)
               && !directoryAccessible(directoryDescriptor)) {
        return {.candidate = std::nullopt, .directoryUnreadable = true};
    } else if (openError != EACCES && openError != EPERM) {
        return {};
    }

    const QString sourcePath = cleanChildPath(directory.hostPath, entryName);
    if (canonicalIdentity.isEmpty()) {
        canonicalIdentity = QDir::cleanPath(QFileInfo(sourcePath).canonicalFilePath());
    }
    if (canonicalIdentity.isEmpty() || !QDir::isAbsolutePath(canonicalIdentity)) {
        return {};
    }
    const InternalFileSignature signature = internalSignature(metadata);
    return {
        .candidate = Candidate{
            .entryName = entryName,
            .sourcePath = sourcePath,
            .launchPath = cleanChildPath(directory.launchPath, entryName),
            .canonicalIdentity = canonicalIdentity,
            .fingerprint = {
                .path = canonicalIdentity,
                .size = signature.size,
                .modifiedMilliseconds = modifiedMilliseconds(metadata),
            },
            .signature = signature,
        },
    };
}

CandidateInspection inspectCandidate(const LocatedProfileDirectory &directory,
                                     const QString &entryName)
{
    QByteArray encodedDirectory = QFile::encodeName(directory.hostPath);
    int descriptor = -1;
    do {
        descriptor = ::open(encodedDirectory.constData(),
                            O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_DIRECTORY);
    } while (descriptor < 0 && errno == EINTR);
    encodedDirectory.fill('\0');
    if (descriptor < 0 || !directoryAccessible(descriptor)) {
        if (descriptor >= 0) {
            ::close(descriptor);
        }
        return {.candidate = std::nullopt, .directoryUnreadable = true};
    }

    const QByteArray encodedName = QFile::encodeName(entryName);
    CandidateInspection inspection =
        inspectCandidateAt(descriptor, directory, encodedName, entryName);
    ::close(descriptor);
    return inspection;
}

EnumerationResult enumerateCandidates(const LocatedProfileDirectory &directory)
{
    QByteArray encodedDirectory = QFile::encodeName(directory.hostPath);
    int descriptor = -1;
    do {
        descriptor = ::open(encodedDirectory.constData(),
                            O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_DIRECTORY);
    } while (descriptor < 0 && errno == EINTR);
    encodedDirectory.fill('\0');
    if (descriptor < 0) {
        return ProfileRepositoryError::UnreadableDirectory;
    }

    if (!directoryAccessible(descriptor)) {
        ::close(descriptor);
        return ProfileRepositoryError::UnreadableDirectory;
    }

    DIR *stream = ::fdopendir(descriptor);
    if (stream == nullptr) {
        ::close(descriptor);
        return ProfileRepositoryError::UnreadableDirectory;
    }

    QList<Candidate> discovered;
    bool enumerationFailed = false;
    bool directoryBecameUnreadable = false;
    while (true) {
        errno = 0;
        dirent *entry = ::readdir(stream);
        if (entry == nullptr) {
            enumerationFailed = errno != 0;
            break;
        }

        const QByteArray encodedName(entry->d_name);
        if (encodedName == QByteArray(".") || encodedName == QByteArray("..")) {
            continue;
        }
        const QString entryName = QFile::decodeName(encodedName);
        if (QFile::encodeName(entryName) != encodedName
            || !entryName.endsWith(QStringLiteral(".remmina"), Qt::CaseSensitive)) {
            continue;
        }

        CandidateInspection inspection =
            inspectCandidateAt(::dirfd(stream), directory, encodedName, entryName);
        if (inspection.directoryUnreadable) {
            directoryBecameUnreadable = true;
            break;
        }
        if (inspection.candidate.has_value()) {
            discovered.append(std::move(*inspection.candidate));
        }
    }
    ::closedir(stream);
    if (enumerationFailed || directoryBecameUnreadable) {
        return ProfileRepositoryError::UnreadableDirectory;
    }

    std::sort(discovered.begin(),
              discovered.end(),
              [](const Candidate &left, const Candidate &right) {
                  const int nameOrder =
                      QString::compare(left.entryName, right.entryName, Qt::CaseSensitive);
                  if (nameOrder != 0) {
                      return nameOrder < 0;
                  }
                  return QString::compare(left.sourcePath, right.sourcePath, Qt::CaseSensitive) < 0;
              });

    QList<Candidate> candidates;
    QSet<QString> seenIdentities;
    for (Candidate &candidate : discovered) {
        if (seenIdentities.contains(candidate.canonicalIdentity)) {
            continue;
        }
        seenIdentities.insert(candidate.canonicalIdentity);
        candidates.append(std::move(candidate));
    }
    return candidates;
}

void normalizeRecord(ProfileParseResult &result,
                     const Candidate &candidate,
                     const QString &opaqueId)
{
    if (ProfileRecord *record = std::get_if<ProfileRecord>(&result)) {
        record->opaqueId = opaqueId;
        record->sourcePath = candidate.sourcePath;
        record->launchPath = candidate.launchPath;
    }
}

} // namespace

QString profile_repository_detail::opaqueProfileId(QStringView instanceId,
                                                    QStringView canonicalIdentity)
{
    return hashTuple({
        QStringLiteral("remmina-krunner-profile-id-v1"),
        instanceId.toString(),
        canonicalIdentity.toString(),
    });
}

ProfileRepository::ProfileRepository(ProfileParserFunction parser)
    : parser_(std::move(parser))
{
    if (!parser_) {
        parser_ = parseRemminaProfile;
    }
}

std::variant<ProfileSnapshot, ProfileRepositoryError> ProfileRepository::load(
    const RemminaInstance &instance)
{
    ProfileLocationResult locationResult =
        profile_locator_detail::locateProfileDirectoryDetailed(instance);
    if (const auto *error = std::get_if<ProfileLocationError>(&locationResult)) {
        cache_.clear();
        activeInstanceId_.clear();
        activeDirectory_.clear();
        hasActiveScope_ = false;
        return *error == ProfileLocationError::Unreadable
            ? ProfileRepositoryError::UnreadableDirectory
            : ProfileRepositoryError::NoProfileDirectory;
    }
    const LocatedProfileDirectory directory =
        std::get<LocatedProfileDirectory>(std::move(locationResult));

    if (!hasActiveScope_ || activeInstanceId_ != instance.id
        || activeDirectory_ != directory.hostPath) {
        cache_.clear();
        activeInstanceId_ = instance.id;
        activeDirectory_ = directory.hostPath;
        hasActiveScope_ = true;
    }

    EnumerationResult enumeration = enumerateCandidates(directory);
    if (const auto *error = std::get_if<ProfileRepositoryError>(&enumeration)) {
        return *error;
    }
    QList<Candidate> candidates = std::get<QList<Candidate>>(std::move(enumeration));

    ProfileSnapshot snapshot;
    snapshot.profiles.reserve(candidates.size());
    snapshot.fingerprint.reserve(candidates.size());
    QSet<QString> activeCacheKeys;
    QSet<QString> processedIdentities;

    for (const Candidate &enumeratedCandidate : candidates) {
        Candidate candidate = enumeratedCandidate;
        if (processedIdentities.contains(candidate.canonicalIdentity)) {
            continue;
        }
        QString key = cacheKey(instance, directory, candidate.canonicalIdentity);
        auto cached = cache_.find(key);
        const bool cacheHit = cached != cache_.end() && cached->instanceId == instance.id
            && cached->directory == directory.hostPath
            && cached->canonicalIdentity == candidate.canonicalIdentity
            && cached->device == candidate.signature.device
            && cached->inode == candidate.signature.inode
            && cached->size == candidate.signature.size
            && cached->modifiedSeconds == candidate.signature.modifiedSeconds
            && cached->modifiedNanoseconds == candidate.signature.modifiedNanoseconds
            && cached->changedSeconds == candidate.signature.changedSeconds
            && cached->changedNanoseconds == candidate.signature.changedNanoseconds;
        if (cacheHit) {
            ProfileParseResult parsed = cached->result;
            const QString opaqueId = profile_repository_detail::opaqueProfileId(
                QStringView(instance.id), QStringView(candidate.canonicalIdentity));
            normalizeRecord(parsed, candidate, opaqueId);
            activeCacheKeys.insert(key);
            processedIdentities.insert(candidate.canonicalIdentity);
            snapshot.fingerprint.append(candidate.fingerprint);
            if (const ProfileRecord *record = std::get_if<ProfileRecord>(&parsed)) {
                snapshot.profiles.append(*record);
            }
            continue;
        }

        cache_.remove(key);
        std::optional<ProfileParseResult> stableResult;
        bool directoryBecameUnreadable = false;
        bool candidateStillEligible = true;
        for (int attempt = 0; attempt < 2; ++attempt) {
            const QString opaqueId = profile_repository_detail::opaqueProfileId(
                QStringView(instance.id), QStringView(candidate.canonicalIdentity));
            ProfileParseResult parsed =
                parser_(candidate.sourcePath, candidate.launchPath, opaqueId);
            normalizeRecord(parsed, candidate, opaqueId);

            CandidateInspection postParse = inspectCandidate(directory, candidate.entryName);
            if (postParse.directoryUnreadable) {
                directoryBecameUnreadable = true;
                break;
            }
            if (!postParse.candidate.has_value()) {
                candidateStillEligible = false;
                break;
            }

            if (postParse.candidate->canonicalIdentity == candidate.canonicalIdentity
                && sameSignature(postParse.candidate->signature, candidate.signature)) {
                stableResult = std::move(parsed);
                break;
            }

            cache_.remove(key);
            candidate = std::move(*postParse.candidate);
            key = cacheKey(instance, directory, candidate.canonicalIdentity);
            cache_.remove(key);
        }
        if (directoryBecameUnreadable) {
            return ProfileRepositoryError::UnreadableDirectory;
        }
        if (!stableResult.has_value()) {
            if (candidateStillEligible
                && !processedIdentities.contains(candidate.canonicalIdentity)) {
                processedIdentities.insert(candidate.canonicalIdentity);
                snapshot.fingerprint.append(candidate.fingerprint);
            }
            continue;
        }
        if (processedIdentities.contains(candidate.canonicalIdentity)) {
            cache_.remove(key);
            continue;
        }

        activeCacheKeys.insert(key);
        processedIdentities.insert(candidate.canonicalIdentity);
        snapshot.fingerprint.append(candidate.fingerprint);
        const bool transientUnreadable = std::holds_alternative<ProfileParseError>(*stableResult)
            && std::get<ProfileParseError>(*stableResult) == ProfileParseError::Unreadable;
        if (!transientUnreadable) {
            cache_.insert(key,
                          {
                              .instanceId = instance.id,
                              .directory = directory.hostPath,
                              .canonicalIdentity = candidate.canonicalIdentity,
                              .fingerprint = candidate.fingerprint,
                              .device = candidate.signature.device,
                              .inode = candidate.signature.inode,
                              .size = candidate.signature.size,
                              .modifiedSeconds = candidate.signature.modifiedSeconds,
                              .modifiedNanoseconds = candidate.signature.modifiedNanoseconds,
                              .changedSeconds = candidate.signature.changedSeconds,
                              .changedNanoseconds = candidate.signature.changedNanoseconds,
                              .result = *stableResult,
                          });
        }
        if (const ProfileRecord *record = std::get_if<ProfileRecord>(&*stableResult)) {
            snapshot.profiles.append(*record);
        }
    }

    for (auto iterator = cache_.begin(); iterator != cache_.end();) {
        if (!activeCacheKeys.contains(iterator.key())) {
            iterator = cache_.erase(iterator);
        } else {
            ++iterator;
        }
    }

    return snapshot;
}
