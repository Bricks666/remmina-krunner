// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include <QtTest>

#include "dbus/dbus_types.h"
#include "dbus/runner_service.h"

#include "core/instance_registry.h"
#include "core/profile_catalog.h"
#include "core/remmina_launcher.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QEventLoop>
#include <QFile>
#include <QMetaMethod>
#include <QRegularExpression>
#include <QTimer>
#include <QXmlStreamReader>

#include <type_traits>
#include <utility>

namespace {

class FakeRegistry final : public InstanceRegistryControlSource {
public:
  RegistrySnapshot snapshot() const override { return {}; }
  RegistrySnapshot rescanAndRepair() override {
    ++rescanCalls;
    return {};
  }
  bool select(QStringView) override { return false; }
  int rescanCalls = 0;
};

class FakeCatalog final : public ProfileCatalogAccess {
public:
  CatalogResult records(const RemminaInstance &) override { return QList<ProfileRecord>{}; }
  std::optional<ProfileRecord> resolve(QStringView, QStringView) const override { return std::nullopt; }
  void endSession() override { ++endCalls; }
  void reset() override { ++resetCalls; }
  int endCalls = 0;
  int resetCalls = 0;
};

class FakeLauncher final : public RemminaLaunchSource {
public:
  RemminaLaunchResult create(QStringView activationToken) override {
    ++createCalls;
    token = activationToken.toString();
    return RemminaLaunchResult::Started;
  }
  RemminaLaunchResult connect(QStringView, QStringView) override {
    ++connectCalls;
    return RemminaLaunchResult::Started;
  }
  int createCalls = 0;
  int connectCalls = 0;
  QString token;
};

class RoundTripRelay final : public QObject {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.example.RemminaKRunner.RoundTrip")

public slots:
  RemminaKRunner::RemoteMatch EchoMatch(RemminaKRunner::RemoteMatch match) { return match; }

  RemminaKRunner::RunnerActions EchoActions(RemminaKRunner::RunnerActions actions) { return actions; }
};

QDBusMessage await(QDBusPendingCall call) {
  QDBusPendingCallWatcher watcher(std::move(call));
  if (!watcher.isFinished()) {
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&watcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(5000);
    loop.exec();
  }
  if (!watcher.isFinished()) {
    return QDBusMessage::createError(QDBusError::Timeout, QStringLiteral("private round-trip timed out"));
  }
  return watcher.reply();
}

struct Argument {
  QString name;
  QString type;
  QString direction;

  bool operator==(const Argument &) const = default;
};

struct Method {
  QString name;
  QList<Argument> arguments;
  QMap<QString, QString> annotations;

  bool operator==(const Method &) const = default;
};

QList<Method> readInterface(const QString &path, QString &interfaceName) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  QXmlStreamReader reader(&file);
  QList<Method> methods;
  Method *method = nullptr;
  while (!reader.atEnd()) {
    reader.readNext();
    if (!reader.isStartElement()) {
      continue;
    }
    if (reader.name() == QLatin1StringView("interface")) {
      interfaceName = reader.attributes().value(QLatin1StringView("name")).toString();
    } else if (reader.name() == QLatin1StringView("method")) {
      methods.append({reader.attributes().value(QLatin1StringView("name")).toString(), {}, {}});
      method = &methods.last();
    } else if (reader.name() == QLatin1StringView("arg") && method != nullptr) {
      method->arguments.append({
          reader.attributes().value(QLatin1StringView("name")).toString(),
          reader.attributes().value(QLatin1StringView("type")).toString(),
          reader.attributes().value(QLatin1StringView("direction")).toString(),
      });
    } else if (reader.name() == QLatin1StringView("annotation") && method != nullptr) {
      method->annotations.insert(reader.attributes().value(QLatin1StringView("name")).toString(),
                                 reader.attributes().value(QLatin1StringView("value")).toString());
    }
  }
  return reader.hasError() ? QList<Method>{} : methods;
}

} // namespace

class DbusContractTest final : public QObject {
  Q_OBJECT

private slots:
  void xmlPreservesUpstreamLicenseAndExactContract();
  void metatypesHaveExactSignaturesAndRoundTrip();
  void serviceMetaobjectExposesExactNoexceptSlots();
  void liveServiceExportsAndAnswersEveryContractMethod();
};

