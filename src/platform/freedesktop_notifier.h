// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include "platform/notifier.h"

class FreedesktopNotifier final : public Notifier {
public:
    void showLaunchFailure() noexcept override;
};
