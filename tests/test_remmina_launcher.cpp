// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include <QtTest>

#include "core/remmina_launcher.h"

#include "core/instance_registry.h"
#include "core/profile_catalog.h"
#include "platform/notifier.h"
#include "platform/process_launcher.h"

#include <QList>
#include <QProcessEnvironment>
#include <QStringList>

#include <stdexcept>
#include <utility>

namespace {

RemminaInstance makeInstance(QString id = QStringLiteral("native:/usr/bin/remmina"),
                             QString executable = QStringLiteral("/usr/bin/remmina"),
                             QStringList prefix = {})
{
    return {
        .id = std::move(id),
        .kind = InstanceKind::Native,
        .displayName = QStringLiteral("synthetic display secret"),
        .executable = std::move(executable),
        .launcherPrefix = std::move(prefix),
        .profiles = {},
    };
}

ProfileRecord makeRecord(QString opaqueId = QStringLiteral("opaque-safe-id"),
                         QString launchPath = QStringLiteral("/profiles/secret profile.remmina"))
{
    return {
        .opaqueId = std::move(opaqueId),
        .sourcePath = QStringLiteral("/host/password=synthetic-secret.remmina"),
        .launchPath = std::move(launchPath),
        .name = QStringLiteral("synthetic username"),
        .server = QStringLiteral("secret.example.test"),
        .labels = {},
        .labelsDisplay = {},
        .protocol = QStringLiteral("RDP"),
    };
}

QString withNul(QString value)
{
    value.insert(value.size() / 2, QChar::Null);
    return value;
}

class FakeRegistry final : public InstanceRegistrySource {
public:
    [[nodiscard]] RegistrySnapshot snapshot() const override
    {
        ++calls;
        if (throws) {
            throw std::runtime_error("registry secret exception");
        }
        return value;
    }

    RegistrySnapshot value;
    mutable int calls = 0;
    bool throws = false;
};

class FakeCatalog final : public ProfileCatalogSource {
public:
    [[nodiscard]] const ProfileRecord *resolve(QStringView opaqueId) const override
    {
        ++calls;
        resolvedIds.append(opaqueId.toString());
        if (throws) {
            throw std::runtime_error("catalog secret exception");
        }
        return hasRecord ? &record : nullptr;
    }

    ProfileRecord record = makeRecord();
    bool hasRecord = true;
    bool throws = false;
    mutable int calls = 0;
    mutable QStringList resolvedIds;
};

class RecordingLauncher final : public ProcessLauncher {
public:
    bool startDetached(const LaunchRequest &request) override
    {
        ++calls;
        requests.append(request);
        if (throws) {
            throw std::runtime_error("process secret exception");
        }
        return succeeds;
    }

    QList<LaunchRequest> requests;
    int calls = 0;
    bool succeeds = true;
    bool throws = false;
};

class RecordingNotifier final : public Notifier {
public:
    void showLaunchFailure() override
    {
        ++calls;
        if (throws) {
            throw std::runtime_error("notification secret exception");
        }
    }

    int calls = 0;
    bool throws = false;
};

struct Harness {
    Harness()
        : launcher(registry,
                   catalog,
                   process,
                   notifier,
                   [this] { return environment; })
    {
        registry.value.instances = {makeInstance()};
        registry.value.selectedId = registry.value.instances.constFirst().id;
    }

    FakeRegistry registry;
    FakeCatalog catalog;
    RecordingLauncher process;
    RecordingNotifier notifier;
    QProcessEnvironment environment;
    RemminaLauncher launcher;
};

} // namespace

class RemminaLauncherTest : public QObject {
    Q_OBJECT

private slots:
    void buildsExactNativeFlatpakAndSnapConnectCommands();
    void createsWithPrefixWithoutConsultingCatalog();
    void preservesHostileValuesAsInertArguments();
    void scopesActivationTokenToEachEnvironmentSnapshot();
    void rejectsUnavailableOrAmbiguousSelection();
    void rejectsMissingProfilesAfterValidSelection();
    void rejectsInvalidCommandsAndToken();
    void reportsProcessStartFailureOnce();
    void usesOnlyGenericNotificationConstants();
    void containsDependencyAndNotifierExceptions();
    void consultsCatalogOnlyForConnectAfterValidCurrentInstance();
};

