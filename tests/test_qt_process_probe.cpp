// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include <QtTest>

#include "platform/qt_process_probe.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

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
    void discardsStderrWithoutDeadlocking();
    void classifiesStartExitAndCrashFailures();
    void killsTimedOutProcesses();
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

QTEST_APPLESS_MAIN(QtProcessProbeTest)

#include "test_qt_process_probe.moc"
