// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include "kcm/remmina_runner_config.h"

#include "ui_remmina_runner_config.h"

#include "core/instance_registry.h"
#include "core/instance_scanner.h"
#include "core/kconfig_selection_store.h"
#include "kcm/instance_settings_model.h"
#include "platform/qt_process_probe.h"

#include <KLocalizedString>

#include <QComboBox>
#include <QDir>
#include <QSignalBlocker>
#include <QStandardPaths>

#include <utility>

namespace {

ScanEnvironment currentEnvironment() {
  ScanEnvironment environment;
  environment.pathEntries = qEnvironmentVariable("PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts);
  environment.flatpakExecutable = QStandardPaths::findExecutable(QStringLiteral("flatpak"), environment.pathEntries);
  environment.snapLauncher = QStringLiteral("/snap/bin/remmina");
  environment.userHome = QDir::homePath();
  return environment;
}

QStringList backendDisplayNames(const QStringList &backends) {
  QStringList names;
  for (const QString &backend : backends) {
    QString name;
    if (backend.compare(QStringLiteral("flatpak"), Qt::CaseInsensitive) == 0) {
      name = i18nc("@item Remmina packaging backend", "Flatpak");
    } else if (backend.compare(QStringLiteral("snap"), Qt::CaseInsensitive) == 0) {
      name = i18nc("@item Remmina packaging backend", "Snap");
    } else if (backend.compare(QStringLiteral("native"), Qt::CaseInsensitive) == 0) {
      name = i18nc("@item Remmina packaging backend", "Native");
    }
    if (!name.isEmpty() && !names.contains(name)) {
      names.append(name);
    }
  }
  return names;
}

} // namespace

struct RemminaRunnerConfig::ProductionDependencies {
  ProductionDependencies() : scanner(probe, currentEnvironment()), registry(scanner, store), model(registry) {}

  QtProcessProbe probe;
  InstanceScanner scanner;
  KConfigSelectionStore store;
  InstanceRegistry registry;
  InstanceSettingsModel model;
};

RemminaRunnerConfig::RemminaRunnerConfig(QWidget *parent, const KPluginMetaData &data)
    : RemminaRunnerConfig(std::make_unique<ProductionDependencies>(), parent, data) {}

RemminaRunnerConfig::RemminaRunnerConfig(InstanceSettingsModel &model, QWidget *parent, const KPluginMetaData &data)
    : KCModule(parent, data), model_(&model) {
  initializeUi();
}

RemminaRunnerConfig::RemminaRunnerConfig(std::unique_ptr<ProductionDependencies> dependencies, QWidget *parent,
                                         const KPluginMetaData &data)
    : KCModule(parent, data), dependencies_(std::move(dependencies)), model_(&dependencies_->model) {
  initializeUi();
}

RemminaRunnerConfig::~RemminaRunnerConfig() = default;

void RemminaRunnerConfig::load() {
  lastScanSucceeded_ = model_->load();
  populate();
  updateStatus();
  unmanagedWidgetChangeState(false);
  setNeedsSave(false);
}

void RemminaRunnerConfig::save() {
  if (!model_->save()) {
    ui_->statusLabel->setText(i18nc("@info", "Could not save the Remmina installation selection."));
    setDirtyState();
    return;
  }

  restorePendingSelection();
  updateStatus();
  setDirtyState();
}

void RemminaRunnerConfig::defaults() {
  try {
    model_->defaults();
  } catch (...) {
    updateStatus();
    setDirtyState();
    return;
  }
  restorePendingSelection();
  updateStatus();
  setDirtyState();
}

void RemminaRunnerConfig::initializeUi() {
  ui_ = std::make_unique<Ui::RemminaRunnerConfig>();
  ui_->setupUi(widget());
  setButtons(Default | Apply);
  setSupportsInstantApply(false);
  connect(ui_->instanceCombo, &QComboBox::currentIndexChanged, this, &RemminaRunnerConfig::instanceChanged);
}

void RemminaRunnerConfig::populate() {
  populating_ = true;
  const QSignalBlocker blocker(ui_->instanceCombo);
  ui_->instanceCombo->clear();
  for (const SettingsInstanceItem &item : model_->items()) {
    ui_->instanceCombo->addItem(item.displayText, item.id);
  }
  restorePendingSelection();
  ui_->instanceCombo->setEnabled(model_->hasInstances());
  populating_ = false;
}

void RemminaRunnerConfig::restorePendingSelection() {
  const QSignalBlocker blocker(ui_->instanceCombo);
  const QString pendingId = model_->selectedId();
  const int selection = pendingId.isEmpty() ? -1 : ui_->instanceCombo->findData(pendingId);
  ui_->instanceCombo->setCurrentIndex(selection);
}

void RemminaRunnerConfig::updateStatus() {
  if (!lastScanSucceeded_) {
    ui_->statusLabel->setText(i18nc("@info", "Could not scan for Remmina installations."));
    return;
  }
  if (!model_->hasInstances()) {
    ui_->statusLabel->setText(i18nc("@info", "No Remmina installations found."));
    return;
  }

  const QStringList failedNames = backendDisplayNames(model_->failedBackends());
  if (!failedNames.isEmpty()) {
    ui_->statusLabel->setText(i18nc("@info", "Some installation types could not be checked: %1.",
                                    failedNames.join(i18nc("@item separator", ", "))));
    return;
  }
  ui_->statusLabel->clear();
}

void RemminaRunnerConfig::setDirtyState() {
  const bool dirty = model_->isDirty();
  unmanagedWidgetChangeState(dirty);
  setNeedsSave(dirty);
}

void RemminaRunnerConfig::instanceChanged(int index) {
  if (populating_) {
    return;
  }
  if (index < 0 || !model_->selectPending(ui_->instanceCombo->itemData(index).toString())) {
    restorePendingSelection();
  }
  updateStatus();
  setDirtyState();
}
