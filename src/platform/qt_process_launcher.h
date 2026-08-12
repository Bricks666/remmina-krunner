// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include "platform/process_launcher.h"

class QtProcessLauncher final : public ProcessLauncher {
public:
  // Invoke on the application thread. The detached process outlives the local
  // QProcess adapter after a successful start.
  bool startDetached(const LaunchRequest &request) override;
};
