// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include <QtTest>

#include "core/instance_scanner.h"
#include "fakes.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <array>

namespace {

QString createExecutable(const QString &directory, const QString &name = QStringLiteral("remmina"))
{
    if (!QDir{}.mkpath(directory)) {
        return {};
    }

    const QString path = QDir(directory).filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return {};
    }
    if (file.write("synthetic executable\n") < 0) {
        return {};
    }
    file.close();

    const QFileDevice::Permissions permissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner
        | QFileDevice::ExeOwner | QFileDevice::ReadGroup | QFileDevice::ExeGroup
        | QFileDevice::ReadOther | QFileDevice::ExeOther;
    if (!QFile::setPermissions(path, permissions)) {
        return {};
    }
    return path;
}

QList<RemminaInstance> instancesOfKind(const InstanceScanResult &result, InstanceKind kind)
{
    QList<RemminaInstance> matching;
    for (const RemminaInstance &instance : result.instances) {
        if (instance.kind == kind) {
            matching.append(instance);
        }
    }
    return matching;
}

ScanEnvironment nativeEnvironment(QStringList pathEntries)
{
    return {
        .pathEntries = std::move(pathEntries),
        .flatpakExecutable = {},
        .snapLauncher = {},
        .userHome = QDir::homePath(),
    };
}

QStringList flatpakListArguments()
{
    return {
        QStringLiteral("list"),
        QStringLiteral("--app"),
        QStringLiteral("--columns=application,ref,installation"),
    };
}

QString cleanedChildPath(const QString &parent, const QString &child)
{
    return QDir::cleanPath(QDir(parent).filePath(child));
}

} // namespace

class InstanceScannerTest : public QObject {
    Q_OBJECT

private slots:
    void discoversNativeExecutablesInPathOrderWithSpaces();
    void rejectsInvalidNativeCandidates();
    void deduplicatesNativeCanonicalPaths();
    void keepsNativeLauncherIdentityAcrossSymlinkRetarget();
    void excludesSnapLauncherAliasesWithoutSubstringGuessing();
    void excludesDistinctNativeAliasToSnapCanonicalTarget();
    void excludesOnlyExecutablesUnderSnapMountRoot();
    void discoversFlatpakInstallationsInDeterministicOrder();
    void filtersMalformedAndDuplicateFlatpakRows();
    void rejectsUnsafeFlatpakFields();
    void treatsMissingFlatpakAsUnavailable();
    void isolatesEveryFlatpakProbeFailure();
    void discoversSnapThroughRefreshStableLauncher();
    void treatsMissingOrNonExecutableSnapAsUnavailable();
    void isolatesSnapResolutionFailureAndOrdersBackendFailures();
    void buildsPackagingSpecificProfileEnvironmentsAndOverallOrder();
};

void InstanceScannerTest::discoversNativeExecutablesInPathOrderWithSpaces()
{
    QTemporaryDir temporaryDirectory(QDir::tempPath() + QStringLiteral("/native path with spaces-XXXXXX"));
    QVERIFY(temporaryDirectory.isValid());
    const QString secondDirectory = QDir(temporaryDirectory.path()).filePath(QStringLiteral("second"));
    const QString firstExecutable = createExecutable(temporaryDirectory.path());
    const QString secondExecutable = createExecutable(secondDirectory);
    QVERIFY(!firstExecutable.isEmpty());
    QVERIFY(!secondExecutable.isEmpty());

    RecordingProcessProbe probe;
    InstanceScanner scanner(
        probe, nativeEnvironment({temporaryDirectory.path(), secondDirectory}));

    const InstanceScanResult result = scanner.scan();
    const QList<RemminaInstance> nativeInstances = instancesOfKind(result, InstanceKind::Native);

    QCOMPARE(nativeInstances.size(), 2);
    QCOMPARE(nativeInstances.at(0).executable, QFileInfo(firstExecutable).canonicalFilePath());
    QCOMPARE(nativeInstances.at(1).executable, QFileInfo(secondExecutable).canonicalFilePath());
    QCOMPARE(nativeInstances.at(0).id,
             QStringLiteral("native:") + QFileInfo(firstExecutable).canonicalFilePath());
    QVERIFY(nativeInstances.at(0).launcherPrefix.isEmpty());
    QVERIFY(result.failedBackends.isEmpty());
    QVERIFY(probe.calls.isEmpty());
}

