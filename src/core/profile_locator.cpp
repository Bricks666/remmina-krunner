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
constexpr auto snapApplicationRoot = "/snap/remmina/current";

enum class DirectoryStatus {
    Invalid,
    Unreadable,
    Readable,
};

bool isStructuralPathError(int error)
{
    return error == ENOENT || error == ENOTDIR || error == ELOOP
        || error == ENAMETOOLONG;
}

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
        return isStructuralPathError(pathStatError) ? DirectoryStatus::Invalid
                                                    : DirectoryStatus::Unreadable;
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
        return isStructuralPathError(accessError) ? DirectoryStatus::Invalid
                                                  : DirectoryStatus::Unreadable;
    }

    int descriptor = -1;
    do {
        descriptor =
            ::open(encodedPath.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_DIRECTORY);
    } while (descriptor < 0 && errno == EINTR);
    const int openError = descriptor < 0 ? errno : 0;
    encodedPath.fill('\0');
    if (descriptor < 0) {
        return isStructuralPathError(openError) ? DirectoryStatus::Invalid
                                                : DirectoryStatus::Unreadable;
    }

    struct stat metadata {};
    int statResult = -1;
    do {
        statResult = ::fstat(descriptor, &metadata);
    } while (statResult < 0 && errno == EINTR);
    ::close(descriptor);
    if (statResult < 0) {
        return DirectoryStatus::Unreadable;
    }
    return S_ISDIR(metadata.st_mode) ? DirectoryStatus::Readable : DirectoryStatus::Invalid;
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

std::optional<QString> resolvedPathForContainment(QString path)
{
    path = cleanAbsolutePath(path);
    if (path.isEmpty()) {
        return std::nullopt;
    }

    constexpr int maximumSymbolicLinks = 40;
    for (int followedLinks = 0; followedLinks < maximumSymbolicLinks; ++followedLinks) {
        const QStringList components = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        QString current = QStringLiteral("/");
        bool followedLink = false;
        for (qsizetype index = 0; index < components.size(); ++index) {
            current = childPath(current, QStringView(components.at(index)));
            const QFileInfo component(current);
            if (!component.isSymbolicLink()) {
                continue;
            }

            QString target = cleanAbsolutePath(component.symLinkTarget());
            if (target.isEmpty()) {
                return std::nullopt;
            }
            for (++index; index < components.size(); ++index) {
                target = childPath(target, QStringView(components.at(index)));
            }
            path = std::move(target);
            followedLink = true;
            break;
        }
        if (!followedLink) {
            const QString canonical = QFileInfo(path).canonicalFilePath();
            return canonical.isEmpty() ? std::optional<QString>(path)
                                       : std::optional<QString>(cleanAbsolutePath(canonical));
        }
    }
    return std::nullopt;
}

std::optional<QString> rootBeforeSuffix(const QString &path, const QString &suffix)
{
    const QString absolutePath = cleanAbsolutePath(path);
    if (absolutePath.size() <= suffix.size()
        || !absolutePath.endsWith(suffix, Qt::CaseSensitive)) {
        return std::nullopt;
    }
    const QString root = absolutePath.first(absolutePath.size() - suffix.size());
    return root.isEmpty() || !QDir::isAbsolutePath(root) ? std::nullopt
                                                         : std::optional<QString>(root);
}

std::optional<QString> verifiedSandboxRoot(const RemminaInstance &instance)
{
    QString expectedRootSuffix;
    QList<std::pair<QString, QString>> roots;
    if (instance.kind == InstanceKind::Flatpak) {
        expectedRootSuffix = QString::fromLatin1(flatpakApplicationRoot);
        roots = {
            {instance.profiles.configHome, QStringLiteral("/config")},
            {instance.profiles.dataHome, QStringLiteral("/data")},
            {instance.profiles.legacyHome, QStringLiteral("/.remmina")},
        };
    } else if (instance.kind == InstanceKind::Snap) {
        expectedRootSuffix = QString::fromLatin1(snapApplicationRoot);
        roots = {
            {instance.profiles.configHome, QStringLiteral("/.config")},
            {instance.profiles.dataHome, QStringLiteral("/.local/share")},
            {instance.profiles.legacyHome, QStringLiteral("/.remmina")},
        };
    } else {
        return std::nullopt;
    }

    std::optional<QString> commonRoot;
    for (const auto &[root, suffix] : roots) {
        const std::optional<QString> candidateRoot = rootBeforeSuffix(root, suffix);
        if (!candidateRoot.has_value()
            || !candidateRoot->endsWith(expectedRootSuffix, Qt::CaseSensitive)
            || (commonRoot.has_value() && *commonRoot != *candidateRoot)) {
            return std::nullopt;
        }
        commonRoot = candidateRoot;
    }
    return commonRoot;
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

std::optional<LocatedProfileDirectory> sandboxCustomLocation(
    const QString &customPath, const std::optional<QString> &sandboxRoot, bool &sawUnreadable)
{
    const QString absoluteCustomPath = cleanAbsolutePath(customPath);
    if (!sandboxRoot.has_value() || !pathBelow(absoluteCustomPath, *sandboxRoot).has_value()) {
        return std::nullopt;
    }

    const std::optional<QString> canonicalRoot = resolvedPathForContainment(*sandboxRoot);
    const std::optional<QString> canonicalCustom =
        resolvedPathForContainment(absoluteCustomPath);
    if (!canonicalRoot.has_value() || !canonicalCustom.has_value()
        || !pathBelow(*canonicalCustom, *canonicalRoot).has_value()) {
        return std::nullopt;
    }
    return existingLocation(absoluteCustomPath, absoluteCustomPath, sawUnreadable);
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

ProfileLocationResult profile_locator_detail::locateProfileDirectoryDetailed(
    const RemminaInstance &instance)
{
    bool sawUnreadable = false;
    const std::optional<QString> sandboxRoot = verifiedSandboxRoot(instance);

    if (const std::optional<QString> customPath = customDataDirectory(instance)) {
        std::optional<LocatedProfileDirectory> custom;
        if (instance.kind == InstanceKind::Native) {
            custom = existingLocation(*customPath, *customPath, sawUnreadable);
        } else {
            custom = sandboxCustomLocation(*customPath, sandboxRoot, sawUnreadable);
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
        std::optional<LocatedProfileDirectory> location =
            existingLocation(hostCandidate, hostCandidate, sawUnreadable);
        if (location.has_value()) {
            return std::move(*location);
        }
        if (sawUnreadable) {
            return ProfileLocationError::Unreadable;
        }
    }
    return ProfileLocationError::NotFound;
}

std::optional<LocatedProfileDirectory> locateProfileDirectory(const RemminaInstance &instance)
{
    ProfileLocationResult result =
        profile_locator_detail::locateProfileDirectoryDetailed(instance);
    if (auto *location = std::get_if<LocatedProfileDirectory>(&result)) {
        return std::move(*location);
    }
    return std::nullopt;
}
