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

[[nodiscard]] std::optional<LocatedProfileDirectory> locateProfileDirectory(
    const RemminaInstance &instance);

namespace profile_locator_detail {

enum class LocationError {
    NotFound,
    Unreadable,
};

using LocationResult = std::variant<LocatedProfileDirectory, LocationError>;

[[nodiscard]] LocationResult locateProfileDirectoryWithError(const RemminaInstance &instance);

} // namespace profile_locator_detail