void InstanceScannerTest::rejectsInvalidNativeCandidates()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QDir root(temporaryDirectory.path());

    const QString nonexistentDirectory = root.filePath(QStringLiteral("missing"));
    const QString directoryCandidate = root.filePath(QStringLiteral("directory/remmina"));
    QVERIFY(QDir{}.mkpath(directoryCandidate));

    const QString nonExecutableDirectory = root.filePath(QStringLiteral("non-executable"));
    QVERIFY(QDir{}.mkpath(nonExecutableDirectory));
    QFile nonExecutable(QDir(nonExecutableDirectory).filePath(QStringLiteral("remmina")));
    QVERIFY(nonExecutable.open(QIODevice::WriteOnly));
    QCOMPARE(nonExecutable.write("not executable\n"), qint64(15));
    nonExecutable.close();
    QVERIFY(QFile::setPermissions(nonExecutable.fileName(),
                                  QFileDevice::ReadOwner | QFileDevice::WriteOwner));

    const QString brokenLinkDirectory = root.filePath(QStringLiteral("broken-link"));
    QVERIFY(QDir{}.mkpath(brokenLinkDirectory));
    QVERIFY(QFile::link(root.filePath(QStringLiteral("absent-target")),
                        QDir(brokenLinkDirectory).filePath(QStringLiteral("remmina"))));

    const QString validDirectory = root.filePath(QStringLiteral("valid"));
    const QString validExecutable = createExecutable(validDirectory);
    QVERIFY(!validExecutable.isEmpty());

    RecordingProcessProbe probe;
    InstanceScanner scanner(probe,
                            nativeEnvironment({nonexistentDirectory,
                                               QFileInfo(directoryCandidate).absolutePath(),
                                               nonExecutableDirectory,
                                               brokenLinkDirectory,
                                               validDirectory}));

    const QList<RemminaInstance> nativeInstances =
        instancesOfKind(scanner.scan(), InstanceKind::Native);

    QCOMPARE(nativeInstances.size(), 1);
    QCOMPARE(nativeInstances.constFirst().executable,
             QFileInfo(validExecutable).canonicalFilePath());
}

void InstanceScannerTest::deduplicatesNativeCanonicalPaths()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QDir root(temporaryDirectory.path());
    const QString realDirectory = root.filePath(QStringLiteral("real"));
    const QString aliasDirectory = root.filePath(QStringLiteral("alias"));
    const QString executable = createExecutable(realDirectory);
    QVERIFY(!executable.isEmpty());
    QVERIFY(QDir{}.mkpath(aliasDirectory));
    QVERIFY(QFile::link(executable, QDir(aliasDirectory).filePath(QStringLiteral("remmina"))));

    RecordingProcessProbe probe;
    InstanceScanner scanner(probe, nativeEnvironment({aliasDirectory, realDirectory}));

    const QList<RemminaInstance> nativeInstances =
        instancesOfKind(scanner.scan(), InstanceKind::Native);

    QCOMPARE(nativeInstances.size(), 1);
    QCOMPARE(nativeInstances.constFirst().executable,
             QDir::cleanPath(QFileInfo(QDir(aliasDirectory).filePath(QStringLiteral("remmina")))
                                 .absoluteFilePath()));
}

