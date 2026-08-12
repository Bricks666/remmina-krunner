// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include <QtTest>

#include "core/profile_parser.h"

#include <QFile>
#include <QTemporaryDir>

namespace {

QString writeFile(QTemporaryDir &directory, QStringView fileName, const QByteArray &contents)
{
    const QString path = directory.filePath(fileName.toString());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size()) {
        qFatal("Unable to create profile parser test input");
    }
    file.close();
    return path;
}

QStringList recordStrings(const ProfileRecord &record)
{
    QStringList strings{
        record.opaqueId,
        record.sourcePath,
        record.launchPath,
        record.name,
        record.server,
        record.labelsDisplay,
        record.protocol,
    };
    strings.append(record.labels);
    return strings;
}

void verifyPrivateValuesAbsent(const ProfileRecord &record, const QStringList &privateValues)
{
    const QStringList strings = recordStrings(record);
    for (const QString &string : strings) {
        for (const QString &privateValue : privateValues) {
            QVERIFY2(!string.contains(privateValue), qPrintable(string));
        }
    }
}

} // namespace

class ProfileParserTest : public QObject {
    Q_OBJECT

private slots:
    void parsesSearchableFieldsAndCopiesCallerMetadata();
    void decodesEscapesAndSplitsLabels();
    void returnsEmptyMissingOptionalFields();
    void rejectsMissingOrWhitespaceOnlyName();
    void classifiesWrongGroupAndMalformedInput();
    void classifiesUnreadableAndNonregularInput();
    void rejectsInvalidUtf8AndOversizedInput();
    void doesNotRetainPrivateValuesInRecordOrDiagnostics();
};

void ProfileParserTest::parsesSearchableFieldsAndCopiesCallerMetadata()
{
    const QString sourcePath = QFINDTESTDATA("fixtures/valid.remmina");
    QVERIFY(!sourcePath.isEmpty());
    const QString launchPath = QStringLiteral("  /trusted/launch profile.remmina  ");
    const QString opaqueId = QStringLiteral(" opaque=id ");

    const auto result = parseRemminaProfile(sourcePath, launchPath, opaqueId);

    QVERIFY(std::holds_alternative<ProfileRecord>(result));
    const ProfileRecord &record = std::get<ProfileRecord>(result);
    QCOMPARE(record.sourcePath, sourcePath);
    QCOMPARE(record.launchPath, launchPath);
    QCOMPARE(record.opaqueId, opaqueId);
    QCOMPARE(record.name, QStringLiteral("Zürich Office"));
    QCOMPARE(record.server, QStringLiteral("rdp.example.test:3389?token=visible=part"));
    const QStringList expectedLabels{
        QStringLiteral("Finance East"),
        QStringLiteral("Operations"),
        QStringLiteral("Zürich"),
    };
    QCOMPARE(record.labels, expectedLabels);
    QCOMPARE(record.labelsDisplay, QStringLiteral("Finance East, Operations, Zürich"));
    QCOMPARE(record.protocol, QStringLiteral("RDP"));
}

void ProfileParserTest::decodesEscapesAndSplitsLabels()
{
    const QString sourcePath = QFINDTESTDATA("fixtures/escaped.remmina");
    QVERIFY(!sourcePath.isEmpty());

    const auto result = parseRemminaProfile(
        sourcePath, QStringLiteral("/launch/escaped.remmina"), QStringLiteral("escaped-id"));

    QVERIFY(std::holds_alternative<ProfileRecord>(result));
    const ProfileRecord &record = std::get<ProfileRecord>(result);
    QCOMPARE(record.name, QStringLiteral("Escaped Office"));
    QCOMPARE(record.server,
             QStringLiteral("rdp.example.test:3389?mode=fast session=primary"));
    QCOMPARE(record.labelsDisplay, QStringLiteral("Finance East, Ops\tTeam, , Lab Zone"));
    const QStringList expectedLabels{
        QStringLiteral("Finance East"),
        QStringLiteral("Ops\tTeam"),
        QStringLiteral("Lab Zone"),
    };
    QCOMPARE(record.labels, expectedLabels);
    QCOMPARE(record.protocol, QStringLiteral("RDP\\TLS"));
}

void ProfileParserTest::returnsEmptyMissingOptionalFields()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path =
        writeFile(directory, u"name-only.remmina", QByteArray("[remmina]\nname=  Visible  \n"));

    const auto result = parseRemminaProfile(
        path, QStringLiteral("/launch/name-only.remmina"), QStringLiteral("name-only"));

    QVERIFY(std::holds_alternative<ProfileRecord>(result));
    const ProfileRecord &record = std::get<ProfileRecord>(result);
    QCOMPARE(record.name, QStringLiteral("Visible"));
    QVERIFY(record.server.isEmpty());
    QVERIFY(record.labels.isEmpty());
    QVERIFY(record.labelsDisplay.isEmpty());
    QVERIFY(record.protocol.isEmpty());
}

void ProfileParserTest::rejectsMissingOrWhitespaceOnlyName()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString missing = writeFile(
        directory, u"missing-name.remmina", QByteArray("[remmina]\nserver=host.example.test\n"));
    const QString whitespace = writeFile(
        directory, u"empty-name.remmina", QByteArray("[remmina]\nname=\\s\\t\n"));

    const auto missingResult =
        parseRemminaProfile(missing, QStringLiteral("/launch/missing"), QStringLiteral("missing"));
    const auto whitespaceResult = parseRemminaProfile(
        whitespace, QStringLiteral("/launch/whitespace"), QStringLiteral("whitespace"));

    QVERIFY(std::holds_alternative<ProfileParseError>(missingResult));
    QVERIFY(std::get<ProfileParseError>(missingResult) == ProfileParseError::MissingName);
    QVERIFY(std::holds_alternative<ProfileParseError>(whitespaceResult));
    QVERIFY(std::get<ProfileParseError>(whitespaceResult) == ProfileParseError::MissingName);
}

