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

class InstanceRegistrySource {
public:
    virtual ~InstanceRegistrySource() = default;
    [[nodiscard]] virtual RegistrySnapshot snapshot() const = 0;
};

class InstanceRegistry final : public InstanceRegistrySource {
public:
    // Both dependencies are non-owning and must outlive the registry.
    InstanceRegistry(InstanceScanSource &scanSource, SelectionStore &selectionStore);

    RegistrySnapshot rescanAndRepair();
    [[nodiscard]] RegistrySnapshot snapshot() const override;
    bool select(QStringView id);

private:
    InstanceScanSource &scanSource_;
    SelectionStore &selectionStore_;
    RegistrySnapshot snapshot_;
    // Whether snapshot_.selectedId is known to match the persisted store.
    bool selectionSynchronized_ = false;
};