void InstanceScannerTest::keepsNativeLauncherIdentityAcrossSymlinkRetarget()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QDir root(temporaryDirectory.path());
    const QString version1 = createExecutable(root.filePath(QStringLiteral("versions/1")));
    const QString version2 = createExecutable(root.filePath(QStringLiteral("versions/2")));
    const QString launcherDirectory = root.filePath(QStringLiteral("bin"));
    const QString launcher = QDir(launcherDirectory).filePath(QStringLiteral("remmina"));
    QVERIFY(!version1.isEmpty());
    QVERIFY(!version2.isEmpty());
    QVERIFY(QDir{}.mkpath(launcherDirectory));
    QVERIFY(QFile::link(version1, launcher));
    const QString version1Canonical = QFileInfo(launcher).canonicalFilePath();

    RecordingProcessProbe firstProbe;
    const QList<RemminaInstance> firstInstances = instancesOfKind(
        InstanceScanner(firstProbe, nativeEnvironment({launcherDirectory})).scan(),
        InstanceKind::Native);
    QCOMPARE(firstInstances.size(), 1);
    const QString lexicalLauncher = QDir::cleanPath(QFileInfo(launcher).absoluteFilePath());
    QCOMPARE(firstInstances.constFirst().id, QStringLiteral("native:") + lexicalLauncher);
    QCOMPARE(firstInstances.constFirst().executable, lexicalLauncher);

    QVERIFY(QFile::remove(launcher));
    QVERIFY(QFile::link(version2, launcher));
    QVERIFY(QFileInfo(launcher).canonicalFilePath() != version1Canonical);
    RecordingProcessProbe refreshedProbe;
    const QList<RemminaInstance> refreshedInstances = instancesOfKind(
        InstanceScanner(refreshedProbe, nativeEnvironment({launcherDirectory})).scan(),
        InstanceKind::Native);

    QCOMPARE(refreshedInstances.size(), 1);
    QCOMPARE(refreshedInstances.constFirst().id, firstInstances.constFirst().id);
    QCOMPARE(refreshedInstances.constFirst().executable,
             firstInstances.constFirst().executable);
}

void InstanceScannerTest::excludesSnapLauncherAliasesWithoutSubstringGuessing()
{
    QTemporaryDir temporaryDirectory(QDir::tempPath() + QStringLiteral("/contains-snap-word-XXXXXX"));
    QVERIFY(temporaryDirectory.isValid());
    const QDir root(temporaryDirectory.path());
    const QString snapTarget = createExecutable(root.filePath(QStringLiteral("snap-target")),
                                                QStringLiteral("snap-command"));
    const QString snapAliasDirectory = root.filePath(QStringLiteral("snap-alias"));
    QVERIFY(!snapTarget.isEmpty());
    QVERIFY(QDir{}.mkpath(snapAliasDirectory));
    const QString snapAlias = QDir(snapAliasDirectory).filePath(QStringLiteral("remmina"));
    QVERIFY(QFile::link(snapTarget, snapAlias));

    const QString unrelatedDirectory = root.filePath(QStringLiteral("ordinary-snap-name"));
    const QString unrelatedExecutable = createExecutable(unrelatedDirectory);
    QVERIFY(!unrelatedExecutable.isEmpty());

    RecordingProcessProbe probe;
    ScanEnvironment environment = nativeEnvironment({snapAliasDirectory, unrelatedDirectory});
    environment.snapLauncher = snapAlias;
    InstanceScanner scanner(probe, std::move(environment));

    const InstanceScanResult result = scanner.scan();
    const QList<RemminaInstance> nativeInstances = instancesOfKind(result, InstanceKind::Native);

    QCOMPARE(nativeInstances.size(), 1);
    QCOMPARE(nativeInstances.constFirst().executable,
             QFileInfo(unrelatedExecutable).canonicalFilePath());
}

void InstanceScannerTest::excludesDistinctNativeAliasToSnapCanonicalTarget()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QDir root(temporaryDirectory.path());
    const QString sharedTarget = createExecutable(root.filePath(QStringLiteral("shared")),
                                                  QStringLiteral("snap-command"));
    const QString snapLauncherDirectory = root.filePath(QStringLiteral("snap-launcher"));
    const QString nativeAliasDirectory = root.filePath(QStringLiteral("native-alias"));
    const QString unrelatedDirectory = root.filePath(QStringLiteral("unrelated"));
    QVERIFY(!sharedTarget.isEmpty());
    QVERIFY(QDir{}.mkpath(snapLauncherDirectory));
    QVERIFY(QDir{}.mkpath(nativeAliasDirectory));

    const QString snapLauncher =
        QDir(snapLauncherDirectory).filePath(QStringLiteral("remmina"));
    const QString nativeAlias =
        QDir(nativeAliasDirectory).filePath(QStringLiteral("remmina"));
    QVERIFY(snapLauncher != nativeAlias);
    QVERIFY(QFile::link(sharedTarget, snapLauncher));
    QVERIFY(QFile::link(sharedTarget, nativeAlias));
    QCOMPARE(QFileInfo(snapLauncher).canonicalFilePath(),
             QFileInfo(nativeAlias).canonicalFilePath());
    const QString unrelatedExecutable = createExecutable(unrelatedDirectory);
    QVERIFY(!unrelatedExecutable.isEmpty());

    RecordingProcessProbe probe;
    ScanEnvironment environment =
        nativeEnvironment({nativeAliasDirectory, unrelatedDirectory});
    environment.snapLauncher = snapLauncher;

    const InstanceScanResult result = InstanceScanner(probe, std::move(environment)).scan();
    const QList<RemminaInstance> nativeInstances = instancesOfKind(result, InstanceKind::Native);

    QCOMPARE(nativeInstances.size(), 1);
    QCOMPARE(nativeInstances.constFirst().executable,
             QFileInfo(unrelatedExecutable).canonicalFilePath());
}

