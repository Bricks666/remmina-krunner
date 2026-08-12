// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include <QtTest>

#include "dbus/runner_service.h"

#include "core/instance_registry.h"
#include "core/profile_catalog.h"
#include "core/remmina_launcher.h"
#include "platform/notifier.h"
#include "platform/process_launcher.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QEventLoop>
#include <QRegularExpression>
#include <QTimer>
#include <QVariant>

#include <chrono>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

using namespace std::chrono_literals;
using RemminaKRunner::RemoteMatch;
using RemminaKRunner::RemoteMatches;
using RemminaKRunner::RunnerService;

namespace {

constexpr int possibleMatch = 50;
constexpr int exactMatch = 100;
constexpr int informationalMatch = 30;

RemminaInstance instance(QString id = QStringLiteral("native:/usr/bin/remmina"))
{
    return {
        .id = std::move(id),
        .kind = InstanceKind::Native,
        .displayName = QStringLiteral("instance-password-secret"),
        .executable = QStringLiteral("/secret/gateway/remmina"),
        .launcherPrefix = {QStringLiteral("unknown-launcher-secret")},
        .profiles = {},
    };
}

ProfileRecord record(QString id = QStringLiteral("opaque-profile-a"),
                     QString name = QStringLiteral("Office desktop"),
                     QString server = QStringLiteral("rdp.example.test"),
                     QString labelsDisplay = QStringLiteral("East, Admin"),
                     QString protocol = QStringLiteral("RDP"))
{
    return {
        .opaqueId = std::move(id),
        .sourcePath = QStringLiteral("/private/password=source-secret.remmina"),
        .launchPath = QStringLiteral("/private/username-gateway-note-unknown-secret.remmina"),
        .name = std::move(name),
        .server = std::move(server),
        .labels = {QStringLiteral("East"), QStringLiteral("Admin")},
        .labelsDisplay = std::move(labelsDisplay),
        .protocol = std::move(protocol),
    };
}

class FakeRegistry final : public InstanceRegistryControlSource {
public:
    RegistrySnapshot snapshot() const override
    {
        ++snapshotCalls;
        if (throwSnapshot) {
            throw std::runtime_error("registry-password-private");
        }
        return value;
    }

    RegistrySnapshot rescanAndRepair() override
    {
        ++rescanCalls;
        if (events != nullptr) {
            events->append(QStringLiteral("rescan"));
        }
        if (throwRescan) {
            throw std::runtime_error("rescan-path-private");
        }
        return value;
    }

    RegistrySnapshot value{{instance()}, QStringLiteral("native:/usr/bin/remmina"), {}};
    mutable int snapshotCalls = 0;
    int rescanCalls = 0;
    bool throwSnapshot = false;
    bool throwRescan = false;
    QStringList *events = nullptr;
};

class FakeCatalog final : public ProfileCatalogAccess {
public:
    CatalogResult records(const RemminaInstance &selected) override
    {
        ++recordsCalls;
        recordInstanceIds.append(selected.id);
        if (throwRecords) {
            throw std::runtime_error("catalog-note-private");
        }
        return result;
    }

    std::optional<ProfileRecord> resolve(QStringView expectedInstanceId,
                                         QStringView opaqueId) const override
    {
        ++resolveCalls;
        resolvedInstanceIds.append(expectedInstanceId.toString());
        resolvedOpaqueIds.append(opaqueId.toString());
        if (throwResolve) {
            throw std::runtime_error("resolve-secret-private");
        }
        if (resolvedRecord.has_value() && expectedInstanceId == resolvableInstanceId
            && opaqueId == resolvedRecord->opaqueId) {
            return resolvedRecord;
        }
        return std::nullopt;
    }

    void endSession() override
    {
        ++endCalls;
        if (events != nullptr) {
            events->append(QStringLiteral("end"));
        }
        if (throwEnd) {
            throw std::runtime_error("end-secret-private");
        }
    }

    void reset() override
    {
        ++resetCalls;
        if (events != nullptr) {
            events->append(QStringLiteral("reset"));
        }
        if (throwReset) {
            throw std::runtime_error("reset-secret-private");
        }
    }

