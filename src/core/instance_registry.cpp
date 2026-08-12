// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include "core/instance_registry.h"

#include "core/selection_policy.h"

#include <utility>

namespace {

QString backendForStableId(QStringView id) {
  if (id.startsWith(QLatin1StringView("flatpak:"))) {
    return QStringLiteral("flatpak");
  }
  if (id.startsWith(QLatin1StringView("snap:"))) {
    return QStringLiteral("snap");
  }
  return {};
}

} // namespace

InstanceRegistry::InstanceRegistry(InstanceScanSource &scanSource, SelectionStore &selectionStore)
    : scanSource_(scanSource), selectionStore_(selectionStore) {}

RegistrySnapshot InstanceRegistry::rescanAndRepair() {
  InstanceScanResult scanResult = scanSource_.scan();
  const QString savedId = selectionStore_.selectedId();
  SelectionDecision selection = validateSelection(scanResult.instances, savedId);
  if (!selection.changed) {
    selectionSynchronized_ = true;
  } else {
    const QString failedSavedBackend = backendForStableId(savedId);
    const bool savedSelectionMayBeTemporarilyUnavailable =
        !failedSavedBackend.isEmpty() && scanResult.failedBackends.contains(failedSavedBackend);
    selectionSynchronized_ =
        !savedSelectionMayBeTemporarilyUnavailable && selectionStore_.writeSelectedId(selection.selectedId);
  }

  snapshot_ = {
      .instances = std::move(scanResult.instances),
      .selectedId = std::move(selection.selectedId),
      .failedBackends = std::move(scanResult.failedBackends),
  };
  return snapshot_;
}

RegistrySnapshot InstanceRegistry::snapshot() const { return snapshot_; }

bool InstanceRegistry::select(QStringView id) {
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
  if (QStringView{snapshot_.selectedId} == id && selectionSynchronized_) {
    return true;
  }
  if (!selectionStore_.writeSelectedId(id)) {
    return false;
  }

  snapshot_.selectedId = id.toString();
  selectionSynchronized_ = true;
  return true;
}