void InstanceScannerTest::excludesOnlyExecutablesUnderSnapMountRoot()
{
    QCOMPARE(ScanEnvironment{}.snapMountRoot, QStringLiteral("/snap"));

    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QDir root(temporaryDirectory.path());
    const QString snapMountRoot = root.filePath(QStringLiteral("snap"));
    const QString underSnapDirectory =
        QDir(snapMountRoot).filePath(QStringLiteral("remmina/revision/bin"));
    const QString prefixSiblingDirectory = root.filePath(QStringLiteral("snapshots"));
    const QString containsSnapDirectory = root.filePath(QStringLiteral("contains-snap-word"));
    const QString underSnapExecutable = createExecutable(underSnapDirectory);
    const QString prefixSiblingExecutable = createExecutable(prefixSiblingDirectory);
    const QString containsSnapExecutable = createExecutable(containsSnapDirectory);
    QVERIFY(!underSnapExecutable.isEmpty());
    QVERIFY(!prefixSiblingExecutable.isEmpty());
    QVERIFY(!containsSnapExecutable.isEmpty());

    RecordingProcessProbe probe;
    ScanEnvironment environment = nativeEnvironment(
        {underSnapDirectory, prefixSiblingDirectory, containsSnapDirectory});
    environment.snapMountRoot = snapMountRoot;

    const InstanceScanResult result = InstanceScanner(probe, std::move(environment)).scan();
    const QList<RemminaInstance> nativeInstances = instancesOfKind(result, InstanceKind::Native);

    QCOMPARE(nativeInstances.size(), 2);
    QCOMPARE(nativeInstances.at(0).executable,
             QFileInfo(prefixSiblingExecutable).canonicalFilePath());
    QCOMPARE(nativeInstances.at(1).executable,
             QFileInfo(containsSnapExecutable).canonicalFilePath());
}

void InstanceScannerTest::discoversFlatpakInstallationsInDeterministicOrder()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString flatpakTarget = createExecutable(temporaryDirectory.path(),
                                                   QStringLiteral("flatpak-real"));
    const QString flatpakAlias =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("flatpak launcher"));
    QVERIFY(!flatpakTarget.isEmpty());
    QVERIFY(QFile::link(flatpakTarget, flatpakAlias));

    const QByteArray output =
        "org.remmina.Remmina\torg.remmina.Remmina/x86_64/stable\tzeta\n"
        "org.remmina.Remmina\torg.remmina.Remmina/x86_64/beta\tuser\n"
        "org.remmina.Remmina\torg.remmina.Remmina/x86_64/stable\talpha\n"
        "org.remmina.Remmina\torg.remmina.Remmina/x86_64/stable\tuser\n"
        "org.remmina.Remmina\torg.remmina.Remmina/x86_64/stable\tsystem\n";
    RecordingProcessProbe probe;
    probe.expect(QFileInfo(flatpakTarget).canonicalFilePath(),
                 flatpakListArguments(),
                 {.status = ProbeResult::Status::Success, .standardOutput = output});
    ScanEnvironment environment = nativeEnvironment({});
    environment.flatpakExecutable = flatpakAlias;
    InstanceScanner scanner(probe, std::move(environment));

    const InstanceScanResult result = scanner.scan();
    const QList<RemminaInstance> flatpakInstances =
        instancesOfKind(result, InstanceKind::Flatpak);

    QCOMPARE(flatpakInstances.size(), 5);
    const QStringList expectedIds{
        QStringLiteral("flatpak:user:org.remmina.Remmina/x86_64/beta"),
        QStringLiteral("flatpak:user:org.remmina.Remmina/x86_64/stable"),
        QStringLiteral("flatpak:system:org.remmina.Remmina/x86_64/stable"),
        QStringLiteral("flatpak:alpha:org.remmina.Remmina/x86_64/stable"),
        QStringLiteral("flatpak:zeta:org.remmina.Remmina/x86_64/stable"),
    };
    QStringList actualIds;
    for (const RemminaInstance &instance : flatpakInstances) {
        actualIds.append(instance.id);
        QCOMPARE(instance.executable, QFileInfo(flatpakTarget).canonicalFilePath());
    }
    QCOMPARE(actualIds, expectedIds);
    QCOMPARE(flatpakInstances.at(0).launcherPrefix,
             QStringList({QStringLiteral("--user"),
                          QStringLiteral("run"),
                          QStringLiteral("org.remmina.Remmina/x86_64/beta")}));
    QCOMPARE(flatpakInstances.at(2).launcherPrefix,
             QStringList({QStringLiteral("--system"),
                          QStringLiteral("run"),
                          QStringLiteral("org.remmina.Remmina/x86_64/stable")}));
    QCOMPARE(flatpakInstances.at(3).launcherPrefix,
             QStringList({QStringLiteral("--installation=alpha"),
                          QStringLiteral("run"),
                          QStringLiteral("org.remmina.Remmina/x86_64/stable")}));
    QVERIFY(probe.expectationsMet());
    QVERIFY(result.failedBackends.isEmpty());
}

