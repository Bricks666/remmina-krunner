// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include "core/remmina_instance.h"
#include "platform/process_probe.h"

#include <QList>
#include <QString>
#include <QStringList>

struct ScanEnvironment {
    QStringList pathEntries;
    QString flatpakExecutable;
    QString snapLauncher;
    QString userHome;
    QString snapMountRoot = QStringLiteral("/snap");
};

struct InstanceScanResult {
    QList<RemminaInstance> instances;
    QStringList failedBackends;
};

class InstanceScanSource {
public:
    virtual ~InstanceScanSource() = default;
    [[nodiscard]] virtual InstanceScanResult scan() const = 0;
};

class InstanceScanner final : public InstanceScanSource {
public:
    InstanceScanner(ProcessProbe &probe, ScanEnvironment environment);
    [[nodiscard]] InstanceScanResult scan() const override;

private:
    ProcessProbe &probe_;
    ScanEnvironment environment_;
};
