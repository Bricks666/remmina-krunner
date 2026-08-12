// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include <QtTest>

#include "core/key_file_reader.h"

#include <QFile>
#include <QTemporaryDir>

namespace {

QString writeFile(QTemporaryDir &directory, QStringView fileName, const QByteArray &contents)
{
    const QString path = directory.filePath(fileName.toString());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size()) {
        qFatal("Unable to create key-file test input");
    }
    file.close();
    return path;
}

QSet<QString> searchableKeys()
{
    return {
        QStringLiteral("name"),
        QStringLiteral("server"),
        QStringLiteral("labels"),
        QStringLiteral("protocol"),
    };
}

void verifyPrivateValuesAbsent(const AllowedValues &values, const QStringList &privateValues)
{
    for (auto iterator = values.cbegin(); iterator != values.cend(); ++iterator) {
        for (const QString &privateValue : privateValues) {
            QVERIFY2(!iterator.key().contains(privateValue), qPrintable(iterator.key()));
            QVERIFY2(!iterator.value().contains(privateValue), qPrintable(iterator.key()));
        }
    }
}

} // namespace

class KeyFileReaderTest : public QObject {
    Q_OBJECT

private slots:
    void readsOnlyAllowlistedValuesFromExactSection();
    void acceptsSyntacticWhitespaceAroundExactSectionHeader();
    void handlesCrLfFirstEqualsUnicodeAndEscapes();
    void preservesMeaningfulTrailingValueWhitespace();
    void usesLastDuplicateAllowedValue();
    void letsValidLastDuplicateReplaceInvalidEscape();
    void ignoresUnknownValuesWithoutDecodingEscapes();
    void rejectsUnknownValueWithInvalidUtf8WithoutRetainingIt();
    void rejectsUnknownAndDanglingEscapesForAllowedValues();
    void rejectsOversizedLines();
    void rejectsExcessiveLineCount();
    void rejectsExcessiveTotalBytes();
    void rejectsWrongOrMissingSection();
    void rejectsUnreadableAndNonregularInputs();
    void rejectsInvalidAndIncompleteUtf8();
    void rejectsMalformedSectionsAndLines();
};

void KeyFileReaderTest::readsOnlyAllowlistedValuesFromExactSection()
{
    const QString fixture = QFINDTESTDATA("fixtures/valid.remmina");
    QVERIFY(!fixture.isEmpty());

    const auto result =
        readAllowedKeyFileValues(fixture, QStringView(u"remmina"), searchableKeys());

    QVERIFY(result.has_value());
    QCOMPARE(result->size(), 4);
    QCOMPARE(result->value(QStringLiteral("name")), QStringLiteral("Zürich Office"));
    QCOMPARE(result->value(QStringLiteral("server")),
             QStringLiteral("rdp.example.test:3389?token=visible=part"));
    QCOMPARE(result->value(QStringLiteral("labels")),
             QStringLiteral("Finance East, Operations, Zürich"));
    QCOMPARE(result->value(QStringLiteral("protocol")), QStringLiteral("RDP"));
    QVERIFY(!result->contains(QStringLiteral("password")));
    QVERIFY(!result->contains(QStringLiteral("gateway")));
    QVERIFY(!result->contains(QStringLiteral("username")));
    QVERIFY(!result->contains(QStringLiteral("notes")));
    verifyPrivateValuesAbsent(*result,
                              {QStringLiteral("synthetic-password-secret"),
                               QStringLiteral("synthetic-gateway-secret"),
                               QStringLiteral("synthetic-username-secret"),
                               QStringLiteral("synthetic-notes-secret")});
}

void KeyFileReaderTest::acceptsSyntacticWhitespaceAroundExactSectionHeader()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeFile(directory,
                                   u"section-whitespace.remmina",
                                   QByteArray(" \t[remmina]\t \nname=Visible\n"));

    const auto result = readAllowedKeyFileValues(
        path, QStringView(u"remmina"), {QStringLiteral("name")});

    QVERIFY(result.has_value());
    QCOMPARE(result->value(QStringLiteral("name")), QStringLiteral("Visible"));
}

