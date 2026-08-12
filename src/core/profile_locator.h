// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include "core/remmina_instance.h"

#include <QString>

#include <optional>
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

namespace profile_locator_detail {

// Preserves the distinction between a missing and an unreadable selected directory.
[[nodiscard]] ProfileLocationResult locateProfileDirectoryDetailed(
    const RemminaInstance &instance);

} // namespace profile_locator_detail

[[nodiscard]] std::optional<LocatedProfileDirectory> locateProfileDirectory(
    const RemminaInstance &instance);
