// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include "core/instance_scanner.h"
#include "core/selection_store.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

struct RegistrySnapshot {
    QList<RemminaInstance> instances;
    QString selectedId;
    QStringList failedBackends;
};

class InstanceRegistry {
public:
    // Both dependencies are non-owning and must outlive the registry.
    InstanceRegistry(InstanceScanSource &scanSource, SelectionStore &selectionStore);

    RegistrySnapshot rescanAndRepair();
    [[nodiscard]] RegistrySnapshot snapshot() const;
    bool select(QStringView id);

private:
    InstanceScanSource &scanSource_;
    SelectionStore &selectionStore_;
    RegistrySnapshot snapshot_;
};
