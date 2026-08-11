// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

struct ProbeResult {
    enum class Status {
        Success,
        Failed,
        TimedOut,
        OutputTooLarge,
    } status;
    QByteArray standardOutput;
};

class ProcessProbe {
public:
    virtual ~ProcessProbe() = default;
    virtual ProbeResult run(const QString &executable, const QStringList &arguments) = 0;
};