void InstanceScannerTest::filtersMalformedAndDuplicateFlatpakRows()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString flatpakExecutable =
        createExecutable(temporaryDirectory.path(), QStringLiteral("flatpak"));
    QVERIFY(!flatpakExecutable.isEmpty());
    const QByteArray output =
        "\n"
        "org.other.Application\torg.other.Application/x86_64/stable\tuser\n"
        "org.remmina.Remmina.beta\torg.remmina.Remmina.beta/x86_64/stable\tuser\n"
        "org.remmina.Remmina\t\tuser\n"
        "org.remmina.Remmina\torg.remmina.Remmina/x86_64/stable\t\n"
        "org.remmina.Remmina\torg.other.Application/x86_64/stable\tuser\n"
        "org.remmina.Remmina without tabular columns\n"
        "org.remmina.Remmina\torg.remmina.Remmina/x86_64/stable\tuser\textra\n"
        "org.remmina.Remmina\torg.remmina.Remmina/x86_64/stable\tuser\n"
        "org.remmina.Remmina\torg.remmina.Remmina/x86_64/stable\tuser\n";
    RecordingProcessProbe probe;
    probe.expect(QFileInfo(flatpakExecutable).canonicalFilePath(),
                 flatpakListArguments(),
                 {.status = ProbeResult::Status::Success, .standardOutput = output});
    ScanEnvironment environment = nativeEnvironment({});
    environment.flatpakExecutable = flatpakExecutable;
    InstanceScanner scanner(probe, std::move(environment));

    const QList<RemminaInstance> flatpakInstances =
        instancesOfKind(scanner.scan(), InstanceKind::Flatpak);

    QCOMPARE(flatpakInstances.size(), 1);
    QCOMPARE(flatpakInstances.constFirst().id,
             QStringLiteral("flatpak:user:org.remmina.Remmina/x86_64/stable"));
    QVERIFY(probe.expectationsMet());
}

void InstanceScannerTest::rejectsUnsafeFlatpakFields()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString flatpakExecutable =
        createExecutable(temporaryDirectory.path(), QStringLiteral("flatpak"));
    QVERIFY(!flatpakExecutable.isEmpty());

    QByteArray output(
        "org.remmina.Remmina\torg.remmina.Remmina/x86_64/stable\tuser\n");
    output.append("org.remmina.Remmina\torg.remmina.Remmina/x86_64/stable\tbad");
    output.append(char(0xff));
    output.append('\n');
    output.append("org.remmina.Remmina\torg.remmina.Remmina/x86_64/stable");
    output.append(char(0));
    output.append("suffix\tuser\n");
    output.append("org.remmina.Remmina\torg.remmina.Remmina/x86_64/stable\tbad");
    output.append(char(0x01));
    output.append("control\n");

    RecordingProcessProbe probe;
    probe.expect(QFileInfo(flatpakExecutable).canonicalFilePath(),
                 flatpakListArguments(),
                 {.status = ProbeResult::Status::Success, .standardOutput = output});
    ScanEnvironment environment = nativeEnvironment({});
    environment.flatpakExecutable = flatpakExecutable;

    const QList<RemminaInstance> flatpakInstances = instancesOfKind(
        InstanceScanner(probe, std::move(environment)).scan(), InstanceKind::Flatpak);

    QCOMPARE(flatpakInstances.size(), 1);
    QCOMPARE(flatpakInstances.constFirst().id,
             QStringLiteral("flatpak:user:org.remmina.Remmina/x86_64/stable"));
    QVERIFY(probe.expectationsMet());
}

