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

class InstanceRegistryControlSource : public InstanceRegistrySource {
public:
    ~InstanceRegistryControlSource() override = default;
    // Implementations are non-owning service dependencies and are called on
    // their owning thread.
    [[nodiscard]] virtual RegistrySnapshot rescanAndRepair() = 0;
    [[nodiscard]] virtual bool select(QStringView id) = 0;
};

class InstanceRegistry final : public InstanceRegistryControlSource {
public:
    // Both dependencies are non-owning, same-thread confined, and must outlive
    // the registry.
    InstanceRegistry(InstanceScanSource &scanSource, SelectionStore &selectionStore);

    RegistrySnapshot rescanAndRepair() override;
    [[nodiscard]] RegistrySnapshot snapshot() const override;
    bool select(QStringView id) override;

private:
    InstanceScanSource &scanSource_;
    SelectionStore &selectionStore_;
    RegistrySnapshot snapshot_;
    // Whether snapshot_.selectedId is known to match the persisted store.
    bool selectionSynchronized_ = false;
};