void KeyFileReaderTest::handlesCrLfFirstEqualsUnicodeAndEscapes()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeFile(
        directory,
        u"escaped-crlf.remmina",
        QByteArray("# comment\r\n"
                   "   ; indented comment\r\n"
                   "\t \r\n"
                   "[other]\r\n"
                   "name=Not selected\r\n"
                   "[remmina]\r\n"
                   "name= Caf\\sName\\nLine\\tTab\\rCarriage\\\\Slash \r\n"
                   "server= host=with=equals \r\n"
                   "labels=Z\xC3\xBCrich\r\n"));

    const auto result =
        readAllowedKeyFileValues(path, QStringView(u"remmina"), searchableKeys());

    QVERIFY(result.has_value());
    QCOMPARE(result->value(QStringLiteral("name")),
             QStringLiteral("Caf Name\nLine\tTab\rCarriage\\Slash "));
    QCOMPARE(result->value(QStringLiteral("server")), QStringLiteral("host=with=equals "));
    QCOMPARE(result->value(QStringLiteral("labels")), QStringLiteral("Zürich"));
}

void KeyFileReaderTest::preservesMeaningfulTrailingValueWhitespace()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeFile(
        directory, u"trailing-space.remmina", QByteArray("[remmina]\nname=  Visible \t\n"));

    const auto result = readAllowedKeyFileValues(
        path, QStringView(u"remmina"), {QStringLiteral("name")});

    QVERIFY(result.has_value());
    QCOMPARE(result->value(QStringLiteral("name")), QStringLiteral("Visible \t"));
}

void KeyFileReaderTest::usesLastDuplicateAllowedValue()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeFile(directory,
                                   u"duplicate.remmina",
                                   QByteArray("[remmina]\n"
                                              "name=First\n"
                                              "name=Second\n"));

    const auto result = readAllowedKeyFileValues(
        path, QStringView(u"remmina"), {QStringLiteral("name")});

    QVERIFY(result.has_value());
    QCOMPARE(result->value(QStringLiteral("name")), QStringLiteral("Second"));
}

void KeyFileReaderTest::letsValidLastDuplicateReplaceInvalidEscape()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeFile(directory,
                                   u"duplicate-invalid.remmina",
                                   QByteArray("[remmina]\n"
                                              "name=Invalid\\q\n"
                                              "name=Visible\n"));

    const auto result = readAllowedKeyFileValues(
        path, QStringView(u"remmina"), {QStringLiteral("name")});

    QVERIFY(result.has_value());
    QCOMPARE(result->value(QStringLiteral("name")), QStringLiteral("Visible"));
}

void KeyFileReaderTest::ignoresUnknownValuesWithoutDecodingEscapes()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeFile(directory,
                                   u"unknown-escape.remmina",
                                   QByteArray("[remmina]\n"
                                              "name=Visible\n"
                                              "password=private-invalid\\qescape\\\n"));

    const auto result = readAllowedKeyFileValues(
        path, QStringView(u"remmina"), {QStringLiteral("name")});

    QVERIFY(result.has_value());
    const AllowedValues expected{{QStringLiteral("name"), QStringLiteral("Visible")}};
    QCOMPARE(*result, expected);
    verifyPrivateValuesAbsent(*result, {QStringLiteral("private-invalid")});
}

void KeyFileReaderTest::rejectsUnknownValueWithInvalidUtf8WithoutRetainingIt()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QByteArray contents("[remmina]\nname=Visible\npassword=private-invalid-");
    contents.append(char(0xC3));
    contents.append(char(0x28));
    contents.append('\n');
    const QString path = writeFile(directory, u"unknown-invalid-utf8.remmina", contents);

    const auto result = readAllowedKeyFileValues(
        path, QStringView(u"remmina"), {QStringLiteral("name")});

    QVERIFY(!result.has_value());
}

void KeyFileReaderTest::rejectsUnknownAndDanglingEscapesForAllowedValues()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString unknown = writeFile(
        directory, u"unknown.remmina", QByteArray("[remmina]\nname=Bad\\qEscape\n"));
    const QString dangling = writeFile(
        directory, u"dangling.remmina", QByteArray("[remmina]\nname=Dangling\\\n"));

    QVERIFY(!readAllowedKeyFileValues(
                 unknown, QStringView(u"remmina"), {QStringLiteral("name")})
                 .has_value());
    QVERIFY(!readAllowedKeyFileValues(
                 dangling, QStringView(u"remmina"), {QStringLiteral("name")})
                 .has_value());
}

