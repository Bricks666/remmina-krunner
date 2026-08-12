// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include <QtTest>

#include "kcm/remmina_runner_config.h"

#include "core/instance_registry.h"
#include "kcm/instance_settings_model.h"

#include <KCModule>
#include <KPluginFactory>
#include <KPluginMetaData>
#include <KPluginModel>

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QTemporaryDir>

#include <stdexcept>
#include <utility>

namespace {

RemminaInstance native(QString id = QStringLiteral("native:/usr/bin/remmina"),
                       QString executable = QStringLiteral("/usr/bin/remmina")) {
  return {
      .id = std::move(id),
      .kind = InstanceKind::Native,
      .displayName = QStringLiteral("private-version-metadata"),
      .executable = std::move(executable),
      .launcherPrefix = {},
      .profiles = {.configHome = QStringLiteral("/secret/native/profiles")},
  };
}

RemminaInstance flatpak() {
  return {
      .id = QStringLiteral("flatpak:user:org.remmina.Remmina/x86_64/stable"),
      .kind = InstanceKind::Flatpak,
      .displayName = QStringLiteral("private-revision-metadata"),
      .executable = QStringLiteral("/usr/bin/flatpak"),
      .launcherPrefix = {QStringLiteral("--user"), QStringLiteral("run"),
                         QStringLiteral("org.remmina.Remmina/x86_64/stable")},
      .profiles = {.dataHome = QStringLiteral("/secret/flatpak/profiles")},
  };
}

RemminaInstance snap() {
  return {
      .id = QStringLiteral("snap:remmina"),
      .kind = InstanceKind::Snap,
      .displayName = QStringLiteral("private-snap-revision"),
      .executable = QStringLiteral("/snap/bin/remmina"),
      .launcherPrefix = {},
      .profiles = {.legacyHome = QStringLiteral("/secret/snap/profiles")},
  };
}

class FakeRegistry final : public InstanceRegistryControlSource {
public:
  RegistrySnapshot snapshot() const override { return current; }

  RegistrySnapshot rescanAndRepair() override {
    ++rescanCalls;
    if (throwRescan) {
      throw std::runtime_error("secret scan failure");
    }
    if (!rescans.isEmpty()) {
      current = rescans.at(qMin(rescanCalls - 1, rescans.size() - 1));
    }
    return current;
  }

  bool select(QStringView id) override {
    ++selectCalls;
    selectedArguments.append(id.toString());
    if (throwSelect) {
      throw std::runtime_error("secret save failure");
    }
    if (selectSucceeds) {
      current.selectedId = id.toString();
    }
    return selectSucceeds;
  }

  QList<RegistrySnapshot> rescans;
  RegistrySnapshot current;
  int rescanCalls = 0;
  int selectCalls = 0;
  QStringList selectedArguments;
  bool throwRescan = false;
  bool throwSelect = false;
  bool selectSucceeds = true;
};

QComboBox *combo(RemminaRunnerConfig &module) {
  QComboBox *result = module.widget()->findChild<QComboBox *>(QStringLiteral("instanceCombo"));
  Q_ASSERT(result);
  return result;
}

QLabel *status(RemminaRunnerConfig &module) {
  QLabel *result = module.widget()->findChild<QLabel *>(QStringLiteral("statusLabel"));
  Q_ASSERT(result);
  return result;
}

class EnvironmentGuard {
public:
  explicit EnvironmentGuard(const QList<QByteArray> &names) {
    for (const QByteArray &name : names) {
      values_.append({name, qgetenv(name.constData()), qEnvironmentVariableIsSet(name.constData())});
    }
  }

  ~EnvironmentGuard() {
    for (const Value &value : std::as_const(values_)) {
      if (value.wasSet) {
        qputenv(value.name.constData(), value.value);
      } else {
        qunsetenv(value.name.constData());
      }
    }
  }

private:
  struct Value {
    QByteArray name;
    QByteArray value;
    bool wasSet;
  };
  QList<Value> values_;
};

} // namespace

class RemminaRunnerConfigTest : public QObject {
  Q_OBJECT

private slots:
  void loadPopulatesPackagingIdentityAndRescansEveryTime();
  void emptyAndFailedScansDisableTheControlWithClearStatus();
  void partialBackendErrorsKeepResultsAndNameOnlyBackendTypes();
  void userChangesTrackDirtyStateAndRejectUnknownData();
  void invalidEmptySelectedIdHighlightsNoItem();
  void savePersistsOnlyOnApplyAndKeepsFailureDirty();
  void defaultsChangePendingSelectionWithoutPersistence();
  void widgetContainsNoProfileControlsOrProfileContent();
  void noJsonPluginFactoryLoadsStandardConstructorInIsolation();
  void absoluteConfigModuleIsDiscoverableOutsideQtPluginPaths();
};