void InstanceScannerTest::treatsMissingFlatpakAsUnavailable()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString nonExecutable =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("flatpak"));
    QFile file(nonExecutable);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    QVERIFY(QFile::setPermissions(nonExecutable,
                                  QFileDevice::ReadOwner | QFileDevice::WriteOwner));

    const QStringList unavailablePaths{
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("missing-flatpak")),
        nonExecutable,
    };
    for (const QString &path : unavailablePaths) {
        RecordingProcessProbe probe;
        ScanEnvironment environment = nativeEnvironment({});
        environment.flatpakExecutable = path;
        const InstanceScanResult result = InstanceScanner(probe, std::move(environment)).scan();
        QVERIFY(result.instances.isEmpty());
        QVERIFY(result.failedBackends.isEmpty());
        QVERIFY(probe.calls.isEmpty());
    }
}

void InstanceScannerTest::isolatesEveryFlatpakProbeFailure()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QDir root(temporaryDirectory.path());
    const QString nativeDirectory = root.filePath(QStringLiteral("native"));
    const QString nativeExecutable = createExecutable(nativeDirectory);
    const QString flatpakExecutable = createExecutable(root.filePath(QStringLiteral("tools")),
                                                       QStringLiteral("flatpak"));
    const QString snapLauncher = createExecutable(root.filePath(QStringLiteral("snap-bin")));
    QVERIFY(!nativeExecutable.isEmpty());
    QVERIFY(!flatpakExecutable.isEmpty());
    QVERIFY(!snapLauncher.isEmpty());

    constexpr std::array failureStatuses{
        ProbeResult::Status::Failed,
        ProbeResult::Status::TimedOut,
        ProbeResult::Status::OutputTooLarge,
    };
    for (const ProbeResult::Status status : failureStatuses) {
        RecordingProcessProbe probe;
        probe.expect(QFileInfo(flatpakExecutable).canonicalFilePath(),
                     flatpakListArguments(),
                     {.status = status, .standardOutput = QByteArray("ignored private output")});
        ScanEnvironment environment = nativeEnvironment({nativeDirectory});
        environment.flatpakExecutable = flatpakExecutable;
        environment.snapLauncher = snapLauncher;

        const InstanceScanResult result = InstanceScanner(probe, std::move(environment)).scan();

        QCOMPARE(result.instances.size(), 2);
        QCOMPARE(result.instances.at(0).kind, InstanceKind::Native);
        QCOMPARE(result.instances.at(1).kind, InstanceKind::Snap);
        QCOMPARE(result.failedBackends, QStringList({QStringLiteral("flatpak")}));
        QVERIFY(probe.expectationsMet());
    }
}