void DbusContractTest::xmlPreservesUpstreamLicenseAndExactContract() {
  QFile file(QStringLiteral(KRUNNER_INTERFACE_XML));
  QVERIFY(file.open(QIODevice::ReadOnly));
  const QByteArray xml = file.readAll();
  QVERIFY(xml.contains("SPDX-License-Identifier: LGPL-2.0-or-later"));
  QVERIFY(xml.contains("2017, 2018 David Edmundson"));
  QVERIFY(xml.contains("2020 Kai Uwe Broulik"));
  QVERIFY(xml.contains("2020-2021 Alexander Lohnau"));

  QString interfaceName;
  const QList<Method> actual = readInterface(QStringLiteral(KRUNNER_INTERFACE_XML), interfaceName);
  QCOMPARE(interfaceName, QStringLiteral("org.kde.krunner1"));
  const QList<Method> expected{
      {QStringLiteral("Teardown"), {}, {}},
      {QStringLiteral("Config"),
       {{QStringLiteral("config"), QStringLiteral("a{sv}"), QStringLiteral("out")}},
       {{QStringLiteral("org.qtproject.QtDBus.QtTypeName.Out0"), QStringLiteral("QVariantMap")}}},
      {QStringLiteral("Actions"),
       {{QStringLiteral("matches"), QStringLiteral("a(sss)"), QStringLiteral("out")}},
       {{QStringLiteral("org.qtproject.QtDBus.QtTypeName.Out0"), QStringLiteral("KRunner::Actions")}}},
      {QStringLiteral("SetActivationToken"),
       {{QStringLiteral("token"), QStringLiteral("s"), QStringLiteral("in")}},
       {}},
      {QStringLiteral("Run"),
       {{QStringLiteral("matchId"), QStringLiteral("s"), QStringLiteral("in")},
        {QStringLiteral("actionId"), QStringLiteral("s"), QStringLiteral("in")}},
       {}},
      {QStringLiteral("Match"),
       {{QStringLiteral("query"), QStringLiteral("s"), QStringLiteral("in")},
        {QStringLiteral("matches"), QStringLiteral("a(sssida{sv})"), QStringLiteral("out")}},
       {{QStringLiteral("org.qtproject.QtDBus.QtTypeName.Out0"), QStringLiteral("RemoteMatches")}}},
  };
  QCOMPARE(actual, expected);
}

void DbusContractTest::metatypesHaveExactSignaturesAndRoundTrip() {
  RemminaKRunner::registerDbusTypes();
  QCOMPARE(QByteArray(QDBusMetaType::typeToSignature(QMetaType::fromType<RemminaKRunner::RemoteMatches>())),
           QByteArray("a(sssida{sv})"));
  QCOMPARE(QByteArray(QDBusMetaType::typeToSignature(QMetaType::fromType<RemminaKRunner::RunnerActions>())),
           QByteArray("a(sss)"));

  const RemminaKRunner::RemoteMatch original{
      QStringLiteral("opaque"),
      QStringLiteral("Visible"),
      QStringLiteral("icon"),
      50,
      0.75,
      {{QStringLiteral("subtext"), QStringLiteral("RDP · host")},
       {QStringLiteral("category"), QStringLiteral("Remmina")},
       {QStringLiteral("actions"), QStringList{}}},
  };
  QDBusConnection bus = QDBusConnection::sessionBus();
  QVERIFY(bus.isConnected());
  const QString serviceName =
      QStringLiteral("org.example.RemminaKRunner.RoundTrip.p%1").arg(QCoreApplication::applicationPid());
  RoundTripRelay relay;
  QVERIFY(bus.registerService(serviceName));
  QVERIFY(bus.registerObject(QStringLiteral("/relay"), &relay, QDBusConnection::ExportAllSlots));
  QDBusInterface interface(serviceName, QStringLiteral("/relay"),
                           QStringLiteral("org.example.RemminaKRunner.RoundTrip"), bus);
  QVERIFY(interface.isValid());
  const QDBusMessage matchReply =
      await(interface.asyncCall(QStringLiteral("EchoMatch"), QVariant::fromValue(original)));
  QCOMPARE(matchReply.type(), QDBusMessage::ReplyMessage);
  QCOMPARE(matchReply.arguments().size(), 1);
  const QDBusArgument encodedMatch = qvariant_cast<QDBusArgument>(matchReply.arguments().constFirst());
  QCOMPARE(encodedMatch.currentSignature(), QStringLiteral("(sssida{sv})"));
  const auto decodedMatch = qdbus_cast<RemminaKRunner::RemoteMatch>(encodedMatch);
  QCOMPARE(decodedMatch.id, original.id);
  QCOMPARE(decodedMatch.text, original.text);
  QCOMPARE(decodedMatch.iconName, original.iconName);
  QCOMPARE(decodedMatch.categoryRelevance, original.categoryRelevance);
  QCOMPARE(decodedMatch.relevance, original.relevance);
  QCOMPARE(decodedMatch.properties, original.properties);

  const RemminaKRunner::RunnerActions actions{{QStringLiteral("id"), QStringLiteral("Text"), QStringLiteral("icon")}};
  const QDBusMessage actionsReply =
      await(interface.asyncCall(QStringLiteral("EchoActions"), QVariant::fromValue(actions)));
  QCOMPARE(actionsReply.type(), QDBusMessage::ReplyMessage);
  QCOMPARE(actionsReply.arguments().size(), 1);
  const QDBusArgument encodedActions = qvariant_cast<QDBusArgument>(actionsReply.arguments().constFirst());
  QCOMPARE(encodedActions.currentSignature(), QStringLiteral("a(sss)"));
  const auto decodedActions = qdbus_cast<RemminaKRunner::RunnerActions>(encodedActions);
  QCOMPARE(decodedActions.size(), 1);
  QCOMPARE(decodedActions.constFirst().id, actions.constFirst().id);
  QCOMPARE(decodedActions.constFirst().text, actions.constFirst().text);
  QCOMPARE(decodedActions.constFirst().iconName, actions.constFirst().iconName);
  bus.unregisterObject(QStringLiteral("/relay"));
  QVERIFY(bus.unregisterService(serviceName));
}