void RemminaRunnerConfigTest::loadPopulatesPackagingIdentityAndRescansEveryTime() {
  FakeRegistry registry;
  registry.rescans = {
      {{native(), flatpak(), snap()}, QStringLiteral("flatpak:user:org.remmina.Remmina/x86_64/stable"), {}},
      {{snap()}, QStringLiteral("snap:remmina"), {}},
  };
  InstanceSettingsModel model(registry);
  QWidget host;
  RemminaRunnerConfig module(model, &host, {});

  module.load();
  QCOMPARE(registry.rescanCalls, 1);
  QCOMPARE(combo(module)->count(), 3);
  QCOMPARE(combo(module)->itemData(0).toString(), QStringLiteral("native:/usr/bin/remmina"));
  QVERIFY(combo(module)->itemText(0).contains(QStringLiteral("Native")));
  QVERIFY(combo(module)->itemText(0).contains(QStringLiteral("/usr/bin/remmina")));
  QVERIFY(combo(module)->itemText(1).contains(QStringLiteral("Flatpak")));
  QVERIFY(combo(module)->itemText(1).contains(QStringLiteral("user")));
  QVERIFY(combo(module)->itemText(1).contains(QStringLiteral("org.remmina.Remmina/x86_64/stable")));
  QVERIFY(combo(module)->itemText(2).contains(QStringLiteral("Snap")));
  QVERIFY(combo(module)->itemText(2).contains(QStringLiteral("/snap/bin/remmina")));
  QCOMPARE(combo(module)->currentData().toString(), QStringLiteral("flatpak:user:org.remmina.Remmina/x86_64/stable"));
  QVERIFY(!module.needsSave());

  module.load();
  QCOMPARE(registry.rescanCalls, 2);
  QCOMPARE(combo(module)->count(), 1);
  QCOMPARE(combo(module)->currentData().toString(), QStringLiteral("snap:remmina"));
  QVERIFY(!module.needsSave());
}

void RemminaRunnerConfigTest::emptyAndFailedScansDisableTheControlWithClearStatus() {
  FakeRegistry empty;
  empty.rescans = {{{}, {}, {}}};
  InstanceSettingsModel emptyModel(empty);
  QWidget emptyHost;
  RemminaRunnerConfig emptyModule(emptyModel, &emptyHost, {});
  emptyModule.load();
  QCOMPARE(combo(emptyModule)->count(), 0);
  QVERIFY(!combo(emptyModule)->isEnabled());
  QCOMPARE(status(emptyModule)->text(), QStringLiteral("No Remmina installations found."));

  FakeRegistry failed;
  failed.throwRescan = true;
  InstanceSettingsModel failedModel(failed);
  QWidget failedHost;
  RemminaRunnerConfig failedModule(failedModel, &failedHost, {});
  failedModule.load();
  QCOMPARE(failed.rescanCalls, 1);
  QCOMPARE(combo(failedModule)->count(), 0);
  QVERIFY(!combo(failedModule)->isEnabled());
  QCOMPARE(status(failedModule)->text(), QStringLiteral("Could not scan for Remmina installations."));
  QVERIFY(!failedModule.needsSave());
}

void RemminaRunnerConfigTest::partialBackendErrorsKeepResultsAndNameOnlyBackendTypes() {
  FakeRegistry registry;
  registry.rescans = {
      {{native()}, QStringLiteral("native:/usr/bin/remmina"), {QStringLiteral("flatpak"), QStringLiteral("snap")}}};
  InstanceSettingsModel model(registry);
  QWidget host;
  RemminaRunnerConfig module(model, &host, {});

  module.load();
  QCOMPARE(combo(module)->count(), 1);
  QVERIFY(combo(module)->isEnabled());
  QVERIFY(status(module)->text().contains(QStringLiteral("Flatpak")));
  QVERIFY(status(module)->text().contains(QStringLiteral("Snap")));
  QVERIFY(!status(module)->text().contains(QStringLiteral("native:/")));
  QVERIFY(!status(module)->text().contains(QStringLiteral("/usr/bin")));
}

