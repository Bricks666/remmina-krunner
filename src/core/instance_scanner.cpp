// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include "core/instance_scanner.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>
#include <utility>

namespace {

constexpr auto remminaApplicationId = "org.remmina.Remmina";

QString cleanPath(const QString &path)
{
    return path.isEmpty() ? QString{} : QDir::cleanPath(path);
}

QString childPath(const QString &parent, const QString &child)
{
    return cleanPath(QDir(parent).filePath(child));
}

bool isExecutableFile(const QFileInfo &fileInfo)
{
    return fileInfo.exists() && fileInfo.isFile() && fileInfo.isExecutable();
}

QString canonicalOrAbsolutePath(const QString &path)
{
    if (path.isEmpty()) {
        return {};
    }
    const QFileInfo fileInfo(path);
    const QString canonicalPath = fileInfo.canonicalFilePath();
    return canonicalPath.isEmpty() ? cleanPath(fileInfo.absoluteFilePath())
                                   : cleanPath(canonicalPath);
}

bool isUnderSnapRoot(const QString &canonicalPath, const QString &snapMountRoot)
{
    if (snapMountRoot.isEmpty()) {
        return false;
    }
    if (snapMountRoot == QStringLiteral("/")) {
        return canonicalPath.startsWith(u'/');
    }
    return canonicalPath == snapMountRoot
        || canonicalPath.startsWith(snapMountRoot + QLatin1Char('/'));
}

void addFailedBackend(QStringList &failedBackends, const QString &backend)
{
    if (!failedBackends.contains(backend)) {
        failedBackends.append(backend);
    }
}

ProfileEnvironment nativeProfiles(const QString &userHome)
{
    const QString dataHome =
        cleanPath(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
    QStringList systemDataHomes;
    QSet<QString> seen;
    for (const QString &location :
         QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) {
        const QString cleaned = cleanPath(location);
        if (cleaned.isEmpty() || cleaned == dataHome || seen.contains(cleaned)) {
            continue;
        }
        seen.insert(cleaned);
        systemDataHomes.append(cleaned);
    }

    return {
        .configHome =
            cleanPath(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)),
        .dataHome = dataHome,
        .legacyHome = childPath(userHome, QStringLiteral(".remmina")),
        .systemDataHomes = std::move(systemDataHomes),
    };
}

ProfileEnvironment flatpakProfiles(const QString &userHome)
{
    const QString base = childPath(userHome, QStringLiteral(".var/app/org.remmina.Remmina"));
    return {
        .configHome = childPath(base, QStringLiteral("config")),
        .dataHome = childPath(base, QStringLiteral("data")),
        .legacyHome = childPath(base, QStringLiteral(".remmina")),
        .systemDataHomes = {},
    };
}

ProfileEnvironment snapProfiles(const QString &userHome)
{
    const QString base = childPath(userHome, QStringLiteral("snap/remmina/current"));
    return {
        .configHome = childPath(base, QStringLiteral(".config")),
        .dataHome = childPath(base, QStringLiteral(".local/share")),
        .legacyHome = childPath(base, QStringLiteral(".remmina")),
        .systemDataHomes = {},
    };
}

struct FlatpakInstallation {
    QString installation;
    QString ref;
};

bool isRemminaApplicationRef(const QString &ref)
{
    const QStringList components = ref.split(u'/', Qt::KeepEmptyParts);
    const bool allComponentsPresent =
        std::all_of(components.cbegin(), components.cend(), [](const QString &component) {
            return !component.isEmpty();
        });
    if (!allComponentsPresent) {
        return false;
    }
    if (components.size() == 3) {
        return components.constFirst() == QLatin1StringView(remminaApplicationId);
    }
    return components.size() == 4 && components.at(0) == QStringLiteral("app")
        && components.at(1) == QLatin1StringView(remminaApplicationId);
}

int installationGroup(const QString &installation)
{
    if (installation == QStringLiteral("user")) {
        return 0;
    }
    if (installation == QStringLiteral("system")) {
        return 1;
    }
    return 2;
}

QList<FlatpakInstallation> parseFlatpakInstallations(const QByteArray &output)
{
    QList<FlatpakInstallation> installations;
    QSet<QString> seen;
    for (const QByteArray &line : output.split('\n')) {
        const QList<QByteArray> columns = line.split('\t');
        if (columns.size() != 3) {
            continue;
        }

        const QString application = QString::fromUtf8(columns.at(0)).trimmed();
        const QString ref = QString::fromUtf8(columns.at(1)).trimmed();
        const QString installation = QString::fromUtf8(columns.at(2)).trimmed();
        if (application != QLatin1StringView(remminaApplicationId) || ref.isEmpty()
            || installation.isEmpty() || !isRemminaApplicationRef(ref)) {
            continue;
        }

        const QString deduplicationKey = installation + QChar(0x1f) + ref;
        if (seen.contains(deduplicationKey)) {
            continue;
        }
        seen.insert(deduplicationKey);
        installations.append({.installation = installation, .ref = ref});
    }

    std::sort(installations.begin(),
              installations.end(),
              [](const FlatpakInstallation &left, const FlatpakInstallation &right) {
                  const int leftGroup = installationGroup(left.installation);
                  const int rightGroup = installationGroup(right.installation);
                  if (leftGroup != rightGroup) {
                      return leftGroup < rightGroup;
                  }
                  const int installationOrder =
                      QString::compare(left.installation, right.installation, Qt::CaseSensitive);
                  if (installationOrder != 0) {
                      return installationOrder < 0;
                  }
                  return QString::compare(left.ref, right.ref, Qt::CaseSensitive) < 0;
              });
    return installations;
}

QStringList flatpakLauncherPrefix(const FlatpakInstallation &installation)
{
    QString selector;
    if (installation.installation == QStringLiteral("user")) {
        selector = QStringLiteral("--user");
    } else if (installation.installation == QStringLiteral("system")) {
        selector = QStringLiteral("--system");
    } else {
        selector = QStringLiteral("--installation=") + installation.installation;
    }
    return {selector, QStringLiteral("run"), installation.ref};
}

} // namespace

