// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include "core/instance_registry.h"

#include "core/selection_policy.h"

#include <utility>

InstanceRegistry::InstanceRegistry(InstanceScanSource &scanSource,
                                   SelectionStore &selectionStore)
    : scanSource_(scanSource)
    , selectionStore_(selectionStore)
{
}

RegistrySnapshot InstanceRegistry::rescanAndRepair()
{
    InstanceScanResult scanResult = scanSource_.scan();
    SelectionDecision selection =
        validateSelection(scanResult.instances, selectionStore_.selectedId());
    if (selection.changed) {
        selectionStore_.writeSelectedId(selection.selectedId);
    }

    snapshot_ = {
        .instances = std::move(scanResult.instances),
        .selectedId = std::move(selection.selectedId),
        .failedBackends = std::move(scanResult.failedBackends),
    };
    return snapshot_;
}

RegistrySnapshot InstanceRegistry::snapshot() const
{
    return snapshot_;
}

bool InstanceRegistry::select(QStringView id)
{
    if (id.isEmpty()) {
        return false;
    }

    bool present = false;
    for (const RemminaInstance &instance : snapshot_.instances) {
        if (QStringView{instance.id} == id) {
            present = true;
            break;
        }
    }
    if (!present) {
        return false;
    }
    if (QStringView{snapshot_.selectedId} == id) {
        return true;
    }
    if (!selectionStore_.writeSelectedId(id)) {
        return false;
    }

    snapshot_.selectedId = id.toString();
    return true;
}
