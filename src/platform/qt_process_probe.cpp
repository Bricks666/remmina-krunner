// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include "platform/qt_process_probe.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QProcess>
#include <QThread>

#include <memory>
#include <utility>

namespace {

constexpr int reapingGraceMilliseconds = 1000;

void deferReaping(std::unique_ptr<QProcess> process)
{
    QCoreApplication *application = QCoreApplication::instance();
    Q_ASSERT(application != nullptr);
    Q_ASSERT(!QCoreApplication::closingDown());
    Q_ASSERT(QThread::currentThread() == application->thread());

    QProcess *pendingProcess = process.release();
    QObject::connect(pendingProcess,
                     &QProcess::finished,
                     pendingProcess,
                     &QObject::deleteLater);
}

void killAndReapOrDefer(std::unique_ptr<QProcess> &process)
{
    if (process->state() == QProcess::NotRunning) {
        return;
    }
    process->kill();
    if (!process->waitForFinished(reapingGraceMilliseconds)
        && process->state() != QProcess::NotRunning) {
        deferReaping(std::move(process));
    }
}

bool appendBoundedStandardOutput(QProcess &process, QByteArray &output)
{
    const qsizetype remaining = QtProcessProbe::maximumStandardOutputBytes - output.size();
    const QByteArray chunk = process.read(remaining + 1);
    if (chunk.size() > remaining) {
        return false;
    }
    output.append(chunk);
    return true;
}

ProbeResult stoppedProbeResult(ProbeResult::Status status, std::unique_ptr<QProcess> &process)
{
    killAndReapOrDefer(process);
    return {.status = status, .standardOutput = {}};
}

} // namespace

ProbeResult QtProcessProbe::run(const QString &executable, const QStringList &arguments)
{
    QCoreApplication *application = QCoreApplication::instance();
    if (application == nullptr || QCoreApplication::closingDown()
        || QThread::currentThread() != application->thread()) {
        return {.status = ProbeResult::Status::Failed, .standardOutput = {}};
    }

    auto process = std::make_unique<QProcess>();
    process->setProgram(executable);
    process->setArguments(arguments);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    process->setStandardErrorFile(QProcess::nullDevice());
    process->setReadChannel(QProcess::StandardOutput);

    QDeadlineTimer deadline(timeoutMilliseconds);
    process->start(QIODevice::ReadOnly);
    const int startWait = deadline.remainingTime();
    if (startWait <= 0 || !process->waitForStarted(startWait)) {
        const ProbeResult::Status status = deadline.hasExpired()
            ? ProbeResult::Status::TimedOut
            : ProbeResult::Status::Failed;
        return stoppedProbeResult(status, process);
    }

    QByteArray output;
    output.reserve(maximumStandardOutputBytes);
    while (process->state() != QProcess::NotRunning) {
        if (!appendBoundedStandardOutput(*process, output)) {
            return stoppedProbeResult(ProbeResult::Status::OutputTooLarge, process);
        }

        const int remainingTime = deadline.remainingTime();
        if (remainingTime <= 0) {
            return stoppedProbeResult(ProbeResult::Status::TimedOut, process);
        }
        if (!process->waitForReadyRead(remainingTime)
            && process->state() != QProcess::NotRunning) {
            if (deadline.hasExpired()) {
                return stoppedProbeResult(ProbeResult::Status::TimedOut, process);
            }
            return stoppedProbeResult(ProbeResult::Status::Failed, process);
        }
    }

    if (!appendBoundedStandardOutput(*process, output)) {
        return stoppedProbeResult(ProbeResult::Status::OutputTooLarge, process);
    }
    if (process->exitStatus() != QProcess::NormalExit || process->exitCode() != 0) {
        return {.status = ProbeResult::Status::Failed, .standardOutput = {}};
    }
    return {.status = ProbeResult::Status::Success, .standardOutput = std::move(output)};
}