void RemminaRunnerConfigTest::userChangesTrackDirtyStateAndRejectUnknownData() {
  FakeRegistry registry;
  registry.rescans = {{{native(), snap()}, QStringLiteral("native:/usr/bin/remmina"), {}}};
  InstanceSettingsModel model(registry);
  QWidget host;
  RemminaRunnerConfig module(model, &host, {});
  module.load();
  QSignalSpy needsSaveChanged(&module, &KCModule::needsSaveChanged);

  combo(module)->setCurrentIndex(1);
  QCOMPARE(model.selectedId(), QStringLiteral("snap:remmina"));
  QVERIFY(module.needsSave());
  QCOMPARE(registry.selectCalls, 0);

  combo(module)->setCurrentIndex(0);
  QCOMPARE(model.selectedId(), QStringLiteral("native:/usr/bin/remmina"));
  QVERIFY(!module.needsSave());
  QVERIFY(needsSaveChanged.count() >= 2);

  combo(module)->setItemData(1, QStringLiteral("unknown-ui-data"));
  combo(module)->setCurrentIndex(1);
  QCOMPARE(combo(module)->currentIndex(), 0);
  QCOMPARE(model.selectedId(), QStringLiteral("native:/usr/bin/remmina"));
  QVERIFY(!module.needsSave());
}

void RemminaRunnerConfigTest::invalidEmptySelectedIdHighlightsNoItem() {
  FakeRegistry registry;
  registry.rescans = {{{native({}, QStringLiteral("/malformed/remmina"))}, {}, {}}};
  InstanceSettingsModel model(registry);
  QWidget host;
  RemminaRunnerConfig module(model, &host, {});

  module.load();
  QCOMPARE(combo(module)->count(), 1);
  QCOMPARE(combo(module)->currentIndex(), -1);
  QVERIFY(model.selectedId().isEmpty());
  QVERIFY(!module.needsSave());
}

void RemminaRunnerConfigTest::savePersistsOnlyOnApplyAndKeepsFailureDirty() {
  FakeRegistry success;
  success.rescans = {{{native(), snap()}, QStringLiteral("native:/usr/bin/remmina"), {}}};
  InstanceSettingsModel successModel(success);
  QWidget successHost;
  RemminaRunnerConfig successModule(successModel, &successHost, {});
  successModule.load();
  combo(successModule)->setCurrentIndex(1);
  QCOMPARE(success.selectCalls, 0);

  successModule.save();
  QCOMPARE(success.selectCalls, 1);
  QCOMPARE(success.selectedArguments, QStringList{QStringLiteral("snap:remmina")});
  QVERIFY(!successModule.needsSave());
  QVERIFY(!successModel.isDirty());

  FakeRegistry failure;
  failure.rescans = {{{native(), snap()}, QStringLiteral("native:/usr/bin/remmina"), {}}};
  failure.selectSucceeds = false;
  InstanceSettingsModel failureModel(failure);
  QWidget failureHost;
  RemminaRunnerConfig failureModule(failureModel, &failureHost, {});
  failureModule.load();
  combo(failureModule)->setCurrentIndex(1);
  failureModule.save();
  QCOMPARE(failure.selectCalls, 1);
  QVERIFY(failureModule.needsSave());
  QVERIFY(failureModel.isDirty());
  QCOMPARE(status(failureModule)->text(), QStringLiteral("Could not save the Remmina installation selection."));

  failure.throwSelect = true;
  failureModule.save();
  QCOMPARE(failure.selectCalls, 2);
  QVERIFY(failureModule.needsSave());
  QCOMPARE(status(failureModule)->text(), QStringLiteral("Could not save the Remmina installation selection."));
}

void RemminaRunnerConfigTest::defaultsChangePendingSelectionWithoutPersistence() {
  FakeRegistry registry;
  registry.rescans = {{{snap(), native()}, QStringLiteral("snap:remmina"), {}}};
  InstanceSettingsModel model(registry);
  QWidget host;
  RemminaRunnerConfig module(model, &host, {});
  module.load();

  module.defaults();
  QCOMPARE(combo(module)->currentData().toString(), QStringLiteral("native:/usr/bin/remmina"));
  QCOMPARE(model.selectedId(), QStringLiteral("native:/usr/bin/remmina"));
  QCOMPARE(model.persistedId(), QStringLiteral("snap:remmina"));
  QVERIFY(module.needsSave());
  QCOMPARE(registry.selectCalls, 0);
}

void RemminaRunnerConfigTest::widgetContainsNoProfileControlsOrProfileContent() {
  FakeRegistry registry;
  registry.rescans = {{{native()}, QStringLiteral("native:/usr/bin/remmina"), {}}};
  InstanceSettingsModel model(registry);
  QWidget host;
  RemminaRunnerConfig module(model, &host, {});
  module.load();

  QVERIFY(module.widget()->findChildren<QLineEdit *>().isEmpty());
  const QList<QLabel *> labels = module.widget()->findChildren<QLabel *>();
  for (const QLabel *label : labels) {
    QVERIFY(!label->text().contains(QStringLiteral("/secret/")));
    QVERIFY(!label->text().contains(QStringLiteral("profile"), Qt::CaseInsensitive));
  }
  for (int index = 0; index < combo(module)->count(); ++index) {
    QVERIFY(!combo(module)->itemText(index).contains(QStringLiteral("/secret/")));
  }
}