    CatalogResult result{QList<ProfileRecord>{record()}};
    std::optional<ProfileRecord> resolvedRecord{record()};
    QString resolvableInstanceId = QStringLiteral("native:/usr/bin/remmina");
    int recordsCalls = 0;
    mutable int resolveCalls = 0;
    int endCalls = 0;
    int resetCalls = 0;
    QStringList recordInstanceIds;
    mutable QStringList resolvedInstanceIds;
    mutable QStringList resolvedOpaqueIds;
    bool throwRecords = false;
    mutable bool throwResolve = false;
    bool throwEnd = false;
    bool throwReset = false;
    QStringList *events = nullptr;
};

class FakeLauncher final : public RemminaLaunchSource {
public:
    RemminaLaunchResult create(QStringView activationToken) override
    {
        ++createCalls;
        tokens.append(activationToken.toString());
        if (throwCreate) {
            throw std::runtime_error("create-token-private");
        }
        return result;
    }

    RemminaLaunchResult connect(QStringView opaqueId, QStringView activationToken) override
    {
        ++connectCalls;
        ids.append(opaqueId.toString());
        tokens.append(activationToken.toString());
        if (throwConnect) {
            throw std::runtime_error("connect-token-private");
        }
        return result;
    }

    RemminaLaunchResult result = RemminaLaunchResult::Started;
    int createCalls = 0;
    int connectCalls = 0;
    QStringList ids;
    QStringList tokens;
    bool throwCreate = false;
    bool throwConnect = false;
};

class RecordingProcessLauncher final : public ProcessLauncher {
public:
    bool startDetached(const LaunchRequest &request) override
    {
        ++calls;
        requests.append(request);
        return true;
    }

    int calls = 0;
    QList<LaunchRequest> requests;
};

class RecordingNotifier final : public Notifier {
public:
    void showLaunchFailure() override { ++calls; }
    int calls = 0;
};

struct Harness {
    explicit Harness(std::chrono::milliseconds timeout = 30s)
        : service(registry, catalog, launcher, timeout)
    {
    }

    FakeRegistry registry;
    FakeCatalog catalog;
    FakeLauncher launcher;
    RunnerService service;
};

QDBusMessage await(QDBusPendingCall call)
{
    QDBusPendingCallWatcher watcher(std::move(call));
    if (!watcher.isFinished()) {
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&watcher, &QDBusPendingCallWatcher::finished,
                         &loop, &QEventLoop::quit);
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeout.start(5000);
        loop.exec();
    }
    if (!watcher.isFinished()) {
        return QDBusMessage::createError(QDBusError::Timeout,
                                         QStringLiteral("private match timed out"));
    }
    return watcher.reply();
}

void appendStrings(const QVariant &value, QStringList &strings)
{
    if (value.metaType() == QMetaType::fromType<QString>()) {
        strings.append(value.toString());
    } else if (value.metaType() == QMetaType::fromType<QStringList>()) {
        strings.append(value.toStringList());
    } else if (value.metaType() == QMetaType::fromType<QVariantList>()) {
        for (const QVariant &item : value.toList()) {
            appendStrings(item, strings);
        }
    } else if (value.metaType() == QMetaType::fromType<QVariantMap>()) {
        const QVariantMap map = value.toMap();
        for (auto iterator = map.cbegin(); iterator != map.cend(); ++iterator) {
            strings.append(iterator.key());
            appendStrings(iterator.value(), strings);
        }
    } else if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
        const QDBusArgument argument = qvariant_cast<QDBusArgument>(value);
        const RemoteMatches matches = qdbus_cast<RemoteMatches>(argument);
        for (const RemoteMatch &match : matches) {
            appendStrings(QVariantList{match.id,
                                       match.text,
                                       match.iconName,
                                       match.categoryRelevance,
                                       match.relevance,
                                       match.properties},
                          strings);
        }
    }
}

