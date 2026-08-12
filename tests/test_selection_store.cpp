// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include <QtTest>

#include "core/kconfig_selection_store.h"

#include <KConfig>
#include <KConfigGroup>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

namespace {

QString configPath(const QTemporaryDir &directory) {
  return QDir(directory.path()).filePath(QStringLiteral("selectionrc"));
}

} // namespace

class SelectionStoreTest : public QObject {
  Q_OBJECT

private slots:
  void missingConfigAndKeyReadAsEmpty();
  void roundTripsOpaqueUnicodeAndPathLikeId();
  void overwritesExistingSelection();
  void emptySelectionClearsPersistedKey();
  void preservesUnrelatedKeysAndGroups();
  void reportsSyncFailure();
  void failedWriteDoesNotLeakAttemptedSelection();
};

void SelectionStoreTest::missingConfigAndKeyReadAsEmpty() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());

  KConfigSelectionStore store(configPath(directory));

  QVERIFY(store.selectedId().isEmpty());
}

void SelectionStoreTest::roundTripsOpaqueUnicodeAndPathLikeId() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = configPath(directory);
  const QString id = QString::fromUtf8("flatpak:用户:/tmp/δοκιμή/../Remmina Profile/稳定");

  KConfigSelectionStore writer(path);
  QVERIFY(writer.writeSelectedId(id));

  const KConfigSelectionStore freshReader(path);
  QCOMPARE(freshReader.selectedId(), id);
}

void SelectionStoreTest::overwritesExistingSelection() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = configPath(directory);
  KConfigSelectionStore store(path);
  QVERIFY(store.writeSelectedId(QStringLiteral("native:/opt/first/remmina")));

  QVERIFY(store.writeSelectedId(QStringLiteral("snap:remmina")));

  QCOMPARE(KConfigSelectionStore(path).selectedId(), QStringLiteral("snap:remmina"));
}

void SelectionStoreTest::emptySelectionClearsPersistedKey() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = configPath(directory);
  KConfigSelectionStore store(path);
  QVERIFY(store.writeSelectedId(QStringLiteral("native:/usr/bin/remmina")));

  QVERIFY(store.writeSelectedId(QStringView{}));

  QVERIFY(KConfigSelectionStore(path).selectedId().isEmpty());
  KConfig config(path, KConfig::SimpleConfig);
  const KConfigGroup general(&config, QStringLiteral("General"));
  QVERIFY(!general.hasKey("selectedInstance"));
}

void SelectionStoreTest::preservesUnrelatedKeysAndGroups() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString path = configPath(directory);
  {
    KConfig config(path, KConfig::SimpleConfig);
    KConfigGroup general(&config, QStringLiteral("General"));
    general.writeEntry("unrelated", QStringLiteral("keep-general"));
    KConfigGroup other(&config, QStringLiteral("Other"));
    other.writeEntry("value", 42);
    QVERIFY(config.sync());
  }
  KConfigSelectionStore store(path);
  QVERIFY(store.writeSelectedId(QStringLiteral("flatpak:user:org.remmina.Remmina")));
  QVERIFY(store.writeSelectedId(QStringView{}));

  KConfig config(path, KConfig::SimpleConfig);
  const KConfigGroup general(&config, QStringLiteral("General"));
  const KConfigGroup other(&config, QStringLiteral("Other"));
  QCOMPARE(general.readEntry("unrelated", QString{}), QStringLiteral("keep-general"));
  QCOMPARE(other.readEntry("value", 0), 42);
}

void SelectionStoreTest::reportsSyncFailure() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString blockedDirectory = QDir(directory.path()).filePath(QStringLiteral("blocked"));
  QVERIFY(QDir{}.mkpath(blockedDirectory));
  const QFileDevice::Permissions readOnlyDirectory = QFileDevice::ReadOwner | QFileDevice::ExeOwner |
                                                     QFileDevice::ReadGroup | QFileDevice::ExeGroup |
                                                     QFileDevice::ReadOther | QFileDevice::ExeOther;
  QVERIFY(QFile::setPermissions(blockedDirectory, readOnlyDirectory));
  const QString path = QDir(blockedDirectory).filePath(QStringLiteral("selectionrc"));

  KConfigSelectionStore store(path);
  QVERIFY(!store.writeSelectedId(QStringLiteral("native:/not/persisted")));

  QVERIFY(QFile::setPermissions(blockedDirectory, readOnlyDirectory | QFileDevice::WriteOwner));
}

void SelectionStoreTest::failedWriteDoesNotLeakAttemptedSelection() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString blockedDirectory = QDir(directory.path()).filePath(QStringLiteral("blocked"));
  QVERIFY(QDir{}.mkpath(blockedDirectory));
  const QString path = QDir(blockedDirectory).filePath(QStringLiteral("selectionrc"));
  KConfigSelectionStore store(path);
  const QString persistedId = QStringLiteral("native:/persisted/remmina");
  QVERIFY(store.writeSelectedId(persistedId));

  const QFileDevice::Permissions readOnlyDirectory = QFileDevice::ReadOwner | QFileDevice::ExeOwner |
                                                     QFileDevice::ReadGroup | QFileDevice::ExeGroup |
                                                     QFileDevice::ReadOther | QFileDevice::ExeOther;
  QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther));
  QVERIFY(QFile::setPermissions(blockedDirectory, readOnlyDirectory));
  QVERIFY(!store.writeSelectedId(QStringLiteral("snap:attempted")));

  QCOMPARE(store.selectedId(), persistedId);
  QCOMPARE(KConfigSelectionStore(path).selectedId(), persistedId);

  QVERIFY(QFile::setPermissions(blockedDirectory, readOnlyDirectory | QFileDevice::WriteOwner));
  QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
}

QTEST_APPLESS_MAIN(SelectionStoreTest)

#include "test_selection_store.moc"
