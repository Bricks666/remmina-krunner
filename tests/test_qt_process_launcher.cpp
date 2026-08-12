// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include <QtTest>

#include "platform/qt_process_launcher.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QTemporaryDir>

#include <utility>

namespace {

class ScopedEnvironmentVariable {
public:
  ScopedEnvironmentVariable(QByteArray name, QByteArray value)
      : name_(std::move(name)), wasSet_(qEnvironmentVariableIsSet(name_.constData())),
        previousValue_(qgetenv(name_.constData())) {
    qputenv(name_.constData(), value);
  }

  ~ScopedEnvironmentVariable() {
    if (wasSet_) {
      qputenv(name_.constData(), previousValue_);
    } else {
      qunsetenv(name_.constData());
    }
  }

private:
  QByteArray name_;
  bool wasSet_;
  QByteArray previousValue_;
};

} // namespace

class QtProcessLauncherTest : public QObject {
  Q_OBJECT

private slots:
  void launchesDetachedWithExactArgumentsAndEnvironment();
  void excludesAmbientVariablesFromExplicitEnvironment();
  void reportsMissingProgramAsFailure();
};

void QtProcessLauncherTest::launchesDetachedWithExactArgumentsAndEnvironment() {
  QTemporaryDir temporaryDirectory;
  QVERIFY(temporaryDirectory.isValid());
  const QString recordPath = QDir(temporaryDirectory.path()).filePath(QStringLiteral("launch record.bin"));
  const QString shellSideEffect = QDir(temporaryDirectory.path()).filePath(QStringLiteral("must-not-exist"));
  const QStringList hostileArguments{
      QStringLiteral("ordinary argument with spaces"),
      QStringLiteral(";touch ") + shellSideEffect,
      QStringLiteral("$(touch ") + shellSideEffect + QStringLiteral(")"),
      QStringLiteral("`touch ") + shellSideEffect + QStringLiteral("`"),
      QStringLiteral("backslash\\quote\"single'\nsecond line 雪"),
  };
  const QString token = QStringLiteral("exact token;$()`ticks`\\\n激活");
  QProcessEnvironment environment;
  environment.insert(QStringLiteral("XDG_ACTIVATION_TOKEN"), token);
  environment.insert(QStringLiteral("LAUNCH_MARKER"), QStringLiteral("marker with spaces;$()\n雪"));
  const LaunchRequest request{
      .program = QStringLiteral(PROCESS_LAUNCHER_HELPER_PATH),
      .arguments = QStringList{recordPath} + hostileArguments,
      .environment = environment,
  };
  QtProcessLauncher launcher;

  QVERIFY(launcher.startDetached(request));
  QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(recordPath), 5000);
  QVERIFY(!QFileInfo::exists(shellSideEffect));

  QFile record(recordPath);
  QVERIFY(record.open(QIODevice::ReadOnly));
  QDataStream stream(&record);
  stream.setVersion(QDataStream::Qt_6_0);
  QStringList recordedArguments;
  QString recordedToken;
  bool tokenPresent = false;
  QString marker;
  bool staleSecretPresent = true;
  stream >> recordedArguments >> recordedToken >> tokenPresent >> marker >> staleSecretPresent;
  QCOMPARE(stream.status(), QDataStream::Ok);
  QCOMPARE(recordedArguments, hostileArguments);
  QVERIFY(tokenPresent);
  QCOMPARE(recordedToken, token);
  QCOMPARE(marker, QStringLiteral("marker with spaces;$()\n雪"));
  QVERIFY(!staleSecretPresent);
}

void QtProcessLauncherTest::excludesAmbientVariablesFromExplicitEnvironment() {
  const ScopedEnvironmentVariable inheritedToken(QByteArrayLiteral("XDG_ACTIVATION_TOKEN"),
                                                 QByteArrayLiteral("ambient stale token"));
  const ScopedEnvironmentVariable inheritedSecret(QByteArrayLiteral("INHERITED_STALE_SECRET"),
                                                  QByteArrayLiteral("ambient secret"));
  QTemporaryDir temporaryDirectory;
  QVERIFY(temporaryDirectory.isValid());
  const QString recordPath = QDir(temporaryDirectory.path()).filePath(QStringLiteral("explicit environment.bin"));
  QProcessEnvironment environment;
  environment.insert(QStringLiteral("LAUNCH_MARKER"), QStringLiteral("explicit marker"));
  const LaunchRequest request{
      .program = QStringLiteral(PROCESS_LAUNCHER_HELPER_PATH),
      .arguments = {recordPath, QStringLiteral("intended argument")},
      .environment = environment,
  };
  QtProcessLauncher launcher;

  QVERIFY(launcher.startDetached(request));
  QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(recordPath), 5000);

  QFile record(recordPath);
  QVERIFY(record.open(QIODevice::ReadOnly));
  QDataStream stream(&record);
  stream.setVersion(QDataStream::Qt_6_0);
  QStringList recordedArguments;
  QString recordedToken;
  bool tokenPresent = true;
  QString marker;
  bool staleSecretPresent = true;
  stream >> recordedArguments >> recordedToken >> tokenPresent >> marker >> staleSecretPresent;
  QCOMPARE(stream.status(), QDataStream::Ok);
  QCOMPARE(recordedArguments, QStringList{QStringLiteral("intended argument")});
  QVERIFY(!tokenPresent);
  QVERIFY(recordedToken.isEmpty());
  QCOMPARE(marker, QStringLiteral("explicit marker"));
  QVERIFY(!staleSecretPresent);
}

void QtProcessLauncherTest::reportsMissingProgramAsFailure() {
  QTemporaryDir temporaryDirectory;
  QVERIFY(temporaryDirectory.isValid());
  const LaunchRequest request{
      .program = QDir(temporaryDirectory.path()).filePath(QStringLiteral("definitely-not-a-program")),
      .arguments = {},
      .environment = QProcessEnvironment::systemEnvironment(),
  };
  QtProcessLauncher launcher;

  QVERIFY(!launcher.startDetached(request));
}

int main(int argc, char **argv) {
  QCoreApplication application(argc, argv);
  QtProcessLauncherTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "test_qt_process_launcher.moc"
