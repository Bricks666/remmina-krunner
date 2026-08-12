// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include "core/instance_registry.h"

#include <QStringList>

class QCoreApplication;
class QTextStream;

namespace RemminaKRunner {

// Narrow startup seam: production owns the concrete graph while CLI tests can
// prove that --rescan never constructs or starts D-Bus/profile services.
class ApplicationBackend {
public:
  virtual ~ApplicationBackend() = default;
  [[nodiscard]] virtual RegistrySnapshot rescanAndRepair() = 0;
  virtual int startService(QCoreApplication &application) = 0;
};

[[nodiscard]] int runApplication(QCoreApplication &application, const QStringList &arguments,
                                 ApplicationBackend &backend, QTextStream &standardOutput,
                                 QTextStream &standardError) noexcept;

} // namespace RemminaKRunner