InstanceScanner::InstanceScanner(ProcessProbe &probe, ScanEnvironment environment)
    : probe_(probe)
    , environment_(std::move(environment))
{
}

InstanceScanResult InstanceScanner::scan() const
{
    InstanceScanResult result;

    const QFileInfo snapLauncherInfo(environment_.snapLauncher);
    const QString snapLauncherPath = environment_.snapLauncher.isEmpty()
        ? QString{}
        : cleanPath(snapLauncherInfo.absoluteFilePath());
    const QString snapLauncherCanonical = snapLauncherInfo.canonicalFilePath();
    const QString snapMountRoot = canonicalOrAbsolutePath(environment_.snapMountRoot);

    const ProfileEnvironment hostProfiles = nativeProfiles(environment_.userHome);
    QSet<QString> nativeCanonicalPaths;
    for (const QString &pathEntry : environment_.pathEntries) {
        const QString candidatePath = QDir(pathEntry).filePath(QStringLiteral("remmina"));
        const QFileInfo candidate(candidatePath);
        if (!isExecutableFile(candidate)) {
            continue;
        }
        const QString canonicalPath = candidate.canonicalFilePath();
        if (canonicalPath.isEmpty()) {
            continue;
        }
        const QString absolutePath = cleanPath(candidate.absoluteFilePath());
        if ((!snapLauncherPath.isEmpty() && absolutePath == snapLauncherPath)
            || (!snapLauncherCanonical.isEmpty() && canonicalPath == snapLauncherCanonical)
            || isUnderSnapRoot(canonicalPath, snapMountRoot)
            || nativeCanonicalPaths.contains(canonicalPath)) {
            continue;
        }

        nativeCanonicalPaths.insert(canonicalPath);
        result.instances.append({
            .id = QStringLiteral("native:") + canonicalPath,
            .kind = InstanceKind::Native,
            .displayName = QStringLiteral("Remmina (Native: %1)").arg(canonicalPath),
            .executable = canonicalPath,
            .launcherPrefix = {},
            .profiles = hostProfiles,
        });
    }

    const QFileInfo flatpakInfo(environment_.flatpakExecutable);
    if (isExecutableFile(flatpakInfo)) {
        const QString flatpakCanonical = flatpakInfo.canonicalFilePath();
        if (!flatpakCanonical.isEmpty()) {
            const ProbeResult probeResult = probe_.run(
                flatpakCanonical,
                {QStringLiteral("list"),
                 QStringLiteral("--app"),
                 QStringLiteral("--columns=application,ref,installation")});
            if (probeResult.status != ProbeResult::Status::Success) {
                addFailedBackend(result.failedBackends, QStringLiteral("flatpak"));
            } else {
                const ProfileEnvironment profiles = flatpakProfiles(environment_.userHome);
                for (const FlatpakInstallation &installation :
                     parseFlatpakInstallations(probeResult.standardOutput)) {
                    result.instances.append({
                        .id = QStringLiteral("flatpak:") + installation.installation
                            + QLatin1Char(':') + installation.ref,
                        .kind = InstanceKind::Flatpak,
                        .displayName = QStringLiteral("Remmina (Flatpak: %1)")
                                           .arg(installation.installation),
                        .executable = flatpakCanonical,
                        .launcherPrefix = flatpakLauncherPrefix(installation),
                        .profiles = profiles,
                    });
                }
            }
        }
    }

    if (!environment_.snapLauncher.isEmpty()) {
        if (!snapLauncherInfo.exists()) {
            if (snapLauncherInfo.isSymLink()) {
                addFailedBackend(result.failedBackends, QStringLiteral("snap"));
            }
        } else if (isExecutableFile(snapLauncherInfo)) {
            if (snapLauncherCanonical.isEmpty()) {
                addFailedBackend(result.failedBackends, QStringLiteral("snap"));
            } else {
                result.instances.append({
                    .id = QStringLiteral("snap:remmina"),
                    .kind = InstanceKind::Snap,
                    .displayName = QStringLiteral("Remmina (Snap)"),
                    .executable = snapLauncherPath,
                    .launcherPrefix = {},
                    .profiles = snapProfiles(environment_.userHome),
                });
            }
        }
    }

    return result;
}
