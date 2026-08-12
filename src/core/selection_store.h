// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include <QString>
#include <QStringView>

class SelectionStore {
public:
  virtual ~SelectionStore() = default;
  [[nodiscard]] virtual QString selectedId() const = 0;
  virtual bool writeSelectedId(QStringView id) = 0;
};
