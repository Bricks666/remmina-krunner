// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include <QDBusArgument>
#include <QDBusMetaType>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QVariantMap>

namespace RemminaKRunner {

struct RemoteMatch {
  QString id;
  QString text;
  QString iconName;
  int categoryRelevance = 0;
  double relevance = 0.0;
  QVariantMap properties;

  bool operator==(const RemoteMatch &) const = default;
};

using RemoteMatches = QList<RemoteMatch>;

struct RunnerAction {
  QString id;
  QString text;
  QString iconName;

  bool operator==(const RunnerAction &) const = default;
};

using RunnerActions = QList<RunnerAction>;

inline QDBusArgument &operator<<(QDBusArgument &argument, const RemoteMatch &match) {
  argument.beginStructure();
  argument << match.id << match.text << match.iconName << match.categoryRelevance << match.relevance
           << match.properties;
  argument.endStructure();
  return argument;
}

inline const QDBusArgument &operator>>(const QDBusArgument &argument, RemoteMatch &match) {
  argument.beginStructure();
  argument >> match.id >> match.text >> match.iconName >> match.categoryRelevance >> match.relevance >>
      match.properties;
  argument.endStructure();
  return argument;
}

inline QDBusArgument &operator<<(QDBusArgument &argument, const RunnerAction &action) {
  argument.beginStructure();
  argument << action.id << action.text << action.iconName;
  argument.endStructure();
  return argument;
}

inline const QDBusArgument &operator>>(const QDBusArgument &argument, RunnerAction &action) {
  argument.beginStructure();
  argument >> action.id >> action.text >> action.iconName;
  argument.endStructure();
  return argument;
}

} // namespace RemminaKRunner

Q_DECLARE_METATYPE(RemminaKRunner::RemoteMatch)
Q_DECLARE_METATYPE(RemminaKRunner::RemoteMatches)
Q_DECLARE_METATYPE(RemminaKRunner::RunnerAction)
Q_DECLARE_METATYPE(RemminaKRunner::RunnerActions)

namespace RemminaKRunner {

inline void registerDbusTypes() {
  static const bool registered = [] {
    qRegisterMetaType<RemoteMatch>();
    qRegisterMetaType<RemoteMatches>();
    qRegisterMetaType<RunnerAction>();
    qRegisterMetaType<RunnerActions>();
    qDBusRegisterMetaType<RemoteMatch>();
    qDBusRegisterMetaType<RemoteMatches>();
    qDBusRegisterMetaType<RunnerAction>();
    qDBusRegisterMetaType<RunnerActions>();
    return true;
  }();
  Q_UNUSED(registered)
}

} // namespace RemminaKRunner
