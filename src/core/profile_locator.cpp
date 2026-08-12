// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include "core/profile_locator.h"

#include "core/key_file_reader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QSet>

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr auto flatpakApplicationRoot = "/.var/app/org.remmina.Remmina";

struct PathMapping {
    QString hostRoot;
    QString launchRoot;
};

enum class DirectoryStatus {
    Invalid,
    Unreadable,
    Readable,
};

QString cleanAbsolutePath(const QString &path)
{
    if (path.isEmpty() || !QDir::isAbsolutePath(path)) {
        return {};
    }
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString childPath(const QString &parent, QStringView child)
{
    const QString absoluteParent = cleanAbsolutePath(parent);
    if (absoluteParent.isEmpty()) {
        return {};
    }
    return QDir::cleanPath(QDir(absoluteParent).filePath(child.toString()));
}

DirectoryStatus directoryStatus(const QString &path)
{
    const QString absolutePath = cleanAbsolutePath(path);
    if (absolutePath.isEmpty()) {
        return DirectoryStatus::Invalid;
    }

    QByteArray encodedPath = QFile::encodeName(absolutePath);
    struct stat pathMetadata {};
    int pathStatResult = -1;
    do {
        pathStatResult = ::stat(encodedPath.constData(), &pathMetadata);
    } while (pathStatResult < 0 && errno == EINTR);
    const int pathStatError = pathStatResult < 0 ? errno : 0;
    if (pathStatResult < 0) {
        encodedPath.fill('\0');
        return pathStatError == EACCES || pathStatError == EPERM ? DirectoryStatus::Unreadable
                                                                 : DirectoryStatus::Invalid;
    }
    if (!S_ISDIR(pathMetadata.st_mode)) {
        encodedPath.fill('\0');
        return DirectoryStatus::Invalid;
    }

    int accessResult = -1;
    do {
        accessResult =
            ::faccessat(AT_FDCWD, encodedPath.constData(), R_OK | X_OK, AT_EACCESS);
    } while (accessResult < 0 && errno == EINTR);
    const int accessError = accessResult < 0 ? errno : 0;
    if (accessResult < 0) {
        encodedPath.fill('\0');
        return accessError == EACCES || accessError == EPERM ? DirectoryStatus::Unreadable
                                                             : DirectoryStatus::Invalid;
    }

    int descriptor = -1;
    do {
        descriptor =
            ::open(encodedPath.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_DIRECTORY);
    } while (descriptor < 0 && errno == EINTR);
    encodedPath.fill('\0');
    if (descriptor < 0) {
        return DirectoryStatus::Unreadable;
    }

    struct stat metadata {};
    int statResult = -1;
    do {
        statResult = ::fstat(descriptor, &metadata);
    } while (statResult < 0 && errno == EINTR);
    const bool readableDirectory = statResult == 0 && S_ISDIR(metadata.st_mode);
    ::close(descriptor);
    return readableDirectory ? DirectoryStatus::Readable : DirectoryStatus::Invalid;
}

std::optional<QString> pathBelow(const QString &path, const QString &root)
{
    const QString absolutePath = cleanAbsolutePath(path);
    const QString absoluteRoot = cleanAbsolutePath(root);
    if (absolutePath.isEmpty() || absoluteRoot.isEmpty()) {
        return std::nullopt;
    }
    if (absolutePath == absoluteRoot) {
        return QString{};
    }
    const QString prefix = absoluteRoot == QStringLiteral("/")
        ? absoluteRoot
        : absoluteRoot + QLatin1Char('/');
    if (!absolutePath.startsWith(prefix, Qt::CaseSensitive)) {
        return std::nullopt;
    }
    return absolutePath.sliced(prefix.size());
}

QString appendRelativePath(const QString &root, const QString &relativePath)
{
    return relativePath.isEmpty() ? cleanAbsolutePath(root)
                                  : childPath(root, QStringView(relativePath));
}

std::optional<QString> flatpakHostHome(const RemminaInstance &instance)
{
    const QString suffixBase = QString::fromLatin1(flatpakApplicationRoot);
    const QList<std::pair<QString, QString>> roots{
        {instance.profiles.configHome, suffixBase + QStringLiteral("/config")},
        {instance.profiles.dataHome, suffixBase + QStringLiteral("/data")},
        {instance.profiles.legacyHome, suffixBase + QStringLiteral("/.remmina")},
    };

    std::optional<QString> home;
    for (const auto &[root, suffix] : roots) {
        const QString absoluteRoot = cleanAbsolutePath(root);
        if (absoluteRoot.size() <= suffix.size()
            || !absoluteRoot.endsWith(suffix, Qt::CaseSensitive)) {
            return std::nullopt;
        }
        const QString candidateHome = absoluteRoot.first(absoluteRoot.size() - suffix.size());
        if (candidateHome.isEmpty() || !QDir::isAbsolutePath(candidateHome)
            || (home.has_value() && *home != candidateHome)) {
            return std::nullopt;
        }
        home = candidateHome;
    }
    return home;
}

std::optional<QList<PathMapping>> flatpakMappings(const RemminaInstance &instance)
{
    const std::optional<QString> home = flatpakHostHome(instance);
    if (!home.has_value()) {
        return std::nullopt;
    }
    return QList<PathMapping>{
        {
            .hostRoot = cleanAbsolutePath(instance.profiles.dataHome),
            .launchRoot = childPath(*home, u".local/share"),
        },
        {
            .hostRoot = cleanAbsolutePath(instance.profiles.configHome),
            .launchRoot = childPath(*home, u".config"),
        },
        {
            .hostRoot = cleanAbsolutePath(instance.profiles.legacyHome),
            .launchRoot = childPath(*home, u".remmina"),
        },
    };
}

std::optional<LocatedProfileDirectory> existingLocation(
    QString hostPath, QString launchPath, bool &sawUnreadable)
{
    hostPath = cleanAbsolutePath(hostPath);
    launchPath = cleanAbsolutePath(launchPath);
    if (hostPath.isEmpty() || launchPath.isEmpty()) {
        return std::nullopt;
    }
    const DirectoryStatus status = directoryStatus(hostPath);
    sawUnreadable = sawUnreadable || status == DirectoryStatus::Unreadable;
    if (status != DirectoryStatus::Readable) {
        return std::nullopt;
    }
    return LocatedProfileDirectory{
        .hostPath = std::move(hostPath),
        .launchPath = std::move(launchPath),
    };
}

std::optional<LocatedProfileDirectory> flatpakCustomLocation(
    const QString &customPath, const QList<PathMapping> &mappings, bool &sawUnreadable)
{
    const QString absoluteCustomPath = cleanAbsolutePath(customPath);
    if (absoluteCustomPath.isEmpty()) {
        return std::nullopt;
    }

    for (const PathMapping &mapping : mappings) {
        if (const std::optional<QString> relative =
                pathBelow(absoluteCustomPath, mapping.launchRoot)) {
            return existingLocation(appendRelativePath(mapping.hostRoot, *relative),
                                    absoluteCustomPath,
                                    sawUnreadable);
        }
    }
    for (const PathMapping &mapping : mappings) {
        if (const std::optional<QString> relative =
                pathBelow(absoluteCustomPath, mapping.hostRoot)) {
            return existingLocation(absoluteCustomPath,
                                    appendRelativePath(mapping.launchRoot, *relative),
                                    sawUnreadable);
        }
    }

    return existingLocation(absoluteCustomPath, absoluteCustomPath, sawUnreadable);
}

std::optional<LocatedProfileDirectory> flatpakHostLocation(
    const QString &hostPath, const QList<PathMapping> &mappings, bool &sawUnreadable)
{
    const QString absoluteHostPath = cleanAbsolutePath(hostPath);
    for (const PathMapping &mapping : mappings) {
        if (const std::optional<QString> relative =
                pathBelow(absoluteHostPath, mapping.hostRoot)) {
            return existingLocation(absoluteHostPath,
                                    appendRelativePath(mapping.launchRoot, *relative),
                                    sawUnreadable);
        }
    }
    return std::nullopt;
}

std::optional<QString> customDataDirectory(const RemminaInstance &instance)
{
    const QString preferences =
        childPath(childPath(instance.profiles.configHome, u"remmina"), u"remmina.pref");
    if (preferences.isEmpty()) {
        return std::nullopt;
    }

    static const QSet<QString> allowedKeys{QStringLiteral("datadir_path")};
    const std::optional<AllowedValues> values = readAllowedKeyFileValues(
        preferences, QStringView(u"remmina_pref"), allowedKeys);
    if (!values.has_value()) {
        return std::nullopt;
    }
    const QString value = values->value(QStringLiteral("datadir_path"));
    if (value.isEmpty() || !QDir::isAbsolutePath(value)) {
        return std::nullopt;
    }
    return value;
}

} // namespace