void DbusContractTest::serviceMetaobjectExposesExactNoexceptSlots() {
  static_assert(noexcept(std::declval<RemminaKRunner::RunnerService &>().Match(std::declval<const QString &>())));
  static_assert(noexcept(std::declval<RemminaKRunner::RunnerService &>().Actions()));
  static_assert(noexcept(std::declval<RemminaKRunner::RunnerService &>().Run(std::declval<const QString &>(),
                                                                             std::declval<const QString &>())));
  static_assert(noexcept(std::declval<RemminaKRunner::RunnerService &>().Teardown()));
  static_assert(noexcept(std::declval<RemminaKRunner::RunnerService &>().Config()));
  static_assert(
      noexcept(std::declval<RemminaKRunner::RunnerService &>().SetActivationToken(std::declval<const QString &>())));

  const QMetaObject &meta = RemminaKRunner::RunnerService::staticMetaObject;
  QCOMPARE(QString::fromLatin1(meta.classInfo(meta.indexOfClassInfo("D-Bus Interface")).value()),
           QStringLiteral("org.kde.krunner1"));
  QStringList slotSignatures;
  for (int index = meta.methodOffset(); index < meta.methodCount(); ++index) {
    const QMetaMethod method = meta.method(index);
    if (method.methodType() == QMetaMethod::Slot) {
      slotSignatures.append(QString::fromLatin1(method.methodSignature()));
    }
  }
  QCOMPARE(slotSignatures, QStringList({QStringLiteral("Match(QString)"), QStringLiteral("Actions()"),
                                        QStringLiteral("Run(QString,QString)"), QStringLiteral("Teardown()"),
                                        QStringLiteral("Config()"), QStringLiteral("SetActivationToken(QString)")}));
}

