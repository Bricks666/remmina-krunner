// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

struct LaunchRequest {
    QString program;
    QStringList arguments;
    QProcessEnvironment environment;
};

class ProcessLauncher {
public:
    virtual ~ProcessLauncher() = default;
    virtual bool startDetached(const LaunchRequest &request) = 0;
};