void InstanceScannerTest::discoversSnapThroughRefreshStableLauncher()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QDir root(temporaryDirectory.path());
    const QString revision101 = createExecutable(root.filePath(QStringLiteral("revision-101")));
    const QString stableDirectory = root.filePath(QStringLiteral("snap-bin"));
    const QString stableLauncher = QDir(stableDirectory).filePath(QStringLiteral("remmina"));
    QVERIFY(!revision101.isEmpty());
    QVERIFY(QDir{}.mkpath(stableDirectory));
    QVERIFY(QFile::link(revision101, stableLauncher));

    RecordingProcessProbe firstProbe;
    ScanEnvironment firstEnvironment = nativeEnvironment({stableDirectory});
    firstEnvironment.snapLauncher = stableLauncher;
    firstEnvironment.userHome = root.filePath(QStringLiteral("home"));
    const InstanceScanResult firstResult =
        InstanceScanner(firstProbe, std::move(firstEnvironment)).scan();
    const QList<RemminaInstance> firstSnap = instancesOfKind(firstResult, InstanceKind::Snap);

    QCOMPARE(firstSnap.size(), 1);
    QCOMPARE(firstSnap.constFirst().id, QStringLiteral("snap:remmina"));
    QCOMPARE(firstSnap.constFirst().executable,
             QDir::cleanPath(QFileInfo(stableLauncher).absoluteFilePath()));
    QVERIFY(firstSnap.constFirst().launcherPrefix.isEmpty());
    QVERIFY(instancesOfKind(firstResult, InstanceKind::Native).isEmpty());

    const QString revision9999 = createExecutable(root.filePath(QStringLiteral("revision-9999")));
    QVERIFY(!revision9999.isEmpty());
    QVERIFY(QFile::remove(stableLauncher));
    QVERIFY(QFile::link(revision9999, stableLauncher));
    RecordingProcessProbe refreshedProbe;
    ScanEnvironment refreshedEnvironment = nativeEnvironment({stableDirectory});
    refreshedEnvironment.snapLauncher = stableLauncher;
    const InstanceScanResult refreshedResult =
        InstanceScanner(refreshedProbe, std::move(refreshedEnvironment)).scan();

    QCOMPARE(instancesOfKind(refreshedResult, InstanceKind::Snap).constFirst().id,
             firstSnap.constFirst().id);
    QVERIFY(!firstSnap.constFirst().id.contains(QStringLiteral("101")));
    QVERIFY(!refreshedResult.instances.constFirst().id.contains(QStringLiteral("9999")));
}

void InstanceScannerTest::treatsMissingOrNonExecutableSnapAsUnavailable()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString nonExecutable =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("remmina"));
    QFile file(nonExecutable);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    QVERIFY(QFile::setPermissions(nonExecutable,
                                  QFileDevice::ReadOwner | QFileDevice::WriteOwner));

    const QStringList unavailablePaths{
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("missing-remmina")),
        nonExecutable,
    };
    for (const QString &path : unavailablePaths) {
        RecordingProcessProbe probe;
        ScanEnvironment environment = nativeEnvironment({});
        environment.snapLauncher = path;
        const InstanceScanResult result = InstanceScanner(probe, std::move(environment)).scan();
        QVERIFY(result.instances.isEmpty());
        QVERIFY(result.failedBackends.isEmpty());
    }
}

void InstanceScannerTest::isolatesSnapResolutionFailureAndOrdersBackendFailures()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QDir root(temporaryDirectory.path());
    const QString nativeDirectory = root.filePath(QStringLiteral("native"));
    const QString nativeExecutable = createExecutable(nativeDirectory);
    const QString flatpakExecutable = createExecutable(root.filePath(QStringLiteral("tools")),
                                                       QStringLiteral("flatpak"));
    const QString firstLoopLink = root.filePath(QStringLiteral("snap-loop-a"));
    const QString secondLoopLink = root.filePath(QStringLiteral("snap-loop-b"));
    QVERIFY(!nativeExecutable.isEmpty());
    QVERIFY(!flatpakExecutable.isEmpty());
    QVERIFY(QFile::link(secondLoopLink, firstLoopLink));
    QVERIFY(QFile::link(firstLoopLink, secondLoopLink));

    RecordingProcessProbe probe;
    probe.expect(QFileInfo(flatpakExecutable).canonicalFilePath(),
                 flatpakListArguments(),
                 {.status = ProbeResult::Status::Failed, .standardOutput = {}});
    ScanEnvironment environment = nativeEnvironment({nativeDirectory});
    environment.flatpakExecutable = flatpakExecutable;
    environment.snapLauncher = firstLoopLink;

    const InstanceScanResult result = InstanceScanner(probe, std::move(environment)).scan();

    QCOMPARE(result.instances.size(), 1);
    QCOMPARE(result.instances.constFirst().kind, InstanceKind::Native);
    QCOMPARE(result.failedBackends,
             QStringList({QStringLiteral("flatpak"), QStringLiteral("snap")}));
    QCOMPARE(QSet<QString>(result.failedBackends.cbegin(), result.failedBackends.cend()).size(),
             result.failedBackends.size());
    QVERIFY(probe.expectationsMet());
}