void DbusContractTest::liveServiceExportsAndAnswersEveryContractMethod() {
  RemminaKRunner::registerDbusTypes();
  FakeRegistry registry;
  FakeCatalog catalog;
  FakeLauncher launcher;
  RemminaKRunner::RunnerService service(registry, catalog, launcher);
  QDBusConnection bus = QDBusConnection::sessionBus();
  QVERIFY(bus.isConnected());
  const QString serviceName =
      QStringLiteral("org.example.RemminaKRunner.Contract.p%1").arg(QCoreApplication::applicationPid());
  QVERIFY(bus.registerService(serviceName));
  QVERIFY(bus.registerObject(QStringLiteral("/runner"), &service, QDBusConnection::ExportAllSlots));

  QDBusInterface introspection(serviceName, QStringLiteral("/runner"),
                               QStringLiteral("org.freedesktop.DBus.Introspectable"), bus);
  const QDBusMessage introspectionReply = await(introspection.asyncCall(QStringLiteral("Introspect")));
  QCOMPARE(introspectionReply.type(), QDBusMessage::ReplyMessage);
  const QString xml = introspectionReply.arguments().constFirst().toString();
  const QRegularExpression interfaceExpression(
      QStringLiteral("<interface name=\"org\\.kde\\.krunner1\">(.*?)</interface>"),
      QRegularExpression::DotMatchesEverythingOption);
  const QRegularExpressionMatch interfaceMatch = interfaceExpression.match(xml);
  QVERIFY(interfaceMatch.hasMatch());
  const QString interfaceXml = interfaceMatch.captured(1);
  QRegularExpression methodExpression(QStringLiteral("<method name=\"([^\"]+)\">"));
  QStringList methods;
  auto iterator = methodExpression.globalMatch(interfaceXml);
  while (iterator.hasNext()) {
    methods.append(iterator.next().captured(1));
  }
  QCOMPARE(methods,
           QStringList({QStringLiteral("Match"), QStringLiteral("Actions"), QStringLiteral("Run"),
                        QStringLiteral("Teardown"), QStringLiteral("Config"), QStringLiteral("SetActivationToken")}));
  QVERIFY(interfaceXml.contains(QStringLiteral("type=\"a(sssida{sv})\" direction=\"out\"")));
  QVERIFY(interfaceXml.contains(QStringLiteral("type=\"a(sss)\" direction=\"out\"")));
  QVERIFY(interfaceXml.contains(QStringLiteral("type=\"a{sv}\" direction=\"out\"")));

  QDBusInterface runner(serviceName, QStringLiteral("/runner"), QStringLiteral("org.kde.krunner1"), bus);
  QVERIFY(runner.isValid());
  const QString abandonedToken = QStringLiteral("abandoned-live-activation-token-sentinel");
  QCOMPARE(await(runner.asyncCall(QStringLiteral("SetActivationToken"), abandonedToken)).type(),
           QDBusMessage::ReplyMessage);
  const QDBusMessage matchReply = await(runner.asyncCall(QStringLiteral("Match"), QStringLiteral("rem new")));
  QCOMPARE(matchReply.type(), QDBusMessage::ReplyMessage);
  const QDBusArgument matchArgument = qvariant_cast<QDBusArgument>(matchReply.arguments().constFirst());
  QCOMPARE(matchArgument.currentSignature(), QStringLiteral("a(sssida{sv})"));
  const RemminaKRunner::RemoteMatches liveMatches = qdbus_cast<RemminaKRunner::RemoteMatches>(matchArgument);
  QCOMPARE(liveMatches.size(), 1);
  QVERIFY(!liveMatches.constFirst().id.contains(abandonedToken));
  QVERIFY(!liveMatches.constFirst().text.contains(abandonedToken));
  QVERIFY(!liveMatches.constFirst().properties.values().contains(abandonedToken));
  QCOMPARE(await(runner.asyncCall(QStringLiteral("Run"), QStringLiteral("action:new"), QString{})).type(),
           QDBusMessage::ReplyMessage);
  QCOMPARE(launcher.createCalls, 1);
  QCOMPARE(launcher.token, QString{});

  const QDBusMessage actionsReply = await(runner.asyncCall(QStringLiteral("Actions")));
  QCOMPARE(actionsReply.type(), QDBusMessage::ReplyMessage);
  const QDBusArgument actionsArgument = qvariant_cast<QDBusArgument>(actionsReply.arguments().constFirst());
  QCOMPARE(actionsArgument.currentSignature(), QStringLiteral("a(sss)"));
  QVERIFY(qdbus_cast<RemminaKRunner::RunnerActions>(actionsArgument).isEmpty());

  const QDBusMessage configReply = await(runner.asyncCall(QStringLiteral("Config")));
  QCOMPARE(configReply.type(), QDBusMessage::ReplyMessage);
  const QVariantMap config = qdbus_cast<QVariantMap>(configReply.arguments().constFirst());
  QCOMPARE(config.keys(), QStringList{QStringLiteral("MatchRegex")});
  QCOMPARE(registry.rescanCalls, 1);
  QCOMPARE(catalog.resetCalls, 1);

  QCOMPARE(await(runner.asyncCall(QStringLiteral("SetActivationToken"), QStringLiteral("one-shot"))).type(),
           QDBusMessage::ReplyMessage);
  QCOMPARE(await(runner.asyncCall(QStringLiteral("Run"), QStringLiteral("action:new"), QString{})).type(),
           QDBusMessage::ReplyMessage);
  QCOMPARE(launcher.createCalls, 2);
  QCOMPARE(launcher.token, QStringLiteral("one-shot"));
  QCOMPARE(await(runner.asyncCall(QStringLiteral("Teardown"))).type(), QDBusMessage::ReplyMessage);
  QCOMPARE(catalog.endCalls, 1);

  bus.unregisterObject(QStringLiteral("/runner"));
  QVERIFY(bus.unregisterService(serviceName));
}

QTEST_GUILESS_MAIN(DbusContractTest)

#include "test_dbus_contract.moc"
