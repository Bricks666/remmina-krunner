// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include <QtTest>

#include "kcm/instance_settings_model.h"

#include "core/instance_registry.h"

#include <stdexcept>
#include <utility>

namespace {

RemminaInstance native(QString id = QStringLiteral("native:/usr/bin/remmina"),
                       QString executable = QStringLiteral("/usr/bin/remmina")) {
  return {
      .id = std::move(id),
      .kind = InstanceKind::Native,
      .displayName = QStringLiteral("version metadata must not be used"),
      .executable = std::move(executable),
      .launcherPrefix = {},
      .profiles = {.configHome = QStringLiteral("/secret/profile/path")},
  };
}

RemminaInstance flatpak(QString id = QStringLiteral("flatpak:user:org.remmina.Remmina/x86_64/stable"),
                        QString selector = QStringLiteral("--user"),
                        QString ref = QStringLiteral("org.remmina.Remmina/x86_64/stable")) {
  return {
      .id = std::move(id),
      .kind = InstanceKind::Flatpak,
      .displayName = QStringLiteral("revision 1234 must not be used"),
      .executable = QStringLiteral("/usr/bin/flatpak"),
      .launcherPrefix = {std::move(selector), QStringLiteral("run"), std::move(ref)},
      .profiles = {.dataHome = QStringLiteral("/secret/flatpak/profile")},
  };
}

RemminaInstance snap(QString id = QStringLiteral("snap:remmina"),
                     QString executable = QStringLiteral("/snap/bin/remmina")) {
  return {
      .id = std::move(id),
      .kind = InstanceKind::Snap,
      .displayName = QStringLiteral("revision 999 must not be used"),
      .executable = std::move(executable),
      .launcherPrefix = {},
      .profiles = {.legacyHome = QStringLiteral("/secret/snap/profile")},
  };
}

class FakeRegistry final : public InstanceRegistryControlSource {
public:
  RegistrySnapshot snapshot() const override { return current; }