void InstanceScannerTest::buildsPackagingSpecificProfileEnvironmentsAndOverallOrder()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QDir root(temporaryDirectory.path());
    const QString nativeDirectory = root.filePath(QStringLiteral("native"));
    const QString nativeExecutable = createExecutable(nativeDirectory);
    const QString flatpakExecutable = createExecutable(root.filePath(QStringLiteral("tools")),
                                                       QStringLiteral("flatpak"));
    const QString snapLauncher = createExecutable(root.filePath(QStringLiteral("snap-bin")));
    QVERIFY(!nativeExecutable.isEmpty());
    QVERIFY(!flatpakExecutable.isEmpty());
    QVERIFY(!snapLauncher.isEmpty());

    RecordingProcessProbe probe;
    probe.expect(QFileInfo(flatpakExecutable).canonicalFilePath(),
                 flatpakListArguments(),
                 {.status = ProbeResult::Status::Success,
                  .standardOutput =
                      QByteArray("org.remmina.Remmina\torg.remmina.Remmina/x86_64/stable\tnamed\n"
                                 "org.remmina.Remmina\torg.remmina.Remmina/x86_64/stable\tuser\n")});
    const QString userHome = root.filePath(QStringLiteral("home/../synthetic-home"));
    ScanEnvironment environment = nativeEnvironment({nativeDirectory});
    environment.flatpakExecutable = flatpakExecutable;
    environment.snapLauncher = snapLauncher;
    environment.userHome = userHome;
    const InstanceScanResult result = InstanceScanner(probe, std::move(environment)).scan();

    QCOMPARE(result.instances.size(), 4);
    QCOMPARE(result.instances.at(0).kind, InstanceKind::Native);
    QCOMPARE(result.instances.at(1).kind, InstanceKind::Flatpak);
    QCOMPARE(result.instances.at(2).kind, InstanceKind::Flatpak);
    QCOMPARE(result.instances.at(3).kind, InstanceKind::Snap);

    const RemminaInstance &native = result.instances.at(0);
    QCOMPARE(native.profiles.configHome,
             QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)));
    QCOMPARE(native.profiles.dataHome,
             QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)));
    QCOMPARE(native.profiles.legacyHome, cleanedChildPath(userHome, QStringLiteral(".remmina")));
    QVERIFY(!native.profiles.systemDataHomes.contains(native.profiles.dataHome));

    const QString flatpakBase =
        cleanedChildPath(userHome, QStringLiteral(".var/app/org.remmina.Remmina"));
    for (qsizetype index = 1; index <= 2; ++index) {
        const ProfileEnvironment &profiles = result.instances.at(index).profiles;
        QCOMPARE(profiles.configHome, cleanedChildPath(flatpakBase, QStringLiteral("config")));
        QCOMPARE(profiles.dataHome, cleanedChildPath(flatpakBase, QStringLiteral("data")));
        QCOMPARE(profiles.legacyHome, cleanedChildPath(flatpakBase, QStringLiteral(".remmina")));
        QVERIFY(profiles.systemDataHomes.isEmpty());
    }

    const QString snapBase = cleanedChildPath(userHome, QStringLiteral("snap/remmina/current"));
    const ProfileEnvironment &snapProfiles = result.instances.at(3).profiles;
    QCOMPARE(snapProfiles.configHome, cleanedChildPath(snapBase, QStringLiteral(".config")));
    QCOMPARE(snapProfiles.dataHome, cleanedChildPath(snapBase, QStringLiteral(".local/share")));
    QCOMPARE(snapProfiles.legacyHome, cleanedChildPath(snapBase, QStringLiteral(".remmina")));
    QVERIFY(snapProfiles.systemDataHomes.isEmpty());

    QSet<QString> ids;
    for (const RemminaInstance &instance : result.instances) {
        QVERIFY(!instance.id.isEmpty());
        QVERIFY(instance.kind == InstanceKind::Native || instance.kind == InstanceKind::Flatpak
                || instance.kind == InstanceKind::Snap);
        QVERIFY(!ids.contains(instance.id));
        ids.insert(instance.id);
    }
    QCOMPARE(ids.size(), result.instances.size());
    QVERIFY(result.failedBackends.isEmpty());
    QVERIFY(probe.expectationsMet());
}

QTEST_APPLESS_MAIN(InstanceScannerTest)

#include "test_instance_scanner.moc"
