// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include <QtTest>

#include "core/key_file_reader.h"

#include <QFile>
#include <QTemporaryDir>

#include <sys/stat.h>

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
    void matchesVerifiedGlibWhitespace_data();
    void matchesVerifiedGlibWhitespace();
    void rejectsSemicolonComments();
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
    void followsSymlinkToRegularInput();
    void rejectsInvalidAndIncompleteUtf8_data();
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

void KeyFileReaderTest::matchesVerifiedGlibWhitespace_data()
{
    QTest::addColumn<QByteArray>("contents");
    QTest::addColumn<bool>("shouldParse");
    QTest::addColumn<bool>("shouldContainName");
    QTest::addColumn<QString>("expectedName");

    QTest::newRow("form-feed-before-group")
        << QByteArray("\f[remmina]\nname=Visible\n") << true << true
        << QStringLiteral("Visible");
    QTest::newRow("form-feed-before-key")
        << QByteArray("[remmina]\n\fname=Visible\n") << true << true
        << QStringLiteral("Visible");
    QTest::newRow("form-feed-before-equals")
        << QByteArray("[remmina]\nname\f=Visible\n") << true << true
        << QStringLiteral("Visible");
    QTest::newRow("form-feed-after-equals")
        << QByteArray("[remmina]\nname=\fVisible\n") << true << true
        << QStringLiteral("Visible");
    QTest::newRow("vertical-tab-before-group")
        << QByteArray("\v[remmina]\nname=Visible\n") << false << false << QString();
    QTest::newRow("vertical-tab-before-key")
        << QByteArray("[remmina]\n\vname=Visible\n") << true << false << QString();
    QTest::newRow("vertical-tab-before-equals")
        << QByteArray("[remmina]\nname\v=Visible\n") << true << false << QString();
    QTest::newRow("vertical-tab-after-equals")
        << QByteArray("[remmina]\nname=\vVisible\n") << true << true
        << QStringLiteral("\vVisible");
    QTest::newRow("form-feed-after-group")
        << QByteArray("[remmina]\f\nname=Visible\n") << false << false << QString();
    QTest::newRow("vertical-tab-after-group")
        << QByteArray("[remmina]\v\nname=Visible\n") << false << false << QString();
    QTest::newRow("trailing-form-feed-and-vertical-tab")
        << QByteArray("[remmina]\nname=Visible\v\f\n") << true << true
        << QStringLiteral("Visible\v\f");
}

void KeyFileReaderTest::matchesVerifiedGlibWhitespace()
{
    QFETCH(QByteArray, contents);
    QFETCH(bool, shouldParse);
    QFETCH(bool, shouldContainName);
    QFETCH(QString, expectedName);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeFile(directory, u"ascii-whitespace.remmina", contents);
    const auto result = readAllowedKeyFileValues(
        path, QStringView(u"remmina"), {QStringLiteral("name")});

    QCOMPARE(result.has_value(), shouldParse);
    if (!shouldParse) {
        return;
    }
    QCOMPARE(result->contains(QStringLiteral("name")), shouldContainName);
    if (shouldContainName) {
        QCOMPARE(result->value(QStringLiteral("name")), expectedName);
    }
}

void KeyFileReaderTest::rejectsSemicolonComments()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString beforeGroup = writeFile(
        directory,
        u"semicolon-before.remmina",
        QByteArray("; synthetic-semicolon-secret\n[remmina]\nname=Visible\n"));
    const QString insideGroup = writeFile(
        directory,
        u"semicolon-inside.remmina",
        QByteArray("[remmina]\n; synthetic-semicolon-secret\nname=Visible\n"));

    QVERIFY(!readAllowedKeyFileValues(
                 beforeGroup, QStringView(u"remmina"), {QStringLiteral("name")})
                 .has_value());
    QVERIFY(!readAllowedKeyFileValues(
                 insideGroup, QStringView(u"remmina"), {QStringLiteral("name")})
                 .has_value());
}

void KeyFileReaderTest::handlesCrLfFirstEqualsUnicodeAndEscapes()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeFile(
        directory,
        u"escaped-crlf.remmina",
        QByteArray("# comment\r\n"
                   "   # indented comment\r\n"
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

    const QString fifoPath = directory.filePath(QStringLiteral("profile.fifo"));
    const QByteArray encodedFifoPath = QFile::encodeName(fifoPath);
    QCOMPARE(::mkfifo(encodedFifoPath.constData(), 0600), 0);
    const auto fifoResult = key_file_reader_detail::readAllowedKeyFileValuesWithError(
        fifoPath, QStringView(u"remmina"), {QStringLiteral("name")});
    const auto *fifoError = std::get_if<key_file_reader_detail::ReadError>(&fifoResult);
    QVERIFY(fifoError != nullptr);
    QCOMPARE(*fifoError, key_file_reader_detail::ReadError::Unreadable);

    const auto deviceResult = key_file_reader_detail::readAllowedKeyFileValuesWithError(
        QStringLiteral("/dev/null"),
        QStringView(u"remmina"),
        {QStringLiteral("name")});
    const auto *deviceError = std::get_if<key_file_reader_detail::ReadError>(&deviceResult);
    QVERIFY(deviceError != nullptr);
    QCOMPARE(*deviceError, key_file_reader_detail::ReadError::Unreadable);
}

void KeyFileReaderTest::followsSymlinkToRegularInput()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString target = writeFile(
        directory, u"target.remmina", QByteArray("[remmina]\nname=Visible\n"));
    const QString link = directory.filePath(QStringLiteral("link.remmina"));
    QVERIFY(QFile::link(target, link));

    const auto result = readAllowedKeyFileValues(
        link, QStringView(u"remmina"), {QStringLiteral("name")});

    QVERIFY(result.has_value());
    QCOMPARE(result->value(QStringLiteral("name")), QStringLiteral("Visible"));
}

void KeyFileReaderTest::rejectsInvalidAndIncompleteUtf8_data()
{
    QTest::addColumn<QByteArray>("invalidBytes");
    QTest::addColumn<bool>("appendNewline");

    QTest::newRow("bad-continuation") << QByteArray("\xC3\x28", 2) << true;
    QTest::newRow("incomplete-at-eof") << QByteArray("\xE2\x82", 2) << false;
    QTest::newRow("overlong-two-byte") << QByteArray("\xC0\xAF", 2) << true;
    QTest::newRow("utf16-surrogate") << QByteArray("\xED\xA0\x80", 3) << true;
    QTest::newRow("above-unicode-maximum") << QByteArray("\xF4\x90\x80\x80", 4) << true;
    QTest::newRow("lone-continuation") << QByteArray("\x80", 1) << true;
}

void KeyFileReaderTest::rejectsInvalidAndIncompleteUtf8()
{
    QFETCH(QByteArray, invalidBytes);
    QFETCH(bool, appendNewline);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QByteArray contents("[remmina]\nname=Bad");
    contents.append(invalidBytes);
    if (appendNewline) {
        contents.append('\n');
    }
    const QString path = writeFile(directory, u"invalid-utf8.remmina", contents);

    QVERIFY(!readAllowedKeyFileValues(
                 path, QStringView(u"remmina"), {QStringLiteral("name")})
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