void KeyFileReaderTest::rejectsOversizedLines()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QByteArray contents("[remmina]\nname=Visible\nnotes=");
    contents.append(QByteArray(20 * 1024, 'x'));
    contents.append('\n');
    const QString path = writeFile(directory, u"oversized.remmina", contents);

    QVERIFY(!readAllowedKeyFileValues(
                 path, QStringView(u"remmina"), {QStringLiteral("name")})
                 .has_value());
}

void KeyFileReaderTest::rejectsExcessiveLineCount()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QByteArray contents("[remmina]\n");
    for (int line = 0; line < 4200; ++line) {
        contents.append("#\n");
    }
    const QString path = writeFile(directory, u"too-many-lines.remmina", contents);

    QVERIFY(!readAllowedKeyFileValues(
                 path, QStringView(u"remmina"), {QStringLiteral("name")})
                 .has_value());
}

void KeyFileReaderTest::rejectsExcessiveTotalBytes()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QByteArray contents("[remmina]\n");
    const QByteArray comment = QByteArray("#") + QByteArray(1023, 'x') + QByteArray("\n");
    for (int line = 0; line < 1100; ++line) {
        contents.append(comment);
    }
    const QString path = writeFile(directory, u"too-many-bytes.remmina", contents);

    QVERIFY(!readAllowedKeyFileValues(
                 path, QStringView(u"remmina"), {QStringLiteral("name")})
                 .has_value());
}

void KeyFileReaderTest::rejectsWrongOrMissingSection()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path =
        writeFile(directory, u"wrong-section.remmina", QByteArray("[not-remmina]\nname=Other\n"));

    QVERIFY(!readAllowedKeyFileValues(
                 path, QStringView(u"remmina"), {QStringLiteral("name")})
                 .has_value());
}

void KeyFileReaderTest::rejectsUnreadableAndNonregularInputs()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    QVERIFY(!readAllowedKeyFileValues(directory.filePath(QStringLiteral("missing.remmina")),
                                      QStringView(u"remmina"),
                                      {QStringLiteral("name")})
                 .has_value());
    QVERIFY(!readAllowedKeyFileValues(directory.path(),
                                      QStringView(u"remmina"),
                                      {QStringLiteral("name")})
                 .has_value());
}

void KeyFileReaderTest::rejectsInvalidAndIncompleteUtf8()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QByteArray invalid("[remmina]\nname=Bad");
    invalid.append(char(0xC3));
    invalid.append(char(0x28));
    invalid.append('\n');
    QByteArray incomplete("[remmina]\nname=Bad");
    incomplete.append(char(0xE2));
    incomplete.append(char(0x82));

    const QString invalidPath = writeFile(directory, u"invalid-utf8.remmina", invalid);
    const QString incompletePath = writeFile(directory, u"incomplete-utf8.remmina", incomplete);

    QVERIFY(!readAllowedKeyFileValues(
                 invalidPath, QStringView(u"remmina"), {QStringLiteral("name")})
                 .has_value());
    QVERIFY(!readAllowedKeyFileValues(
                 incompletePath, QStringView(u"remmina"), {QStringLiteral("name")})
                 .has_value());
}

void KeyFileReaderTest::rejectsMalformedSectionsAndLines()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString section = writeFile(
        directory, u"bad-section.remmina", QByteArray("[remmina] trailing\nname=Visible\n"));
    const QString line = writeFile(
        directory, u"bad-line.remmina", QByteArray("[remmina]\nnot-a-key-value\n"));
    const QString key = writeFile(
        directory, u"bad-key.remmina", QByteArray("[remmina]\nbad[key=value\n"));

    QVERIFY(!readAllowedKeyFileValues(
                 section, QStringView(u"remmina"), {QStringLiteral("name")})
                 .has_value());
    QVERIFY(!readAllowedKeyFileValues(
                 line, QStringView(u"remmina"), {QStringLiteral("name")})
                 .has_value());
    QVERIFY(!readAllowedKeyFileValues(
                 key, QStringView(u"remmina"), {QStringLiteral("name")})
                 .has_value());
}

QTEST_APPLESS_MAIN(KeyFileReaderTest)

#include "test_key_file_reader.moc"
