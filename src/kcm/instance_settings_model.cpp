// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include "kcm/instance_settings_model.h"

#include "core/instance_registry.h"
#include "core/selection_policy.h"

#include <KLocalizedString>

#include <utility>

namespace {

QString oneLine(QStringView text) {
  QString result;
  result.reserve(text.size());
  for (const QChar character : text) {
    result.append(character.isPrint() ? character : u' ');
  }
  return result;
}

QString flatpakIdentity(const RemminaInstance &instance) {
  QString installation;
  QString ref;
  for (qsizetype index = 0; index < instance.launcherPrefix.size(); ++index) {
    const QString &argument = instance.launcherPrefix.at(index);
    if (argument == QStringLiteral("--user")) {
      installation = i18nc("@item:inlistbox Flatpak installation scope", "user");
    } else if (argument == QStringLiteral("--system")) {
      installation = i18nc("@item:inlistbox Flatpak installation scope", "system");
    } else if (argument.startsWith(QLatin1StringView("--installation="))) {
      installation = oneLine(QStringView{argument}.sliced(15));
    }
    if (argument == QStringLiteral("run") && index + 1 < instance.launcherPrefix.size()) {
      ref = oneLine(instance.launcherPrefix.at(index + 1));
    }
  }
  if (ref.isEmpty() && !instance.launcherPrefix.isEmpty()) {
    ref = oneLine(instance.launcherPrefix.constLast());
  }
  if (installation.isEmpty()) {
    return ref;
  }
  if (ref.isEmpty()) {
    return installation;
  }
  return i18nc("@item:inlistbox Flatpak installation identity", "%1 · %2", installation, ref);
}

QString displayText(const RemminaInstance &instance) {
  switch (instance.kind) {
  case InstanceKind::Native:
    return i18nc("@item:inlistbox Remmina packaging and executable", "Native — %1", oneLine(instance.executable));
  case InstanceKind::Flatpak:
    return i18nc("@item:inlistbox Remmina packaging and Flatpak identity", "Flatpak — %1", flatpakIdentity(instance));
  case InstanceKind::Snap:
    return i18nc("@item:inlistbox Remmina packaging and executable", "Snap — %1", oneLine(instance.executable));
  }
  return {};
}

} // namespace

InstanceSettingsModel::InstanceSettingsModel(InstanceRegistryControlSource &registry) : registry_(registry) {}

bool InstanceSettingsModel::load() {
  try {
    RegistrySnapshot snapshot = registry_.rescanAndRepair();
    QList<SettingsInstanceItem> newItems;
    newItems.reserve(snapshot.instances.size());
    for (const RemminaInstance &instance : std::as_const(snapshot.instances)) {
      newItems.append({instance.id, displayText(instance), instance.kind});
    }

    instances_ = std::move(snapshot.instances);
    items_ = std::move(newItems);
    failedBackends_ = std::move(snapshot.failedBackends);
    persistedId_ = isUniqueAvailableId(snapshot.selectedId) ? snapshot.selectedId : QString{};
    selectedId_ = persistedId_;
    return true;
  } catch (...) {
    clear();
    return false;
  }
}

QList<SettingsInstanceItem> InstanceSettingsModel::items() const { return items_; }

QString InstanceSettingsModel::selectedId() const { return selectedId_; }

QString InstanceSettingsModel::persistedId() const { return persistedId_; }

QStringList InstanceSettingsModel::failedBackends() const { return failedBackends_; }

bool InstanceSettingsModel::hasInstances() const { return !instances_.isEmpty(); }

bool InstanceSettingsModel::isDirty() const { return selectedId_ != persistedId_; }

bool InstanceSettingsModel::selectPending(QStringView id) {
  if (!isUniqueAvailableId(id)) {
    return false;
  }
  selectedId_ = id.toString();
  return true;
}

bool InstanceSettingsModel::save() {
  if (!isDirty()) {
    return true;
  }
  if (!isUniqueAvailableId(selectedId_)) {
    return false;
  }
  try {
    if (!registry_.select(selectedId_)) {
      return false;
    }
    persistedId_ = selectedId_;
    return true;
  } catch (...) {
    return false;
  }
}

void InstanceSettingsModel::defaults() {
  if (instances_.isEmpty()) {
    selectedId_.clear();
    return;
  }
  selectedId_ = validateSelection(instances_, QStringView{}).selectedId;
}

void InstanceSettingsModel::clear() {
  instances_.clear();
  items_.clear();
  selectedId_.clear();
  persistedId_.clear();
  failedBackends_.clear();
}

bool InstanceSettingsModel::isUniqueAvailableId(QStringView id) const {
  if (id.isEmpty()) {
    return false;
  }
  int matches = 0;
  for (const RemminaInstance &instance : instances_) {
    if (QStringView{instance.id} == id && ++matches > 1) {
      return false;
    }
  }
  return matches == 1;
}