void RemminaRunnerConfigTest::noJsonPluginFactoryLoadsStandardConstructorInIsolation() {
  EnvironmentGuard guard({QByteArrayLiteral("HOME"), QByteArrayLiteral("PATH"), QByteArrayLiteral("XDG_CONFIG_HOME"),
                          QByteArrayLiteral("XDG_DATA_HOME")});
  QTemporaryDir isolated;
  QVERIFY(isolated.isValid());
  qputenv("HOME", isolated.path().toUtf8());
  qputenv("PATH", isolated.path().toUtf8());
  qputenv("XDG_CONFIG_HOME", isolated.path().toUtf8());
  qputenv("XDG_DATA_HOME", isolated.path().toUtf8());

  const QString pluginPath = QStringLiteral(KCM_PLUGIN_PATH);
  QVERIFY2(QFileInfo::exists(pluginPath), qPrintable(pluginPath));
  const KPluginMetaData metaData(pluginPath, KPluginMetaData::AllowEmptyMetaData);
  QVERIFY(metaData.isValid());
  QWidget host;
  auto result = KPluginFactory::instantiatePlugin<KCModule>(metaData, &host);
  QVERIFY2(result.plugin != nullptr, qPrintable(result.errorText));
  std::unique_ptr<KCModule> module(result.plugin);
  module->load();

  QComboBox *instanceCombo = module->widget()->findChild<QComboBox *>(QStringLiteral("instanceCombo"));
  QLabel *statusLabel = module->widget()->findChild<QLabel *>(QStringLiteral("statusLabel"));
  QVERIFY(instanceCombo != nullptr);
  QVERIFY(statusLabel != nullptr);
  QVERIFY(!instanceCombo->isEnabled());
  QCOMPARE(instanceCombo->count(), 0);
  QCOMPARE(statusLabel->text(), QStringLiteral("No Remmina installations found."));
}

void RemminaRunnerConfigTest::absoluteConfigModuleIsDiscoverableOutsideQtPluginPaths() {
  EnvironmentGuard guard({QByteArrayLiteral("HOME"), QByteArrayLiteral("PATH"), QByteArrayLiteral("QT_PLUGIN_PATH"),
                          QByteArrayLiteral("XDG_CONFIG_HOME"), QByteArrayLiteral("XDG_DATA_HOME")});
  QTemporaryDir isolated(QDir::tempPath() + QStringLiteral("/absolute kcm “quote” \\backslash-XXXXXX"));
  QVERIFY(isolated.isValid());
  qputenv("HOME", isolated.path().toUtf8());
  qputenv("PATH", isolated.path().toUtf8());
  qunsetenv("QT_PLUGIN_PATH");
  qputenv("XDG_CONFIG_HOME", isolated.path().toUtf8());
  qputenv("XDG_DATA_HOME", isolated.path().toUtf8());

  const QString copiedPlugin = isolated.filePath(QStringLiteral("kcm_remmina_krunner.so"));
  QVERIFY2(QFile::copy(QStringLiteral(KCM_PLUGIN_PATH), copiedPlugin), qPrintable(copiedPlugin));

  const QJsonObject runnerMetaData{
      {QStringLiteral("KPlugin"), QJsonObject{{QStringLiteral("Id"), QStringLiteral("org.remminakrunner.KRunner")},
                                              {QStringLiteral("Name"), QStringLiteral("Remmina")}}},
      {QStringLiteral("X-KDE-ConfigModule"), copiedPlugin},
  };
  const KPluginMetaData runnerPlugin(runnerMetaData,
                                     isolated.filePath(QStringLiteral("org.remminakrunner.KRunner.desktop")));
  QVERIFY(runnerPlugin.isValid());

  KPluginModel model;
  model.addPlugins({runnerPlugin}, {});
  const KPluginMetaData configModule = model.findConfigForPluginId(QStringLiteral("org.remminakrunner.KRunner"));
  QVERIFY(configModule.isValid());
  QCOMPARE(QFileInfo(configModule.fileName()).canonicalFilePath(), QFileInfo(copiedPlugin).canonicalFilePath());

  QWidget host;
  auto result = KPluginFactory::instantiatePlugin<KCModule>(configModule, &host);
  QVERIFY2(result.plugin != nullptr, qPrintable(result.errorText));
  std::unique_ptr<KCModule> module(result.plugin);
  module->load();
  QVERIFY(module->widget()->findChild<QComboBox *>(QStringLiteral("instanceCombo")) != nullptr);
}

QTEST_MAIN(RemminaRunnerConfigTest)

#include "test_kcm.moc"
