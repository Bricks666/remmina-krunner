// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include "core/remmina_instance.h"

#include <QList>
#include <QString>
#include <QStringView>

struct SelectionDecision {
  QString selectedId;
  bool changed;
};

SelectionDecision validateSelection(const QList<RemminaInstance> &instances, QStringView savedId);