void RemminaLauncherTest::buildsExactNativeFlatpakAndSnapConnectCommands()
{
    struct Case {
        QString id;
        QString program;
        QStringList prefix;
    };
    const QList<Case> cases{
        {QStringLiteral("native:/opt/remmina/bin/remmina"),
         QStringLiteral("/opt/remmina/bin/remmina"),
         {}},
        {QStringLiteral("flatpak:user:org.remmina.Remmina/x86_64/stable"),
         QStringLiteral("/usr/bin/flatpak"),
         {QStringLiteral("--user"),
          QStringLiteral("run"),
          QStringLiteral("org.remmina.Remmina/x86_64/stable")}},
        {QStringLiteral("flatpak:system:org.remmina.Remmina/x86_64/stable"),
         QStringLiteral("/usr/bin/flatpak"),
         {QStringLiteral("--system"),
          QStringLiteral("run"),
          QStringLiteral("org.remmina.Remmina/x86_64/stable")}},
        {QStringLiteral("flatpak:lab:org.remmina.Remmina/x86_64/beta"),
         QStringLiteral("/usr/bin/flatpak"),
         {QStringLiteral("--installation=lab"),
          QStringLiteral("run"),
          QStringLiteral("org.remmina.Remmina/x86_64/beta")}},
        {QStringLiteral("snap:remmina"), QStringLiteral("/snap/bin/remmina"), {}},
    };

    for (const Case &testCase : cases) {
        Harness harness;
        harness.registry.value.instances = {
            makeInstance(testCase.id, testCase.program, testCase.prefix)};
        harness.registry.value.selectedId = testCase.id;
        harness.catalog.record = makeRecord(
            QStringLiteral("opaque-safe-id"), QStringLiteral("/launch/profile one.remmina"));

        QCOMPARE(harness.launcher.connect(QStringLiteral("opaque-safe-id")),
                 RemminaLaunchResult::Started);
        QCOMPARE(harness.process.calls, 1);
        QCOMPARE(harness.process.requests.constFirst().program, testCase.program);
        QStringList expectedArguments = testCase.prefix;
        expectedArguments.append(QStringLiteral("--connect"));
        expectedArguments.append(QStringLiteral("/launch/profile one.remmina"));
        QCOMPARE(harness.process.requests.constFirst().arguments, expectedArguments);
        QCOMPARE(harness.notifier.calls, 0);
    }
}

void RemminaLauncherTest::createsWithPrefixWithoutConsultingCatalog()
{
    Harness harness;
    const QStringList prefix{QStringLiteral("--installation=work station"),
                             QStringLiteral("run"),
                             QStringLiteral("org.remmina.Remmina/x86_64/stable")};
    harness.registry.value.instances = {
        makeInstance(QStringLiteral("flatpak:work station:ref"),
                     QStringLiteral("/usr/bin/flatpak"),
                     prefix)};
    harness.registry.value.selectedId = harness.registry.value.instances.constFirst().id;
    harness.catalog.throws = true;

    QCOMPARE(harness.launcher.create(), RemminaLaunchResult::Started);
    QCOMPARE(harness.catalog.calls, 0);
    QCOMPARE(harness.process.calls, 1);
    QCOMPARE(harness.process.requests.constFirst().program, QStringLiteral("/usr/bin/flatpak"));
    QStringList expected = prefix;
    expected.append(QStringLiteral("--new"));
    QCOMPARE(harness.process.requests.constFirst().arguments, expected);
    QCOMPARE(harness.notifier.calls, 0);
}

void RemminaLauncherTest::preservesHostileValuesAsInertArguments()
{
    Harness harness;
    const QString hostileProgram =
        QStringLiteral("/opt/Remmina dir/remmina;$(program)`tick`\\line\n雪");
    const QStringList hostilePrefix{
        QStringLiteral("prefix with spaces"),
        QStringLiteral("; rm -rf -- /"),
        QStringLiteral("$(touch /tmp/not-run)"),
        QStringLiteral("`also-not-run`\\\nстрока"),
    };
    const QString hostilePath =
        QStringLiteral("relative path;$(profile)`tick`\\line\nプロファイル.remmina");
    harness.registry.value.instances = {
        makeInstance(QStringLiteral("hostile-id"), hostileProgram, hostilePrefix)};
    harness.registry.value.selectedId = QStringLiteral("hostile-id");
    harness.catalog.record = makeRecord(QStringLiteral("opaque"), hostilePath);

    QCOMPARE(harness.launcher.connect(QStringLiteral("opaque")),
             RemminaLaunchResult::Started);

    const LaunchRequest &request = harness.process.requests.constFirst();
    QCOMPARE(request.program, hostileProgram);
    QStringList expected = hostilePrefix;
    expected.append(QStringLiteral("--connect"));
    expected.append(hostilePath);
    QCOMPARE(request.arguments, expected);
    QVERIFY(request.program != QStringLiteral("/bin/sh"));
    QVERIFY(request.program != QStringLiteral("sh"));
}

