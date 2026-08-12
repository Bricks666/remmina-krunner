// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include "core/profile_record.h"

#include <QString>

#include <variant>

enum class ProfileParseError {
    Unreadable,
    Malformed,
    MissingName,
};

std::variant<ProfileRecord, ProfileParseError> parseRemminaProfile(
    const QString &sourcePath, const QString &launchPath, QString opaqueId);
