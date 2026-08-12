// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include "application.h"

#include "core/remmina_instance.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QRegularExpression>
#include <QTest>
#include <QTextStream>

#include <KRunner/AbstractRunnerTest>

#include <stdexcept>

namespace {

RemminaInstance instance(QString id, InstanceKind kind)
{
    return {
        .id = std::move(id),
        .kind = kind,
        .displayName = QStringLiteral("privacy-sentinel-display"),
        .executable = QStringLiteral("/privacy/sentinel/remmina"),
        .launcherPrefix = {QStringLiteral("privacy-sentinel-ref")},
        .profiles = {},
    };
}

class FakeBackend final : public RemminaKRunner::ApplicationBackend {
public:
    RegistrySnapshot rescanAndRepair() override
    {
        ++rescanCalls;
        if (throwOnRescan) {
            throw std::runtime_error("private-rescan-error-sentinel");
        }
        return snapshot;
    }

    int startService(QCoreApplication &) override
    {
        ++serviceCalls;
        return serviceResult;
    }

    RegistrySnapshot snapshot;
    int rescanCalls = 0;
    int serviceCalls = 0;
    int serviceResult = 17;
    bool throwOnRescan = false;
};

class ConfigService final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.krunner1")

public slots:
    QVariantMap Config()
    {
        ++calls;
        return {
            {QStringLiteral("MatchRegex"), QStringLiteral("(?i)^rem(?:\\s.*)?$")},
        };
    }

public:
    int calls = 0;
};

struct InvocationResult {
    int exitCode;
    QString standardOutput;
    QString standardError;
};

InvocationResult invoke(const QStringList &arguments, FakeBackend &backend)
{
    QByteArray outputBytes;
    QByteArray errorBytes;
    QBuffer outputBuffer(&outputBytes);
    QBuffer errorBuffer(&errorBytes);
    if (!outputBuffer.open(QIODevice::WriteOnly)
        || !errorBuffer.open(QIODevice::WriteOnly)) {
        qFatal("Unable to open test buffers");
    }
    QTextStream output(&outputBuffer);
    QTextStream error(&errorBuffer);
    const int exitCode = RemminaKRunner::runApplication(
        *QCoreApplication::instance(), arguments, backend, output, error);
    output.flush();
    error.flush();
    return {
        exitCode,
        QString::fromUtf8(outputBytes),
        QString::fromUtf8(errorBytes),
    };
}

} // namespace

class RescanCliTest : public QObject {
    Q_OBJECT

private slots:
    void rescanRepairsSelectionAndPrintsOnlySafeSummary();
    void emptyScanIsSuccessful();
    void normalStartupRescansBeforeStartingService();
    void invalidArgumentsAreRejectedWithoutTouchingBackends();
    void scanFailureIsSanitizedAndDoesNotStartService();
    void installedKf6KeepsTheEffectiveTriggerCaseInsensitive();
};

void RescanCliTest::rescanRepairsSelectionAndPrintsOnlySafeSummary()
{
    FakeBackend backend;
    backend.snapshot = {
        {
            instance(QStringLiteral("native:/private/path"), InstanceKind::Native),
            instance(QStringLiteral("flatpak:private:ref"), InstanceKind::Flatpak),
            instance(QStringLiteral("snap:private"), InstanceKind::Snap),
        },
        QStringLiteral("flatpak:private:ref"),
        {QStringLiteral("snap")},
    };

    const InvocationResult result = invoke(
        {QStringLiteral("remmina-krunner"), QStringLiteral("--rescan")}, backend);

    QCOMPARE(result.exitCode, 0);
    QCOMPARE(backend.rescanCalls, 1);
    QCOMPARE(backend.serviceCalls, 0);
    QCOMPARE(result.standardError, QString{});
    QCOMPARE(result.standardOutput,
             QStringLiteral("scan_status=partial\n"
                            "instances=3\n"
                            "native=1\n"
                            "flatpak=1\n"
                            "snap=1\n"
                            "selected_type=flatpak\n"));
    QVERIFY(!result.standardOutput.contains(QStringLiteral("private")));
    QVERIFY(!result.standardOutput.contains(QStringLiteral("sentinel")));
}

