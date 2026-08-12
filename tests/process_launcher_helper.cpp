// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include <QCoreApplication>
#include <QDataStream>
#include <QSaveFile>
#include <QStringList>

int main(int argc, char **argv) {
  QCoreApplication application(argc, argv);
  const QStringList arguments = application.arguments();
  if (arguments.size() < 2) {
    return 64;
  }

  QSaveFile output(arguments.at(1));
  if (!output.open(QIODevice::WriteOnly)) {
    return 65;
  }
  QDataStream stream(&output);
  stream.setVersion(QDataStream::Qt_6_0);
  stream << arguments.mid(2) << qEnvironmentVariable("XDG_ACTIVATION_TOKEN")
         << qEnvironmentVariableIsSet("XDG_ACTIVATION_TOKEN") << qEnvironmentVariable("LAUNCH_MARKER")
         << qEnvironmentVariableIsSet("INHERITED_STALE_SECRET");
  return stream.status() == QDataStream::Ok && output.commit() ? 0 : 66;
}
