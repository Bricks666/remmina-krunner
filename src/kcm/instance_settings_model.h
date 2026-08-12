// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include "core/remmina_instance.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

class InstanceRegistryControlSource;

struct SettingsInstanceItem {
  QString id;
  QString displayText;
  InstanceKind kind;
};

class InstanceSettingsModel {
public:
  // The registry is non-owning, same-thread confined, and must outlive the model.
  explicit InstanceSettingsModel(InstanceRegistryControlSource &registry);

  bool load();
  [[nodiscard]] QList<SettingsInstanceItem> items() const;
  [[nodiscard]] QString selectedId() const;
  [[nodiscard]] QString persistedId() const;
  [[nodiscard]] QStringList failedBackends() const;
  [[nodiscard]] bool hasInstances() const;
  [[nodiscard]] bool isDirty() const;
  bool selectPending(QStringView id);
  bool save();
  void defaults();

private:
  void clear();
  [[nodiscard]] bool isUniqueAvailableId(QStringView id) const;

  InstanceRegistryControlSource &registry_;
  QList<RemminaInstance> instances_;
  QList<SettingsInstanceItem> items_;
  QString selectedId_;
  QString persistedId_;
  QStringList failedBackends_;
};
