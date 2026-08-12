// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include "core/profile_parser.h"

#include "core/key_file_reader.h"

#include <QSet>

#include <utility>

std::variant<ProfileRecord, ProfileParseError> parseRemminaProfile(
    const QString &sourcePath, const QString &launchPath, QString opaqueId)
{
    static const QSet<QString> allowedKeys{
        QStringLiteral("name"),
        QStringLiteral("server"),
        QStringLiteral("labels"),
        QStringLiteral("protocol"),
    };

    key_file_reader_detail::ReadResult readResult =
        key_file_reader_detail::readAllowedKeyFileValuesWithError(
            sourcePath, QStringView(u"remmina"), allowedKeys);
    if (const auto *error = std::get_if<key_file_reader_detail::ReadError>(&readResult)) {
        return *error == key_file_reader_detail::ReadError::Unreadable
            ? ProfileParseError::Unreadable
            : ProfileParseError::Malformed;
    }

    AllowedValues values = std::get<AllowedValues>(std::move(readResult));
    const QString name = values.take(QStringLiteral("name")).trimmed();
    if (name.isEmpty()) {
        return ProfileParseError::MissingName;
    }

    const QString server = values.take(QStringLiteral("server")).trimmed();
    const QString labelsDisplay = values.take(QStringLiteral("labels")).trimmed();
    const QString protocol = values.take(QStringLiteral("protocol")).trimmed();

    QStringList labels;
    for (const QString &part : labelsDisplay.split(',', Qt::KeepEmptyParts)) {
        const QString label = part.trimmed();
        if (!label.isEmpty()) {
            labels.append(label);
        }
    }

    return ProfileRecord{
        .opaqueId = std::move(opaqueId),
        .sourcePath = sourcePath,
        .launchPath = launchPath,
        .name = name,
        .server = server,
        .labels = std::move(labels),
        .labelsDisplay = labelsDisplay,
        .protocol = protocol,
    };
}