void RemminaLauncherTest::scopesActivationTokenToEachEnvironmentSnapshot()
{
    Harness harness;
    const QString token = QStringLiteral("token with spaces;$()`ticks`\\line\n雪");
    harness.environment.insert(QStringLiteral("XDG_ACTIVATION_TOKEN"),
                               QStringLiteral("stale inherited token"));
    harness.environment.insert(QStringLiteral("UNCHANGED_MARKER"), QStringLiteral("present"));

    QCOMPARE(harness.launcher.create(token), RemminaLaunchResult::Started);
    QCOMPARE(harness.launcher.create(), RemminaLaunchResult::Started);

    QCOMPARE(harness.process.requests.size(), 2);
    const LaunchRequest &withToken = harness.process.requests.at(0);
    const LaunchRequest &withoutToken = harness.process.requests.at(1);
    QCOMPARE(withToken.environment.value(QStringLiteral("XDG_ACTIVATION_TOKEN")), token);
    QCOMPARE(withToken.environment.value(QStringLiteral("UNCHANGED_MARKER")),
             QStringLiteral("present"));
    QVERIFY(!withToken.arguments.contains(token));
    QVERIFY(!withoutToken.environment.contains(QStringLiteral("XDG_ACTIVATION_TOKEN")));
    QVERIFY(!withoutToken.arguments.contains(token));
    QCOMPARE(harness.environment.value(QStringLiteral("XDG_ACTIVATION_TOKEN")),
             QStringLiteral("stale inherited token"));
    QCOMPARE(harness.notifier.calls, 0);
}

void RemminaLauncherTest::rejectsUnavailableOrAmbiguousSelection()
{
    const QList<RegistrySnapshot> invalidSnapshots{
        {.instances = {makeInstance()}, .selectedId = {}, .failedBackends = {}},
        {.instances = {makeInstance()},
         .selectedId = QStringLiteral("missing-id"),
         .failedBackends = {}},
        {.instances = {makeInstance(QStringLiteral("duplicate")),
                       makeInstance(QStringLiteral("duplicate"),
                                    QStringLiteral("/opt/other/remmina"))},
         .selectedId = QStringLiteral("duplicate"),
         .failedBackends = {}},
        {.instances = {}, .selectedId = {}, .failedBackends = {}},
    };

    for (const RegistrySnapshot &snapshot : invalidSnapshots) {
        Harness harness;
        harness.registry.value = snapshot;

        QCOMPARE(harness.launcher.create(), RemminaLaunchResult::NoInstance);
        QCOMPARE(harness.process.calls, 0);
        QCOMPARE(harness.catalog.calls, 0);
        QCOMPARE(harness.notifier.calls, 1);
    }
}

void RemminaLauncherTest::rejectsMissingProfilesAfterValidSelection()
{
    const QStringList ids{QString{}, QStringLiteral("unknown"), QStringLiteral("dirty-or-stale")};
    for (const QString &id : ids) {
        Harness harness;
        harness.catalog.hasRecord = false;

        QCOMPARE(harness.launcher.connect(id), RemminaLaunchResult::MissingProfile);
        QCOMPARE(harness.catalog.calls, 1);
        QCOMPARE(harness.catalog.resolvedIds, QStringList{id});
        QCOMPARE(harness.process.calls, 0);
        QCOMPARE(harness.notifier.calls, 1);
    }
}

void RemminaLauncherTest::rejectsInvalidCommandsAndToken()
{
    struct Case {
        QString program;
        QStringList prefix;
        QString path;
        QString token;
    };
    const QList<Case> cases{
        {{}, {}, QStringLiteral("/profile.remmina"), {}},
        {QStringLiteral("relative/remmina"), {}, QStringLiteral("/profile.remmina"), {}},
        {withNul(QStringLiteral("/usr/bin/remmina")), {}, QStringLiteral("/profile.remmina"), {}},
        {QStringLiteral("/usr/bin/remmina"),
         {withNul(QStringLiteral("prefix"))},
         QStringLiteral("/profile.remmina"),
         {}},
        {QStringLiteral("/usr/bin/remmina"), {}, {}, {}},
        {QStringLiteral("/usr/bin/remmina"),
         {},
         withNul(QStringLiteral("/profile.remmina")),
         {}},
        {QStringLiteral("/usr/bin/remmina"),
         {},
         QStringLiteral("/profile.remmina"),
         withNul(QStringLiteral("token"))},
    };

    for (const Case &testCase : cases) {
        Harness harness;
        harness.registry.value.instances = {
            makeInstance(QStringLiteral("selected"), testCase.program, testCase.prefix)};
        harness.registry.value.selectedId = QStringLiteral("selected");
        harness.catalog.record = makeRecord(QStringLiteral("opaque"), testCase.path);

        QCOMPARE(harness.launcher.connect(QStringLiteral("opaque"), testCase.token),
                 RemminaLaunchResult::StartFailed);
        QCOMPARE(harness.process.calls, 0);
        QCOMPARE(harness.notifier.calls, 1);
    }
}

