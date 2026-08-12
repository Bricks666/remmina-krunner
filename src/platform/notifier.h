// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include <QLatin1StringView>

inline constexpr QLatin1StringView launchFailureTitle{"Remmina KRunner"};
inline constexpr QLatin1StringView launchFailureBody{"Could not open Remmina."};

class Notifier {
public:
    virtual ~Notifier() = default;
    // Implementations should contain failures. RemminaLauncher also catches an
    // unexpected exception from third-party/test adapters defensively.
    virtual void showLaunchFailure() = 0;
};
