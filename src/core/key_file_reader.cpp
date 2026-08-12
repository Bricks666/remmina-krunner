// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include "core/key_file_reader.h"

#include <QByteArrayView>
#include <QFile>
#include <QFileInfo>

namespace {

constexpr qsizetype maximumLineBytes = 16 * 1024;
constexpr qint64 maximumTotalBytes = 1024 * 1024;
constexpr qsizetype maximumLineCount = 4096;

using ReadError = key_file_reader_detail::ReadError;
using ReadResult = key_file_reader_detail::ReadResult;

void wipe(QByteArray &bytes)
{
    if (!bytes.isEmpty()) {
        bytes.fill('\0');
        bytes.clear();
    }
}

bool isContinuationByte(unsigned char byte)
{
    return byte >= 0x80 && byte <= 0xBF;
}

bool hasContinuationBytes(QByteArrayView input, qsizetype offset, qsizetype count)
{
    if (offset + count > input.size()) {
        return false;
    }
    for (qsizetype index = 0; index < count; ++index) {
        if (!isContinuationByte(static_cast<unsigned char>(input.at(offset + index)))) {
            return false;
        }
    }
    return true;
}

bool isValidUtf8(QByteArrayView input)
{
    for (qsizetype index = 0; index < input.size();) {
        const auto lead = static_cast<unsigned char>(input.at(index));
        if (lead == 0) {
            return false;
        }
        if (lead <= 0x7F) {
            ++index;
            continue;
        }

        if (lead >= 0xC2 && lead <= 0xDF) {
            if (!hasContinuationBytes(input, index + 1, 1)) {
                return false;
            }
            index += 2;
            continue;
        }

        if (lead >= 0xE0 && lead <= 0xEF) {
            if (!hasContinuationBytes(input, index + 1, 2)) {
                return false;
            }
            const auto second = static_cast<unsigned char>(input.at(index + 1));
            if ((lead == 0xE0 && second < 0xA0) || (lead == 0xED && second > 0x9F)) {
                return false;
            }
            index += 3;
            continue;
        }

        if (lead >= 0xF0 && lead <= 0xF4) {
            if (!hasContinuationBytes(input, index + 1, 3)) {
                return false;
            }
            const auto second = static_cast<unsigned char>(input.at(index + 1));
            if ((lead == 0xF0 && second < 0x90) || (lead == 0xF4 && second > 0x8F)) {
                return false;
            }
            index += 4;
            continue;
        }

        return false;
    }
    return true;
}

bool isSyntacticWhitespace(char character)
{
    return character == ' ' || character == '\t';
}

qsizetype firstNonWhitespace(QByteArrayView line)
{
    qsizetype index = 0;
    while (index < line.size() && isSyntacticWhitespace(line.at(index))) {
        ++index;
    }
    return index;
}

QByteArrayView trimmed(QByteArrayView value)
{
    qsizetype start = 0;
    while (start < value.size() && isSyntacticWhitespace(value.at(start))) {
        ++start;
    }

    qsizetype end = value.size();
    while (end > start && isSyntacticWhitespace(value.at(end - 1))) {
        --end;
    }
    return value.sliced(start, end - start);
}

QByteArrayView leftTrimmed(QByteArrayView value)
{
    const qsizetype start = firstNonWhitespace(value);
    return value.sliced(start);
}

bool isValidKeyName(QStringView key)
{
    if (key.isEmpty()) {
        return false;
    }

    const qsizetype openingBracket = key.indexOf(u'[');
    const qsizetype closingBracket = key.indexOf(u']');
    if (openingBracket < 0) {
        return closingBracket < 0;
    }
    if (openingBracket == 0 || closingBracket != key.size() - 1
        || key.indexOf(u'[', openingBracket + 1) >= 0) {
        return false;
    }

    const QStringView locale = key.sliced(openingBracket + 1,
                                          closingBracket - openingBracket - 1);
    if (locale.isEmpty()) {
        return false;
    }
    for (const QChar character : locale) {
        if (!character.isLetterOrNumber() && character != u'-' && character != u'_'
            && character != u'.' && character != u'@') {
            return false;
        }
    }
    return true;
}

bool isValidGroupName(QByteArrayView group)
{
    if (group.isEmpty()) {
        return false;
    }
    for (const char rawByte : group) {
        const auto byte = static_cast<unsigned char>(rawByte);
        if (byte == '[' || byte == ']' || byte < 0x20 || byte == 0x7F) {
            return false;
        }
    }
    return true;
}

std::optional<QString> decodeValue(QByteArrayView encoded)
{
    QByteArray decoded;
    decoded.reserve(encoded.size());
    for (qsizetype index = 0; index < encoded.size(); ++index) {
        const char current = encoded.at(index);
        if (current != '\\') {
            decoded.append(current);
            continue;
        }
        if (++index == encoded.size()) {
            wipe(decoded);
            return std::nullopt;
        }

        switch (encoded.at(index)) {
        case 's':
            decoded.append(' ');
            break;
        case 'n':
            decoded.append('\n');
            break;
        case 't':
            decoded.append('\t');
            break;
        case 'r':
            decoded.append('\r');
            break;
        case '\\':
            decoded.append('\\');
            break;
        default:
            wipe(decoded);
            return std::nullopt;
        }
    }

    QString value = QString::fromUtf8(decoded);
    wipe(decoded);
    return value;
}

ReadResult malformed(QByteArray &line)
{
    wipe(line);
    return ReadError::Malformed;
}

} // namespace