void verifySafeProperties(const RemoteMatch &match, const QString &subtext)
{
    QCOMPARE(match.properties.keys(),
             QStringList({QStringLiteral("actions"),
                          QStringLiteral("category"),
                          QStringLiteral("subtext")}));
    QCOMPARE(match.properties.value(QStringLiteral("subtext")).metaType(),
             QMetaType::fromType<QString>());
    QCOMPARE(match.properties.value(QStringLiteral("subtext")).toString(), subtext);
    QCOMPARE(match.properties.value(QStringLiteral("category")).metaType(),
             QMetaType::fromType<QString>());
    QCOMPARE(match.properties.value(QStringLiteral("category")).toString(),
             QStringLiteral("Remmina"));
    QCOMPARE(match.properties.value(QStringLiteral("actions")).metaType(),
             QMetaType::fromType<QStringList>());
    QVERIFY(match.properties.value(QStringLiteral("actions")).toStringList().isEmpty());
}

void verifyGenericError(const RemoteMatches &matches, const QString &id)
{
    QCOMPARE(matches.size(), 1);
    const RemoteMatch &match = matches.constFirst();
    QCOMPARE(match.id, id);
    QCOMPARE(match.text, QStringLiteral("Remmina unavailable"));
    QCOMPARE(match.iconName, QStringLiteral("org.remmina.Remmina"));
    QCOMPARE(match.categoryRelevance, informationalMatch);
    QCOMPARE(match.relevance, 0.0);
    QVERIFY(!match.properties.value(QStringLiteral("subtext")).toString().isEmpty());
    verifySafeProperties(match,
                         match.properties.value(QStringLiteral("subtext")).toString());
}

} // namespace

class RunnerServiceTest final : public QObject {
    Q_OBJECT

private slots:
    void ignoredAndCreationQueriesTouchNoSources();
    void invalidSelectionsReturnGenericErrorWithoutCatalog();
    void profileMatchesExposeOnlyVisibleMetadataAndStableOrdering();
    void subtitleOmitsBlankComponents();
    void catalogErrorsEndSessionAndClearLaunchability();
    void successfulLookupsReuseSessionAndRestartIdleTimer();
    void emptySuccessfulLookupStillExpiresSession();
    void runRoutesOnlySupportedIdsAndAlwaysConsumesToken();
    void onlyCurrentOfferedProfileIdsAreActionable();
    void activationTokenReplacementAndLifecycleClearing();
    void selectionChangeBetweenMatchAndRunIsRejectedByLauncher();
    void teardownIdleAndDestructorHaveExactSessionSemantics();
    void configHasExactTriggerContractAndResetOrdering();
    void everyPublicSlotContainsDependencyExceptions();
    void actionsAreAlwaysEmpty();
};

void RunnerServiceTest::ignoredAndCreationQueriesTouchNoSources()
{
    Harness harness;
    for (const QString &query : {QString{},
                                 QStringLiteral("   "),
                                 QStringLiteral("rem"),
                                 QStringLiteral("REM   "),
                                 QStringLiteral("remote host"),
                                 QStringLiteral("rem:host")}) {
        QVERIFY(harness.service.Match(query).isEmpty());
    }
    QCOMPARE(harness.registry.snapshotCalls, 0);
    QCOMPARE(harness.catalog.recordsCalls, 0);
    QCOMPARE(harness.catalog.endCalls, 0);

    const RemoteMatches matches = harness.service.Match(QStringLiteral("ReM NeW"));
    QCOMPARE(matches.size(), 1);
    const RemoteMatch &match = matches.constFirst();
    QCOMPARE(match.id, QStringLiteral("action:new"));
    QCOMPARE(match.text, QStringLiteral("Create a Remmina connection"));
    QCOMPARE(match.iconName, QStringLiteral("org.remmina.Remmina"));
    QCOMPARE(match.categoryRelevance, exactMatch);
    QCOMPARE(match.relevance, 1.0);
    verifySafeProperties(match, QStringLiteral("Create a new connection profile"));
    QCOMPARE(harness.registry.snapshotCalls, 0);
    QCOMPARE(harness.catalog.recordsCalls, 0);
    QCOMPARE(harness.catalog.endCalls, 0);
}

