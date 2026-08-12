// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include <QString>
#include <QStringList>

struct ProfileRecord {
  QString opaqueId;
  QString sourcePath;
  QString launchPath;
  QString name;
  QString server;
  QStringList labels;
  QString labelsDisplay;
  QString protocol;
};