  RegistrySnapshot rescanAndRepair() override {
    ++rescanCalls;
    if (throwRescan) {
      throw std::runtime_error("private scan failure");
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
      throw std::runtime_error("private save failure");
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

} // namespace

class InstanceSettingsModelTest : public QObject {
  Q_OBJECT

private slots:
  void loadRescansExactlyOnceAndReplacesState();
  void loadUsesRegistryAutoSelectionWithoutSelectingAgain();
  void listsAllInstancesInOrderWithTypeAndIdentity();
  void manualSelectionWritesOnlyOnSuccessfulApply();
  void failedAndThrowingSavesKeepPendingSelectionDirty();
  void rejectsEmptyUnknownAndDuplicateSelections();
  void preservesPartialErrorsAndHandlesEmptyOrThrowingScans();
  void defaultsUsePriorityAndInputOrderWithoutWriting();
  void sanitizesDisplayControlsWithoutChangingIds();
};

void InstanceSettingsModelTest::loadRescansExactlyOnceAndReplacesState() {
  FakeRegistry registry;
  registry.rescans = {
      {{snap()}, QStringLiteral("snap:remmina"), {}},
      {{native(), flatpak()}, QStringLiteral("flatpak:user:org.remmina.Remmina/x86_64/stable"), {}},
  };
  InstanceSettingsModel model(registry);

  QVERIFY(model.load());
  QCOMPARE(registry.rescanCalls, 1);
  QCOMPARE(model.items().size(), 1);
  QCOMPARE(model.selectedId(), QStringLiteral("snap:remmina"));

  QVERIFY(model.load());
  QCOMPARE(registry.rescanCalls, 2);
  QCOMPARE(model.items().size(), 2);
  QCOMPARE(model.persistedId(), QStringLiteral("flatpak:user:org.remmina.Remmina/x86_64/stable"));
  QCOMPARE(model.selectedId(), model.persistedId());
  QVERIFY(!model.isDirty());
}

void InstanceSettingsModelTest::loadUsesRegistryAutoSelectionWithoutSelectingAgain() {
  FakeRegistry registry;
  registry.rescans = {{{snap(), native()}, QStringLiteral("native:/usr/bin/remmina"), {}}};
  InstanceSettingsModel model(registry);

  QVERIFY(model.load());
  QCOMPARE(model.persistedId(), QStringLiteral("native:/usr/bin/remmina"));
  QCOMPARE(registry.selectCalls, 0);
}

void InstanceSettingsModelTest::listsAllInstancesInOrderWithTypeAndIdentity() {
  FakeRegistry registry;
  registry.rescans = {{
      {snap(), flatpak(), native()},
      QStringLiteral("snap:remmina"),
      {},
  }};
  InstanceSettingsModel model(registry);

  QVERIFY(model.load());
  const QList<SettingsInstanceItem> items = model.items();
  QCOMPARE(items.size(), 3);
  QCOMPARE(items.at(0).id, QStringLiteral("snap:remmina"));
  QCOMPARE(items.at(0).kind, InstanceKind::Snap);
  QVERIFY(items.at(0).displayText.contains(QStringLiteral("Snap")));
  QVERIFY(items.at(0).displayText.contains(QStringLiteral("/snap/bin/remmina")));
  QCOMPARE(items.at(1).kind, InstanceKind::Flatpak);
  QVERIFY(items.at(1).displayText.contains(QStringLiteral("Flatpak")));
  QVERIFY(items.at(1).displayText.contains(QStringLiteral("user")));
  QVERIFY(items.at(1).displayText.contains(QStringLiteral("org.remmina.Remmina/x86_64/stable")));
  QCOMPARE(items.at(2).kind, InstanceKind::Native);
  QVERIFY(items.at(2).displayText.contains(QStringLiteral("Native")));
  QVERIFY(items.at(2).displayText.contains(QStringLiteral("/usr/bin/remmina")));
  for (const SettingsInstanceItem &item : items) {
    QVERIFY(!item.displayText.contains(QStringLiteral("revision")));
    QVERIFY(!item.displayText.contains(QStringLiteral("/secret/")));
  }
}

void InstanceSettingsModelTest::manualSelectionWritesOnlyOnSuccessfulApply() {
  FakeRegistry registry;
  registry.rescans = {{{native(), snap()}, QStringLiteral("native:/usr/bin/remmina"), {}}};
  InstanceSettingsModel model(registry);
  QVERIFY(model.load());

  QVERIFY(model.selectPending(QStringLiteral("snap:remmina")));
  QCOMPARE(model.selectedId(), QStringLiteral("snap:remmina"));
  QCOMPARE(model.persistedId(), QStringLiteral("native:/usr/bin/remmina"));
  QVERIFY(model.isDirty());
  QCOMPARE(registry.selectCalls, 0);

  QVERIFY(model.save());
  QCOMPARE(registry.selectCalls, 1);
  QCOMPARE(registry.selectedArguments, QStringList{QStringLiteral("snap:remmina")});
  QCOMPARE(model.persistedId(), QStringLiteral("snap:remmina"));
  QVERIFY(!model.isDirty());

  QVERIFY(model.save());
  QCOMPARE(registry.selectCalls, 1);
  QVERIFY(model.selectPending(QStringLiteral("native:/usr/bin/remmina")));
  QVERIFY(model.isDirty());
  QVERIFY(model.selectPending(QStringLiteral("snap:remmina")));
  QVERIFY(!model.isDirty());
}

void InstanceSettingsModelTest::failedAndThrowingSavesKeepPendingSelectionDirty() {
  FakeRegistry registry;
  registry.rescans = {{{native(), snap()}, QStringLiteral("native:/usr/bin/remmina"), {}}};
  InstanceSettingsModel model(registry);
  QVERIFY(model.load());
  QVERIFY(model.selectPending(QStringLiteral("snap:remmina")));

  registry.selectSucceeds = false;
  QVERIFY(!model.save());
  QCOMPARE(model.persistedId(), QStringLiteral("native:/usr/bin/remmina"));
  QCOMPARE(model.selectedId(), QStringLiteral("snap:remmina"));
  QVERIFY(model.isDirty());

  registry.throwSelect = true;
  QVERIFY(!model.save());
  QCOMPARE(registry.selectCalls, 2);
  QCOMPARE(model.persistedId(), QStringLiteral("native:/usr/bin/remmina"));
  QCOMPARE(model.selectedId(), QStringLiteral("snap:remmina"));
  QVERIFY(model.isDirty());
}

void InstanceSettingsModelTest::rejectsEmptyUnknownAndDuplicateSelections() {
  const RemminaInstance duplicated = native(QStringLiteral("opaque-duplicate"), QStringLiteral("/opt/remmina"));
  FakeRegistry registry;
  registry.rescans = {{{native(), duplicated, duplicated}, QStringLiteral("opaque-duplicate"), {}}};
  InstanceSettingsModel model(registry);
  QVERIFY(model.load());

  QVERIFY(model.selectedId().isEmpty());
  QVERIFY(model.persistedId().isEmpty());
  QVERIFY(!model.selectPending(QStringView{}));
  QVERIFY(!model.selectPending(QStringLiteral("unknown")));
  QVERIFY(!model.selectPending(QStringLiteral("opaque-duplicate")));
  QVERIFY(model.selectPending(QStringLiteral("native:/usr/bin/remmina")));
  QVERIFY(model.isDirty());
  QCOMPARE(registry.selectCalls, 0);
}

void InstanceSettingsModelTest::preservesPartialErrorsAndHandlesEmptyOrThrowingScans() {
  FakeRegistry partial;
  partial.rescans = {
      {{native()}, QStringLiteral("native:/usr/bin/remmina"), {QStringLiteral("flatpak"), QStringLiteral("snap")}}};
  InstanceSettingsModel partialModel(partial);
  QVERIFY(partialModel.load());
  QVERIFY(partialModel.hasInstances());
  QCOMPARE(partialModel.items().size(), 1);
  QCOMPARE(partialModel.failedBackends(), QStringList({QStringLiteral("flatpak"), QStringLiteral("snap")}));

  FakeRegistry empty;
  empty.rescans = {{{}, {}, {}}};
  InstanceSettingsModel emptyModel(empty);
  QVERIFY(emptyModel.load());
  QVERIFY(!emptyModel.hasInstances());
  QVERIFY(emptyModel.selectedId().isEmpty());
  QVERIFY(!emptyModel.isDirty());
  QVERIFY(emptyModel.save());
  QCOMPARE(empty.selectCalls, 0);

  FakeRegistry throwing;
  throwing.throwRescan = true;
  InstanceSettingsModel throwingModel(throwing);
  QVERIFY(!throwingModel.load());
  QCOMPARE(throwing.rescanCalls, 1);
  QVERIFY(!throwingModel.hasInstances());
  QVERIFY(throwingModel.items().isEmpty());
  QVERIFY(throwingModel.selectedId().isEmpty());
  QVERIFY(throwingModel.persistedId().isEmpty());
  QVERIFY(throwingModel.failedBackends().isEmpty());
  QVERIFY(!throwingModel.isDirty());
}

void InstanceSettingsModelTest::defaultsUsePriorityAndInputOrderWithoutWriting() {
  FakeRegistry registry;
  registry.rescans = {{
      {snap(), native(QStringLiteral("native:first"), QStringLiteral("/first/remmina")),
       native(QStringLiteral("native:second"), QStringLiteral("/second/remmina")), flatpak()},
      QStringLiteral("snap:remmina"),
      {},
  }};
  InstanceSettingsModel model(registry);
  QVERIFY(model.load());

  model.defaults();
  QCOMPARE(model.selectedId(), QStringLiteral("native:first"));
  QCOMPARE(model.persistedId(), QStringLiteral("snap:remmina"));
  QVERIFY(model.isDirty());
  QCOMPARE(registry.selectCalls, 0);

  FakeRegistry empty;
  empty.rescans = {{{}, {}, {}}};
  InstanceSettingsModel emptyModel(empty);
  QVERIFY(emptyModel.load());
  emptyModel.defaults();
  QVERIFY(emptyModel.selectedId().isEmpty());
  QVERIFY(!emptyModel.isDirty());
  QCOMPARE(empty.selectCalls, 0);
}

void InstanceSettingsModelTest::sanitizesDisplayControlsWithoutChangingIds() {
  const QString opaqueId = QStringLiteral("native:opaque\nidentifier");
  FakeRegistry registry;
  registry.rescans = {{{native(opaqueId, QStringLiteral("/opt/rem\nmina\tlauncher\x1f"))}, opaqueId, {}}};
  InstanceSettingsModel model(registry);

  QVERIFY(model.load());
  const SettingsInstanceItem item = model.items().constFirst();
  QCOMPARE(item.id, opaqueId);
  QCOMPARE(model.selectedId(), opaqueId);
  QVERIFY(!item.displayText.contains(u'\n'));
  QVERIFY(!item.displayText.contains(u'\t'));
  QVERIFY(!item.displayText.contains(QChar(0x1f)));
  QVERIFY(item.displayText.contains(QStringLiteral("rem mina launcher")));
}

QTEST_APPLESS_MAIN(InstanceSettingsModelTest)

#include "test_instance_settings_model.moc"