void RemminaLauncherTest::reportsProcessStartFailureOnce()
{
    Harness harness;
    harness.process.succeeds = false;

    QCOMPARE(harness.launcher.create(), RemminaLaunchResult::StartFailed);
    QCOMPARE(harness.process.calls, 1);
    QCOMPARE(harness.notifier.calls, 1);
}

void RemminaLauncherTest::usesOnlyGenericNotificationConstants()
{
    QCOMPARE(QString{launchFailureTitle}, QStringLiteral("Remmina KRunner"));
    QCOMPARE(QString{launchFailureBody}, QStringLiteral("Could not open Remmina."));

    const QStringList forbidden{
        QStringLiteral("synthetic-secret"),
        QStringLiteral("synthetic username"),
        QStringLiteral("/host/password=synthetic-secret.remmina"),
        QStringLiteral("secret.example.test"),
        QStringLiteral("activation-secret"),
    };
    for (const QString &secret : forbidden) {
        QVERIFY(!QString{launchFailureTitle}.contains(secret));
        QVERIFY(!QString{launchFailureBody}.contains(secret));
    }

    Harness harness;
    harness.process.succeeds = false;
    QCOMPARE(harness.launcher.connect(QStringLiteral("opaque-safe-id"),
                                      QStringLiteral("activation-secret")),
             RemminaLaunchResult::StartFailed);
    QCOMPARE(harness.notifier.calls, 1);
    // Notifier receives no arguments, so caller data cannot be forwarded to it.
}

void RemminaLauncherTest::containsDependencyAndNotifierExceptions()
{
    {
        Harness harness;
        harness.registry.throws = true;
        QCOMPARE(harness.launcher.create(), RemminaLaunchResult::StartFailed);
        QCOMPARE(harness.notifier.calls, 1);
        QCOMPARE(harness.process.calls, 0);
        QCOMPARE(harness.catalog.calls, 0);
    }
    {
        Harness harness;
        harness.catalog.throws = true;
        QCOMPARE(harness.launcher.connect(QStringLiteral("opaque-safe-id")),
                 RemminaLaunchResult::StartFailed);
        QCOMPARE(harness.notifier.calls, 1);
        QCOMPARE(harness.process.calls, 0);
    }
    {
        Harness harness;
        harness.process.throws = true;
        QCOMPARE(harness.launcher.create(), RemminaLaunchResult::StartFailed);
        QCOMPARE(harness.notifier.calls, 1);
        QCOMPARE(harness.process.calls, 1);
    }
    {
        Harness harness;
        harness.process.succeeds = false;
        harness.notifier.throws = true;
        QCOMPARE(harness.launcher.create(), RemminaLaunchResult::StartFailed);
        QCOMPARE(harness.notifier.calls, 1);
        QCOMPARE(harness.process.calls, 1);
    }
}

void RemminaLauncherTest::consultsCatalogOnlyForConnectAfterValidCurrentInstance()
{
    Harness harness;
    QCOMPARE(harness.launcher.create(), RemminaLaunchResult::Started);
    QCOMPARE(harness.catalog.calls, 0);

    harness.registry.value.selectedId = QStringLiteral("no-longer-present");
    QCOMPARE(harness.launcher.connect(QStringLiteral("opaque-safe-id")),
             RemminaLaunchResult::NoInstance);
    QCOMPARE(harness.catalog.calls, 0);

    harness.registry.value.selectedId = harness.registry.value.instances.constFirst().id;
    QCOMPARE(harness.launcher.connect(QStringLiteral("opaque-safe-id")),
             RemminaLaunchResult::Started);
    QCOMPARE(harness.catalog.calls, 1);
}

QTEST_APPLESS_MAIN(RemminaLauncherTest)

#include "test_remmina_launcher.moc"