void RescanCliTest::emptyScanIsSuccessful()
{
    FakeBackend backend;
    const InvocationResult result = invoke(
        {QStringLiteral("remmina-krunner"), QStringLiteral("--rescan")}, backend);

    QCOMPARE(result.exitCode, 0);
    QCOMPARE(backend.rescanCalls, 1);
    QCOMPARE(backend.serviceCalls, 0);
    QCOMPARE(result.standardError, QString{});
    QCOMPARE(result.standardOutput,
             QStringLiteral("scan_status=empty\n"
                            "instances=0\n"
                            "native=0\n"
                            "flatpak=0\n"
                            "snap=0\n"
                            "selected_type=none\n"));
}

void RescanCliTest::normalStartupRescansBeforeStartingService()
{
    FakeBackend backend;
    backend.snapshot.instances = {
        instance(QStringLiteral("native:/private/path"), InstanceKind::Native),
    };
    backend.snapshot.selectedId = QStringLiteral("native:/private/path");

    const InvocationResult result = invoke({QStringLiteral("remmina-krunner")}, backend);

    QCOMPARE(result.exitCode, 17);
    QCOMPARE(backend.rescanCalls, 1);
    QCOMPARE(backend.serviceCalls, 1);
    QCOMPARE(result.standardOutput, QString{});
    QCOMPARE(result.standardError, QString{});
}

void RescanCliTest::invalidArgumentsAreRejectedWithoutTouchingBackends()
{
    for (const QStringList arguments : {
             QStringList{QStringLiteral("remmina-krunner"), QStringLiteral("--unknown")},
             QStringList{QStringLiteral("remmina-krunner"), QStringLiteral("--rescan"),
                         QStringLiteral("extra")},
         }) {
        FakeBackend backend;
        const InvocationResult result = invoke(arguments, backend);
        QCOMPARE(result.exitCode, 64);
        QCOMPARE(backend.rescanCalls, 0);
        QCOMPARE(backend.serviceCalls, 0);
        QCOMPARE(result.standardOutput, QString{});
        QCOMPARE(result.standardError,
                 QStringLiteral("Usage: remmina-krunner [--rescan]\n"));
    }
}

void RescanCliTest::scanFailureIsSanitizedAndDoesNotStartService()
{
    FakeBackend backend;
    backend.throwOnRescan = true;
    const InvocationResult result = invoke(
        {QStringLiteral("remmina-krunner"), QStringLiteral("--rescan")}, backend);

    QCOMPARE(result.exitCode, 1);
    QCOMPARE(backend.rescanCalls, 1);
    QCOMPARE(backend.serviceCalls, 0);
    QCOMPARE(result.standardOutput, QStringLiteral("scan_status=error\n"));
    QCOMPARE(result.standardError, QString{});
    QVERIFY(!result.standardOutput.contains(QStringLiteral("sentinel")));
}

void RescanCliTest::installedKf6KeepsTheEffectiveTriggerCaseInsensitive()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    QVERIFY(bus.isConnected());
    ConfigService service;
    QVERIFY(bus.registerObject(QStringLiteral("/runner"), &service,
                               QDBusConnection::ExportAllSlots));
    QVERIFY(bus.registerService(QStringLiteral("org.remminakrunner.KRunner")));

    KRunner::AbstractRunnerTest runnerTest;
    runnerTest.initProperties();
    QTRY_COMPARE_WITH_TIMEOUT(service.calls, 1, 3000);
    QVERIFY(runnerTest.runner != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(
        runnerTest.runner->matchRegex().match(QStringLiteral("REM")).hasMatch(), 3000);
    const QRegularExpression effective = runnerTest.runner->matchRegex();
    QVERIFY(effective.isValid());
    for (const QString &accepted : {QStringLiteral("rem"),
                                    QStringLiteral("REM"),
                                    QStringLiteral("rem host"),
                                    QStringLiteral("ReM new")}) {
        QVERIFY2(effective.match(accepted).hasMatch(), qPrintable(effective.pattern()));
    }
    for (const QString &rejected : {QStringLiteral("remote"),
                                    QStringLiteral("x rem"),
                                    QStringLiteral(" rem")}) {
        QVERIFY2(!effective.match(rejected).hasMatch(), qPrintable(effective.pattern()));
    }

    runnerTest.manager.reset();
    QVERIFY(bus.unregisterService(QStringLiteral("org.remminakrunner.KRunner")));
    bus.unregisterObject(QStringLiteral("/runner"));
}

QTEST_GUILESS_MAIN(RescanCliTest)

#include "test_rescan_cli.moc"