ProfileLocationResult locateProfileDirectory(const RemminaInstance &instance)
{
    bool sawUnreadable = false;
    const std::optional<QList<PathMapping>> mappings = instance.kind == InstanceKind::Flatpak
        ? flatpakMappings(instance)
        : std::optional<QList<PathMapping>>{};

    if (const std::optional<QString> customPath = customDataDirectory(instance)) {
        std::optional<LocatedProfileDirectory> custom;
        if (instance.kind == InstanceKind::Flatpak && mappings.has_value()) {
            custom = flatpakCustomLocation(*customPath, *mappings, sawUnreadable);
        } else if (instance.kind != InstanceKind::Flatpak) {
            custom = existingLocation(*customPath, *customPath, sawUnreadable);
        } else {
            custom = existingLocation(*customPath, *customPath, sawUnreadable);
        }
        if (custom.has_value()) {
            return std::move(*custom);
        }
        if (sawUnreadable) {
            return ProfileLocationError::Unreadable;
        }
    }

    QStringList hostCandidates{
        instance.profiles.legacyHome,
        childPath(instance.profiles.dataHome, u"remmina"),
    };
    if (instance.kind == InstanceKind::Native) {
        for (const QString &systemDataHome : instance.profiles.systemDataHomes) {
            hostCandidates.append(childPath(systemDataHome, u"remmina"));
        }
    }

    for (const QString &hostCandidate : hostCandidates) {
        std::optional<LocatedProfileDirectory> location;
        if (instance.kind == InstanceKind::Flatpak) {
            if (mappings.has_value()) {
                location = flatpakHostLocation(hostCandidate, *mappings, sawUnreadable);
            }
        } else {
            location = existingLocation(hostCandidate, hostCandidate, sawUnreadable);
        }
        if (location.has_value()) {
            return std::move(*location);
        }
        if (sawUnreadable) {
            return ProfileLocationError::Unreadable;
        }
    }
    return ProfileLocationError::NotFound;
}
