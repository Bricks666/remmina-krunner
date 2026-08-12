// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include "core/remmina_instance.h"

#include <QString>

#include <variant>

struct LocatedProfileDirectory {
    QString hostPath;
    QString launchPath;
};

enum class ProfileLocationError {
    NotFound,
    Unreadable,
};

using ProfileLocationResult = std::variant<LocatedProfileDirectory, ProfileLocationError>;

[[nodiscard]] ProfileLocationResult locateProfileDirectory(const RemminaInstance &instance);
