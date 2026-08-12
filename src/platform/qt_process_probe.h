// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include "platform/process_probe.h"

class QtProcessProbe final : public ProcessProbe {
public:
  static constexpr int timeoutMilliseconds = 2000;
  static constexpr qsizetype maximumStandardOutputBytes = 64 * 1024;

  // Call only from the QCoreApplication thread before shutdown. The main event loop must
  // resume after this synchronous call so a pathologically slow killed child can be reaped.
  // Unsupported invocations fail without starting a process.
  ProbeResult run(const QString &executable, const QStringList &arguments) override;
};
