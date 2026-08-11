// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include <QtTest>

#include "platform/qt_process_probe.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>

namespace {

QString readPid(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromLatin1(file.readAll()).trimmed();
}

void verifyProcessWasReaped(const QString &pidFile)
{
    const QString pid = readPid(pidFile);
    QVERIFY(!pid.isEmpty());
    QVERIFY(!QFileInfo::exists(QStringLiteral("/proc/") + pid));
}

} // namespace

class QtProcessProbeTest : public QObject {
    Q_OBJECT

private slots:
    void preservesArgumentsWithoutShellParsing();
    void rejectsWorkerThreadWithoutSpawning();
    void discardsStderrWithoutDeadlocking();
    void classifiesStartExitAndCrashFailures();
    void killsTimedOutProcesses();
    void acceptsOutputAtExactLimit();
    void rejectsFirstBytePastLimitBeforeExit();
    void capsOversizedStandardOutput();
};

void QtProcessProbeTest::preservesArgumentsWithoutShellParsing()
{
    QtProcessProbe probe;
    const QString inertArgument = QStringLiteral("path with spaces;$(not-a-command)\nsecond line");

    const ProbeResult result =
        probe.run(QStringLiteral(PROCESS_PROBE_HELPER_PATH),
                  {QStringLiteral("echo"), inertArgument});

    QCOMPARE(result.status, ProbeResult::Status::Success);
    QCOMPARE(result.standardOutput, inertArgument.toUtf8());
}

void QtProcessProbeTest::rejectsWorkerThreadWithoutSpawning()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString markerPath =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("spawned.marker"));
    ProbeResult workerResult{
        .status = ProbeResult::Status::Success,
        .standardOutput = QByteArray("not updated"),
    };
    QThread *worker = QThread::create([&workerResult, &markerPath] {
        QtProcessProbe probe;
        workerResult = probe.run(QStringLiteral(PROCESS_PROBE_HELPER_PATH),
                                 {QStringLiteral("mark"), markerPath});
    });

    worker->start();
    QVERIFY(worker->wait(QtProcessProbe::timeoutMilliseconds + 1000));
    delete worker;

    QCOMPARE(workerResult.status, ProbeResult::Status::Failed);
    QVERIFY(workerResult.standardOutput.isEmpty());
    QVERIFY(!QFileInfo::exists(markerPath));
}

void QtProcessProbeTest::discardsStderrWithoutDeadlocking()
{
    QtProcessProbe probe;

    const ProbeResult result = probe.run(QStringLiteral(PROCESS_PROBE_HELPER_PATH),
                                         {QStringLiteral("stderr-flood")});

    QCOMPARE(result.status, ProbeResult::Status::Success);
    QCOMPARE(result.standardOutput, QByteArray("safe stdout"));
}

void QtProcessProbeTest::classifiesStartExitAndCrashFailures()
{
    QtProcessProbe probe;
    const QString missingExecutable =
        QDir(QDir::tempPath()).filePath(QStringLiteral("definitely-missing-process-probe"));

    QCOMPARE(probe.run(missingExecutable, {}).status, ProbeResult::Status::Failed);
    QCOMPARE(probe.run(QStringLiteral(PROCESS_PROBE_HELPER_PATH),
                       {QStringLiteral("nonzero")})
                 .status,
             ProbeResult::Status::Failed);
    QCOMPARE(probe.run(QStringLiteral(PROCESS_PROBE_HELPER_PATH), {QStringLiteral("crash")})
                 .status,
             ProbeResult::Status::Failed);
}

void QtProcessProbeTest::killsTimedOutProcesses()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString pidFile =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("timeout.pid"));
    QtProcessProbe probe;

    const ProbeResult result = probe.run(QStringLiteral(PROCESS_PROBE_HELPER_PATH),
                                         {QStringLiteral("timeout"), pidFile});

    QCOMPARE(result.status, ProbeResult::Status::TimedOut);
    QVERIFY(result.standardOutput.isEmpty());
    verifyProcessWasReaped(pidFile);
}

void QtProcessProbeTest::acceptsOutputAtExactLimit()
{
    QtProcessProbe probe;

    const ProbeResult result = probe.run(QStringLiteral(PROCESS_PROBE_HELPER_PATH),
                                         {QStringLiteral("exact-limit")});

    QCOMPARE(result.status, ProbeResult::Status::Success);
    QCOMPARE(result.standardOutput.size(), QtProcessProbe::maximumStandardOutputBytes);
}

void QtProcessProbeTest::rejectsFirstBytePastLimitBeforeExit()
{
    QtProcessProbe probe;
    QElapsedTimer elapsed;
    elapsed.start();

    const ProbeResult result = probe.run(QStringLiteral(PROCESS_PROBE_HELPER_PATH),
                                         {QStringLiteral("limit-plus-one")});

    QCOMPARE(result.status, ProbeResult::Status::OutputTooLarge);
    QVERIFY(result.standardOutput.isEmpty());
    QVERIFY(elapsed.elapsed() < QtProcessProbe::timeoutMilliseconds);
}

void QtProcessProbeTest::capsOversizedStandardOutput()
{
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());
    const QString pidFile =
        QDir(temporaryDirectory.path()).filePath(QStringLiteral("oversize.pid"));
    QtProcessProbe probe;

    const ProbeResult result = probe.run(QStringLiteral(PROCESS_PROBE_HELPER_PATH),
                                         {QStringLiteral("oversize"), pidFile});

    QCOMPARE(result.status, ProbeResult::Status::OutputTooLarge);
    QVERIFY(result.standardOutput.size() <= QtProcessProbe::maximumStandardOutputBytes);
    verifyProcessWasReaped(pidFile);
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QtProcessProbeTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_qt_process_probe.moc"
