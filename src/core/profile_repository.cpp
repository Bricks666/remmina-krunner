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

struct Candidate {
    QString entryName;
    QString sourcePath;
    QString launchPath;
    QString canonicalIdentity;
    FileFingerprint fingerprint;
};

using EnumerationResult = std::variant<QList<Candidate>, ProfileRepositoryError>;

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

bool sameFingerprint(const FileFingerprint &left, const FileFingerprint &right)
{
    return left.path == right.path && left.size == right.size
        && left.modifiedMilliseconds == right.modifiedMilliseconds;
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

    DIR *stream = ::fdopendir(descriptor);
    if (stream == nullptr) {
        ::close(descriptor);
        return ProfileRepositoryError::UnreadableDirectory;
    }

    QList<Candidate> discovered;
    bool enumerationFailed = false;
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

        struct stat metadata {};
        int statResult = -1;
        do {
            statResult = ::fstatat(::dirfd(stream), encodedName.constData(), &metadata, 0);
        } while (statResult < 0 && errno == EINTR);
        if (statResult < 0 || !S_ISREG(metadata.st_mode)) {
            continue;
        }

        int fileDescriptor = -1;
        do {
            fileDescriptor = ::openat(::dirfd(stream),
                                      encodedName.constData(),
                                      O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        } while (fileDescriptor < 0 && errno == EINTR);
        if (fileDescriptor >= 0) {
            struct stat openedMetadata {};
            int openedStatResult = -1;
            do {
                openedStatResult = ::fstat(fileDescriptor, &openedMetadata);
            } while (openedStatResult < 0 && errno == EINTR);
            ::close(fileDescriptor);
            if (openedStatResult < 0 || !S_ISREG(openedMetadata.st_mode)) {
                continue;
            }
            metadata = openedMetadata;
        }

        const QString sourcePath = cleanChildPath(directory.hostPath, entryName);
        const QString canonicalIdentity =
            QDir::cleanPath(QFileInfo(sourcePath).canonicalFilePath());
        if (canonicalIdentity.isEmpty() || !QDir::isAbsolutePath(canonicalIdentity)) {
            continue;
        }
        discovered.append({
            .entryName = entryName,
            .sourcePath = sourcePath,
            .launchPath = cleanChildPath(directory.launchPath, entryName),
            .canonicalIdentity = canonicalIdentity,
            .fingerprint = {
                .path = canonicalIdentity,
                .size = static_cast<qint64>(metadata.st_size),
                .modifiedMilliseconds = modifiedMilliseconds(metadata),
            },
        });
    }
    ::closedir(stream);
    if (enumerationFailed) {
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
    profile_locator_detail::LocationResult locationResult =
        profile_locator_detail::locateProfileDirectoryWithError(instance);
    if (const auto *error =
            std::get_if<profile_locator_detail::LocationError>(&locationResult)) {
        return *error == profile_locator_detail::LocationError::Unreadable
            ? ProfileRepositoryError::UnreadableDirectory
            : ProfileRepositoryError::NoProfileDirectory;
    }
    const LocatedProfileDirectory directory =
        std::get<LocatedProfileDirectory>(std::move(locationResult));

    EnumerationResult enumeration = enumerateCandidates(directory);
    if (const auto *error = std::get_if<ProfileRepositoryError>(&enumeration)) {
        return *error;
    }
    QList<Candidate> candidates = std::get<QList<Candidate>>(std::move(enumeration));

    ProfileSnapshot snapshot;
    snapshot.profiles.reserve(candidates.size());
    snapshot.fingerprint.reserve(candidates.size());
    QSet<QString> activeCacheKeys;

    for (const Candidate &candidate : candidates) {
        const QString opaqueId = profile_repository_detail::opaqueProfileId(
            QStringView(instance.id), QStringView(candidate.canonicalIdentity));
        const QString key = cacheKey(instance, directory, candidate.canonicalIdentity);
        activeCacheKeys.insert(key);

        ProfileParseResult parsed;
        auto cached = cache_.find(key);
        if (cached != cache_.end() && cached->instanceId == instance.id
            && cached->directory == directory.hostPath
            && cached->canonicalIdentity == candidate.canonicalIdentity
            && sameFingerprint(cached->fingerprint, candidate.fingerprint)) {
            parsed = cached->result;
        } else {
            parsed = parser_(candidate.sourcePath, candidate.launchPath, opaqueId);
            normalizeRecord(parsed, candidate, opaqueId);
            cache_.insert(key,
                          {
                              .instanceId = instance.id,
                              .directory = directory.hostPath,
                              .canonicalIdentity = candidate.canonicalIdentity,
                              .fingerprint = candidate.fingerprint,
                              .result = parsed,
                          });
        }
        normalizeRecord(parsed, candidate, opaqueId);
        snapshot.fingerprint.append(candidate.fingerprint);
        if (const ProfileRecord *record = std::get_if<ProfileRecord>(&parsed)) {
            snapshot.profiles.append(*record);
        }
    }

    for (auto iterator = cache_.begin(); iterator != cache_.end();) {
        if (iterator->instanceId == instance.id && iterator->directory == directory.hostPath
            && !activeCacheKeys.contains(iterator.key())) {
            iterator = cache_.erase(iterator);
        } else {
            ++iterator;
        }
    }

    return snapshot;
}
