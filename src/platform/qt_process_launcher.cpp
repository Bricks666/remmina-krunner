// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include "platform/qt_process_launcher.h"

#include <QProcess>

bool QtProcessLauncher::startDetached(const LaunchRequest &request)
{
    QProcess process;
    process.setProgram(request.program);
    process.setArguments(request.arguments);
    process.setProcessEnvironment(request.environment);
    return process.startDetached();
}