void RunnerServiceTest::invalidSelectionsReturnGenericErrorWithoutCatalog()
{
    const QList<RegistrySnapshot> snapshots{
        {{instance()}, {}, {}},
        {{instance()}, QStringLiteral("absent"), {}},
        {{instance(QStringLiteral("duplicate")), instance(QStringLiteral("duplicate"))},
         QStringLiteral("duplicate"), {}},
    };
    for (const RegistrySnapshot &snapshot : snapshots) {
        Harness harness;
        harness.registry.value = snapshot;
        verifyGenericError(harness.service.Match(QStringLiteral("rem office")),
                           QStringLiteral("error:no-instance"));
        QCOMPARE(harness.registry.snapshotCalls, 1);
        QCOMPARE(harness.catalog.recordsCalls, 0);
        QCOMPARE(harness.catalog.endCalls, 0);
    }
}

void RunnerServiceTest::profileMatchesExposeOnlyVisibleMetadataAndStableOrdering()
{
    Harness harness;
    harness.catalog.result = QList<ProfileRecord>{
        record(QStringLiteral("opaque-z"), QStringLiteral("Zulu office")),
        record(QStringLiteral("opaque-a"), QStringLiteral("Alpha office")),
    };

    const RemoteMatches matches = harness.service.Match(QStringLiteral("rem office east"));
    QCOMPARE(harness.registry.snapshotCalls, 1);
    QCOMPARE(harness.catalog.recordsCalls, 1);
    QCOMPARE(matches.size(), 2);
    QCOMPARE(matches.at(0).id, QStringLiteral("opaque-a"));
    QCOMPARE(matches.at(1).id, QStringLiteral("opaque-z"));
    for (const RemoteMatch &match : matches) {
        QCOMPARE(match.iconName, QStringLiteral("org.remmina.Remmina"));
        QCOMPARE(match.categoryRelevance, possibleMatch);
        QCOMPARE(match.relevance, 0.75);
        verifySafeProperties(match, QStringLiteral("RDP · rdp.example.test · East, Admin"));
    }

    QVariantList variants;
    for (const RemoteMatch &match : matches) {
        variants.append(QVariantList{match.id,
                                     match.text,
                                     match.iconName,
                                     match.categoryRelevance,
                                     match.relevance,
                                     match.properties});
    }
    QDBusConnection bus = QDBusConnection::sessionBus();
    QVERIFY(bus.isConnected());
    const QString serviceName = QStringLiteral("org.example.RemminaKRunner.Privacy.p%1")
                                    .arg(QCoreApplication::applicationPid());
    QVERIFY(bus.registerService(serviceName));
    QVERIFY(bus.registerObject(QStringLiteral("/runner"), &harness.service,
                               QDBusConnection::ExportAllSlots));
    QDBusInterface interface(serviceName,
                             QStringLiteral("/runner"),
                             QStringLiteral("org.kde.krunner1"),
                             bus);
    QVERIFY(interface.isValid());
    const QDBusMessage reply = await(interface.asyncCall(QStringLiteral("Match"),
                                                         QStringLiteral("rem office east")));
    QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
    QCOMPARE(reply.arguments().size(), 1);
    const QDBusArgument encoded =
        qvariant_cast<QDBusArgument>(reply.arguments().constFirst());
    QCOMPARE(encoded.currentSignature(), QStringLiteral("a(sssida{sv})"));
    variants.append(QVariant::fromValue(encoded));
    bus.unregisterObject(QStringLiteral("/runner"));
    QVERIFY(bus.unregisterService(serviceName));
    const QVariantMap config = harness.service.Config();
    variants.append(config);

    QStringList strings;
    appendStrings(variants, strings);
    for (const QString &secret : {QStringLiteral("password=source-secret"),
                                  QStringLiteral("username-gateway-note-unknown-secret"),
                                  QStringLiteral("instance-password-secret"),
                                  QStringLiteral("/secret/gateway/remmina"),
                                  QStringLiteral("unknown-launcher-secret")}) {
        for (const QString &visible : strings) {
            QVERIFY2(!visible.contains(secret), qPrintable(visible));
        }
    }
}

