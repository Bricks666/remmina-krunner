// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringView>

#include <optional>
#include <variant>

using AllowedValues = QHash<QString, QString>;

namespace key_file_reader_detail {

enum class ReadError {
    Unreadable,
    Malformed,
};

using ReadResult = std::variant<AllowedValues, ReadError>;

ReadResult readAllowedKeyFileValuesWithError(
    const QString &path, QStringView section, const QSet<QString> &allowedKeys);

} // namespace key_file_reader_detail

std::optional<AllowedValues> readAllowedKeyFileValues(
    const QString &path, QStringView section, const QSet<QString> &allowedKeys);