void ProfileParserTest::classifiesWrongGroupAndMalformedInput()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString wrongGroup = writeFile(
        directory, u"wrong-group.remmina", QByteArray("[other]\nname=Other\n"));
    const QString malformed = QFINDTESTDATA("fixtures/malformed.remmina");
    QVERIFY(!malformed.isEmpty());

    const auto wrongGroupResult = parseRemminaProfile(
        wrongGroup, QStringLiteral("/launch/wrong"), QStringLiteral("wrong"));
    const auto malformedResult = parseRemminaProfile(
        malformed, QStringLiteral("/launch/malformed"), QStringLiteral("malformed"));

    QVERIFY(std::holds_alternative<ProfileParseError>(wrongGroupResult));
    QVERIFY(std::get<ProfileParseError>(wrongGroupResult) == ProfileParseError::Malformed);
    QVERIFY(std::holds_alternative<ProfileParseError>(malformedResult));
    QVERIFY(std::get<ProfileParseError>(malformedResult) == ProfileParseError::Malformed);
}

void ProfileParserTest::classifiesUnreadableAndNonregularInput()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const auto missing = parseRemminaProfile(directory.filePath(QStringLiteral("missing.remmina")),
                                             QStringLiteral("/launch/missing"),
                                             QStringLiteral("missing"));
    const auto directoryResult = parseRemminaProfile(directory.path(),
                                                     QStringLiteral("/launch/directory"),
                                                     QStringLiteral("directory"));

    QVERIFY(std::holds_alternative<ProfileParseError>(missing));
    QVERIFY(std::get<ProfileParseError>(missing) == ProfileParseError::Unreadable);
    QVERIFY(std::holds_alternative<ProfileParseError>(directoryResult));
    QVERIFY(std::get<ProfileParseError>(directoryResult) == ProfileParseError::Unreadable);
}

void ProfileParserTest::rejectsInvalidUtf8AndOversizedInput()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QByteArray invalid("[remmina]\nname=Bad");
    invalid.append(char(0xE2));
    invalid.append(char(0x82));
    const QString invalidPath = writeFile(directory, u"invalid.remmina", invalid);

    QByteArray oversized("[remmina]\nname=");
    oversized.append(QByteArray(20 * 1024, 'x'));
    oversized.append('\n');
    const QString oversizedPath = writeFile(directory, u"oversized.remmina", oversized);

    const auto invalidResult = parseRemminaProfile(
        invalidPath, QStringLiteral("/launch/invalid"), QStringLiteral("invalid"));
    const auto oversizedResult = parseRemminaProfile(
        oversizedPath, QStringLiteral("/launch/oversized"), QStringLiteral("oversized"));

    QVERIFY(std::holds_alternative<ProfileParseError>(invalidResult));
    QVERIFY(std::get<ProfileParseError>(invalidResult) == ProfileParseError::Malformed);
    QVERIFY(std::holds_alternative<ProfileParseError>(oversizedResult));
    QVERIFY(std::get<ProfileParseError>(oversizedResult) == ProfileParseError::Malformed);
}

void ProfileParserTest::doesNotRetainPrivateValuesInRecordOrDiagnostics()
{
    const QString valid = QFINDTESTDATA("fixtures/valid.remmina");
    const QString escaped = QFINDTESTDATA("fixtures/escaped.remmina");
    const QString malformed = QFINDTESTDATA("fixtures/malformed.remmina");
    QVERIFY(!valid.isEmpty());
    QVERIFY(!escaped.isEmpty());
    QVERIFY(!malformed.isEmpty());

    const auto validResult = parseRemminaProfile(
        valid, QStringLiteral("/launch/valid"), QStringLiteral("valid-id"));
    const auto escapedResult = parseRemminaProfile(
        escaped, QStringLiteral("/launch/escaped"), QStringLiteral("escaped-id"));
    const auto malformedResult = parseRemminaProfile(
        malformed, QStringLiteral("/launch/malformed"), QStringLiteral("malformed-id"));

    QVERIFY(std::holds_alternative<ProfileRecord>(validResult));
    verifyPrivateValuesAbsent(std::get<ProfileRecord>(validResult),
                              {QStringLiteral("synthetic-password-secret"),
                               QStringLiteral("synthetic-gateway-secret"),
                               QStringLiteral("synthetic-username-secret"),
                               QStringLiteral("synthetic-notes-secret")});
    QVERIFY(std::holds_alternative<ProfileRecord>(escapedResult));
    verifyPrivateValuesAbsent(std::get<ProfileRecord>(escapedResult),
                              {QStringLiteral("escaped-password-secret"),
                               QStringLiteral("escaped-gateway-secret"),
                               QStringLiteral("escaped-username-secret"),
                               QStringLiteral("escaped-notes-secret")});
    QVERIFY(std::holds_alternative<ProfileParseError>(malformedResult));
    QVERIFY(std::get<ProfileParseError>(malformedResult) == ProfileParseError::Malformed);
}

QTEST_APPLESS_MAIN(ProfileParserTest)

#include "test_profile_parser.moc"