void RunnerServiceTest::subtitleOmitsBlankComponents()
{
    struct Case {
        QString protocol;
        QString server;
        QString labels;
        QString expected;
    };
    const QList<Case> cases{
        {QStringLiteral("  RDP  "), QStringLiteral(" server "), QStringLiteral(" lab "),
         QStringLiteral("RDP · server · lab")},
        {QStringLiteral("  "), QStringLiteral(" server "), {}, QStringLiteral("server")},
        {QStringLiteral("VNC"), QStringLiteral("  "), QStringLiteral(" lab "),
         QStringLiteral("VNC · lab")},
        {{}, {}, {}, {}},
    };
    for (const Case &item : cases) {
        Harness harness;
        harness.catalog.result = QList<ProfileRecord>{record(QStringLiteral("opaque"),
                                                              QStringLiteral("Office"),
                                                              item.server,
                                                              item.labels,
                                                              item.protocol)};
        const RemoteMatches matches = harness.service.Match(QStringLiteral("rem office"));
        QCOMPARE(matches.size(), 1);
        verifySafeProperties(matches.constFirst(), item.expected);
    }
}

void RunnerServiceTest::catalogErrorsEndSessionAndClearLaunchability()
{
    struct Case {
        ProfileCatalogError error;
        QString id;
    };
    const QList<Case> cases{
        {ProfileCatalogError::NoProfileDirectory, QStringLiteral("error:no-profiles")},
        {ProfileCatalogError::UnreadableDirectory, QStringLiteral("error:unreadable")},
    };
    for (const Case &item : cases) {
        Harness harness;
        harness.catalog.result = item.error;
        verifyGenericError(harness.service.Match(QStringLiteral("rem office")), item.id);
        QCOMPARE(harness.catalog.recordsCalls, 1);
        QCOMPARE(harness.catalog.endCalls, 1);

        harness.service.SetActivationToken(QStringLiteral("must-be-consumed"));
        harness.service.Run(item.id, QString{});
        QCOMPARE(harness.launcher.createCalls, 0);
        QCOMPARE(harness.launcher.connectCalls, 0);
        harness.service.Run(QStringLiteral("action:new"), QString{});
        QCOMPARE(harness.launcher.tokens, QStringList{QString{}});
    }
}

void RunnerServiceTest::successfulLookupsReuseSessionAndRestartIdleTimer()
{
    Harness harness(200ms);
    QCOMPARE(harness.service.Match(QStringLiteral("rem office")).size(), 1);
    QCOMPARE(harness.service.Match(QStringLiteral("rem office east")).size(), 1);
    QCOMPARE(harness.registry.snapshotCalls, 2);
    QCOMPARE(harness.catalog.recordsCalls, 2);
    QCOMPARE(harness.catalog.recordInstanceIds,
             QStringList({QStringLiteral("native:/usr/bin/remmina"),
                          QStringLiteral("native:/usr/bin/remmina")}));
    QCOMPARE(harness.catalog.endCalls, 0);

    QVERIFY(harness.service.Match(QStringLiteral("rem")).isEmpty());
    QCOMPARE(harness.registry.snapshotCalls, 2);
    QCOMPARE(harness.catalog.recordsCalls, 2);
    QCOMPARE(harness.catalog.endCalls, 0);
    QTRY_COMPARE_WITH_TIMEOUT(harness.catalog.endCalls, 1, 1000);
}

void RunnerServiceTest::emptySuccessfulLookupStillExpiresSession()
{
    Harness harness(0ms);
    harness.catalog.result = QList<ProfileRecord>{};
    QVERIFY(harness.service.Match(QStringLiteral("rem no-match")).isEmpty());
    QCOMPARE(harness.registry.snapshotCalls, 1);
    QCOMPARE(harness.catalog.recordsCalls, 1);
    QTRY_COMPARE_WITH_TIMEOUT(harness.catalog.endCalls, 1, 1000);
}