namespace key_file_reader_detail {

ReadResult readAllowedKeyFileValuesWithError(
    const QString &path, QStringView section, const QSet<QString> &allowedKeys)
{
    const QFileInfo fileInfo(path);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        return ReadError::Unreadable;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return ReadError::Unreadable;
    }

    AllowedValues values;
    QSet<QString> invalidValues;
    bool hasSection = false;
    bool inRequestedSection = false;
    bool hasRequestedSection = false;
    qint64 totalBytes = 0;
    qsizetype lineCount = 0;

    while (!file.atEnd()) {
        QByteArray line = file.readLine(maximumLineBytes + 1);
        if (line.isEmpty() && file.error() != QFile::NoError) {
            return ReadError::Unreadable;
        }

        ++lineCount;
        if (lineCount > maximumLineCount || line.size() > maximumLineBytes
            || totalBytes > maximumTotalBytes - line.size()
            || (!line.endsWith('\n') && !file.atEnd())) {
            return malformed(line);
        }
        totalBytes += line.size();

        if (!isValidUtf8(QByteArrayView(line))) {
            return malformed(line);
        }

        if (line.endsWith('\n')) {
            line.chop(1);
            if (line.endsWith('\r')) {
                line.chop(1);
            }
        }

        const QByteArrayView lineView(line);
        const qsizetype contentStart = firstNonWhitespace(lineView);
        if (contentStart == lineView.size() || lineView.at(contentStart) == '#'
            || lineView.at(contentStart) == ';') {
            wipe(line);
            continue;
        }

        const QByteArrayView contentView = lineView.sliced(contentStart);
        if (contentView.at(0) == '[') {
            const QByteArrayView header = trimmed(contentView);
            if (header.size() < 3 || header.back() != ']') {
                return malformed(line);
            }
            const QByteArrayView encodedSection = header.sliced(1, header.size() - 2);
            if (!isValidGroupName(encodedSection)) {
                return malformed(line);
            }

            const QString decodedSection = QString::fromUtf8(encodedSection);
            hasSection = true;
            inRequestedSection = decodedSection == section;
            hasRequestedSection = hasRequestedSection || inRequestedSection;
            wipe(line);
            continue;
        }

        if (!hasSection) {
            return malformed(line);
        }

        const qsizetype equals = contentView.indexOf('=');
        if (equals < 0) {
            return malformed(line);
        }
        const QByteArrayView encodedKey = trimmed(contentView.first(equals));
        if (encodedKey.isEmpty()) {
            return malformed(line);
        }

        const QString key = QString::fromUtf8(encodedKey);
        if (!isValidKeyName(key)) {
            return malformed(line);
        }
        if (!inRequestedSection || !allowedKeys.contains(key)) {
            wipe(line);
            continue;
        }

        const QByteArrayView encodedValue = leftTrimmed(contentView.sliced(equals + 1));
        std::optional<QString> value = decodeValue(encodedValue);
        if (!value.has_value()) {
            values.remove(key);
            invalidValues.insert(key);
            wipe(line);
            continue;
        }
        invalidValues.remove(key);
        values.insert(key, std::move(*value));
        wipe(line);
    }

    if (file.error() != QFile::NoError) {
        return ReadError::Unreadable;
    }
    if (!hasRequestedSection || !invalidValues.isEmpty()) {
        return ReadError::Malformed;
    }
    return values;
}

} // namespace key_file_reader_detail

std::optional<AllowedValues> readAllowedKeyFileValues(
    const QString &path, QStringView section, const QSet<QString> &allowedKeys)
{
    key_file_reader_detail::ReadResult result =
        key_file_reader_detail::readAllowedKeyFileValuesWithError(path, section, allowedKeys);
    if (const auto *values = std::get_if<AllowedValues>(&result)) {
        return std::move(*values);
    }
    return std::nullopt;
}