void RunnerServiceTest::runRoutesOnlySupportedIdsAndAlwaysConsumesToken()
{
    Harness harness;
    QCOMPARE(harness.service.Match(QStringLiteral("rem office")).size(), 1);
    harness.service.SetActivationToken(QStringLiteral("token-action"));
    harness.service.Run(QStringLiteral("action:new"), QString{});
    QCOMPARE(harness.launcher.createCalls, 1);
    QCOMPARE(harness.launcher.tokens, QStringList{QStringLiteral("token-action")});
    QCOMPARE(harness.catalog.endCalls, 1);

    QCOMPARE(harness.service.Match(QStringLiteral("rem office")).size(), 1);
    harness.service.SetActivationToken(QStringLiteral("token-profile"));
    harness.service.Run(QStringLiteral("opaque-profile-a"), QString{});
    QCOMPARE(harness.launcher.connectCalls, 1);
    QCOMPARE(harness.launcher.ids, QStringList{QStringLiteral("opaque-profile-a")});
    QCOMPARE(harness.launcher.tokens.constLast(), QStringLiteral("token-profile"));

    for (const auto &[id, action] : {
             std::pair{QString{}, QString{}},
             std::pair{QStringLiteral("error:internal"), QString{}},
             std::pair{QStringLiteral("action:unknown"), QString{}},
             std::pair{QStringLiteral("never-offered-profile"), QString{}},
             std::pair{QStringLiteral("opaque-profile-a"), QStringLiteral("unsupported")},
         }) {
        harness.service.SetActivationToken(QStringLiteral("discard-this"));
        harness.service.Run(id, action);
    }
    QCOMPARE(harness.launcher.createCalls, 1);
    QCOMPARE(harness.launcher.connectCalls, 1);
    harness.service.Run(QStringLiteral("action:new"), QString{});
    QCOMPARE(harness.launcher.tokens.constLast(), QString{});
}

void RunnerServiceTest::onlyCurrentOfferedProfileIdsAreActionable()
{
    const QList<ProfileRecord> records{
        record(QStringLiteral("office-id"), QStringLiteral("Office")),
        record(QStringLiteral("home-id"), QStringLiteral("Home")),
    };

    {
        Harness harness;
        harness.catalog.result = records;
        QCOMPARE(harness.service.Match(QStringLiteral("rem office")).size(), 1);
        QCOMPARE(harness.service.Match(QStringLiteral("rem home")).size(), 1);
        harness.service.SetActivationToken(QStringLiteral("discard-replaced"));
        harness.service.Run(QStringLiteral("office-id"), QString{});
        QCOMPARE(harness.launcher.connectCalls, 0);
        harness.service.Run(QStringLiteral("action:new"), QString{});
        QCOMPARE(harness.launcher.tokens, QStringList{QString{}});
    }

    for (const QString &ending : {QStringLiteral("teardown"),
                                  QStringLiteral("config"),
                                  QStringLiteral("error")}) {
        Harness harness;
        harness.catalog.result = records;
        QCOMPARE(harness.service.Match(QStringLiteral("rem office")).size(), 1);
        if (ending == QStringLiteral("teardown")) {
            harness.service.Teardown();
        } else if (ending == QStringLiteral("config")) {
            harness.service.Config();
        } else {
            harness.catalog.result = ProfileCatalogError::UnreadableDirectory;
            verifyGenericError(harness.service.Match(QStringLiteral("rem office")),
                               QStringLiteral("error:unreadable"));
        }
        harness.service.Run(QStringLiteral("office-id"), QString{});
        QCOMPARE(harness.launcher.connectCalls, 0);
    }

    {
        Harness idle(0ms);
        idle.catalog.result = records;
        QCOMPARE(idle.service.Match(QStringLiteral("rem office")).size(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(idle.catalog.endCalls, 1, 1000);
        idle.service.Run(QStringLiteral("office-id"), QString{});
        QCOMPARE(idle.launcher.connectCalls, 0);
    }

    Harness current;
    current.catalog.result = records;
    QCOMPARE(current.service.Match(QStringLiteral("rem home")).size(), 1);
    current.service.Run(QStringLiteral("home-id"), QString{});
    QCOMPARE(current.launcher.connectCalls, 1);
    QCOMPARE(current.launcher.ids, QStringList{QStringLiteral("home-id")});
}

void RunnerServiceTest::activationTokenReplacementAndLifecycleClearing()
{
    Harness harness(0ms);
    harness.service.SetActivationToken(QStringLiteral("old"));
    harness.service.SetActivationToken(QStringLiteral("replacement"));
    harness.service.Run(QStringLiteral("action:new"), QString{});
    QCOMPARE(harness.launcher.tokens, QStringList{QStringLiteral("replacement")});

    harness.service.SetActivationToken(QStringLiteral("teardown-secret"));
    harness.service.Teardown();
    harness.service.Run(QStringLiteral("action:new"), QString{});
    QCOMPARE(harness.launcher.tokens.constLast(), QString{});

    harness.service.SetActivationToken(QStringLiteral("config-secret"));
    harness.service.Config();
    harness.service.Run(QStringLiteral("action:new"), QString{});
    QCOMPARE(harness.launcher.tokens.constLast(), QString{});

    QCOMPARE(harness.service.Match(QStringLiteral("rem office")).size(), 1);
    harness.service.SetActivationToken(QStringLiteral("idle-secret"));
    QTRY_COMPARE_WITH_TIMEOUT(harness.catalog.endCalls, 2, 1000);
    harness.service.Run(QStringLiteral("action:new"), QString{});
    QCOMPARE(harness.launcher.tokens.constLast(), QString{});
}

void RunnerServiceTest::selectionChangeBetweenMatchAndRunIsRejectedByLauncher()
{
    FakeRegistry registry;
    FakeCatalog catalog;
    RecordingProcessLauncher process;
    RecordingNotifier notifier;
    RemminaLauncher launcher(registry, catalog, process, notifier, [] {
        return QProcessEnvironment{};
    });
    RunnerService service(registry, catalog, launcher);

    registry.value.instances = {instance(QStringLiteral("B"))};
    registry.value.selectedId = QStringLiteral("B");
    catalog.resolvableInstanceId = QStringLiteral("B");
    catalog.resolvedRecord = record();
    QCOMPARE(service.Match(QStringLiteral("rem office")).size(), 1);

    registry.value.instances = {instance(QStringLiteral("A"))};
    registry.value.selectedId = QStringLiteral("A");
    service.Run(QStringLiteral("opaque-profile-a"), QString{});
    QCOMPARE(catalog.resolveCalls, 1);
    QCOMPARE(catalog.resolvedInstanceIds, QStringList{QStringLiteral("A")});
    QCOMPARE(process.calls, 0);
    QCOMPARE(notifier.calls, 1);
}

void RunnerServiceTest::teardownIdleAndDestructorHaveExactSessionSemantics()
{
    FakeRegistry registry;
    FakeCatalog catalog;
    FakeLauncher launcher;
    {
        RunnerService service(registry, catalog, launcher, 0ms);
        QCOMPARE(service.Match(QStringLiteral("rem office")).size(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(catalog.endCalls, 1, 1000);
        service.Teardown();
        QCOMPARE(catalog.endCalls, 2);
        service.Teardown();
        QCOMPARE(catalog.endCalls, 3);
    }
    QCOMPARE(catalog.endCalls, 3);

    {
        RunnerService service(registry, catalog, launcher, 10s);
        QCOMPARE(service.Match(QStringLiteral("rem office")).size(), 1);
    }
    QCOMPARE(catalog.endCalls, 4);
}

void RunnerServiceTest::configHasExactTriggerContractAndResetOrdering()
{
    Harness harness;
    QStringList events;
    harness.registry.events = &events;
    harness.catalog.events = &events;
    QCOMPARE(harness.service.Match(QStringLiteral("rem office")).size(), 1);
    harness.service.SetActivationToken(QStringLiteral("private-token"));
    const QVariantMap config = harness.service.Config();
    QCOMPARE(harness.catalog.endCalls, 1);
    QCOMPARE(harness.registry.rescanCalls, 1);
    QCOMPARE(harness.catalog.resetCalls, 1);
    QCOMPARE(events,
             QStringList({QStringLiteral("end"),
                          QStringLiteral("rescan"),
                          QStringLiteral("reset")}));
    QCOMPARE(config.keys(),
             QStringList({QStringLiteral("MatchRegex"), QStringLiteral("TriggerWords")}));
    QCOMPARE(config.value(QStringLiteral("MatchRegex")).metaType(),
             QMetaType::fromType<QString>());
    QCOMPARE(config.value(QStringLiteral("TriggerWords")).metaType(),
             QMetaType::fromType<QStringList>());
    QCOMPARE(config.value(QStringLiteral("TriggerWords")).toStringList(),
             QStringList{QStringLiteral("rem")});

    const QRegularExpression regex(config.value(QStringLiteral("MatchRegex")).toString());
    QVERIFY(regex.isValid());
    for (const QString &accepted : {QStringLiteral("rem"),
                                    QStringLiteral("REM"),
                                    QStringLiteral("rem host"),
                                    QStringLiteral("rem new"),
                                    QStringLiteral("rem\thost")}) {
        QVERIFY2(regex.match(accepted).hasMatch(), qPrintable(accepted));
    }
    for (const QString &rejected : {QString{},
                                    QStringLiteral("remote"),
                                    QStringLiteral("rem:host"),
                                    QStringLiteral("x rem host"),
                                    QStringLiteral(" rem host")}) {
        QVERIFY2(!regex.match(rejected).hasMatch(), qPrintable(rejected));
    }

    harness.service.Run(QStringLiteral("action:new"), QString{});
    QCOMPARE(harness.launcher.tokens, QStringList{QString{}});

    Harness throwing;
    QStringList throwingEvents;
    throwing.registry.events = &throwingEvents;
    throwing.catalog.events = &throwingEvents;
    throwing.registry.throwRescan = true;
    const QVariantMap fallback = throwing.service.Config();
    QCOMPARE(throwing.registry.rescanCalls, 1);
    QCOMPARE(throwing.catalog.resetCalls, 1);
    QCOMPARE(fallback, config);
    QCOMPARE(throwingEvents,
             QStringList({QStringLiteral("rescan"), QStringLiteral("reset")}));
}

void RunnerServiceTest::everyPublicSlotContainsDependencyExceptions()
{
    Harness snapshotFailure;
    snapshotFailure.registry.throwSnapshot = true;
    verifyGenericError(snapshotFailure.service.Match(QStringLiteral("rem office")),
                       QStringLiteral("error:internal"));
    QCOMPARE(snapshotFailure.catalog.recordsCalls, 0);

    Harness recordFailure;
    recordFailure.catalog.throwRecords = true;
    recordFailure.catalog.throwEnd = true;
    verifyGenericError(recordFailure.service.Match(QStringLiteral("rem office")),
                       QStringLiteral("error:internal"));
    QCOMPARE(recordFailure.catalog.endCalls, 1);
    recordFailure.service.Teardown();
    QCOMPARE(recordFailure.catalog.endCalls, 2);

    Harness launcherFailure;
    launcherFailure.launcher.throwCreate = true;
    launcherFailure.service.SetActivationToken(QStringLiteral("private"));
    launcherFailure.service.Run(QStringLiteral("action:new"), QString{});
    QCOMPARE(launcherFailure.launcher.createCalls, 1);
    launcherFailure.service.Run(QStringLiteral("action:new"), QString{});
    QCOMPARE(launcherFailure.launcher.tokens.constLast(), QString{});
    launcherFailure.launcher.throwConnect = true;
    QCOMPARE(launcherFailure.service.Match(QStringLiteral("rem office")).size(), 1);
    launcherFailure.service.Run(QStringLiteral("opaque-profile-a"), QString{});
    QCOMPARE(launcherFailure.launcher.connectCalls, 1);

    Harness resetFailure;
    resetFailure.registry.throwRescan = true;
    resetFailure.catalog.throwReset = true;
    const QVariantMap config = resetFailure.service.Config();
    QCOMPARE(resetFailure.registry.rescanCalls, 1);
    QCOMPARE(resetFailure.catalog.resetCalls, 1);
    QVERIFY(!config.isEmpty());

    QVERIFY(resetFailure.service.Actions().isEmpty());
    resetFailure.service.SetActivationToken(QString(32, QLatin1Char('x')));
}

void RunnerServiceTest::actionsAreAlwaysEmpty()
{
    Harness harness;
    QVERIFY(harness.service.Actions().isEmpty());
    harness.registry.throwSnapshot = true;
    harness.catalog.throwRecords = true;
    harness.launcher.throwConnect = true;
    QVERIFY(harness.service.Actions().isEmpty());
}

QTEST_GUILESS_MAIN(RunnerServiceTest)

#include "test_runner_service.moc"
