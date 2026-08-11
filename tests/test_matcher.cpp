// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include <QtTest>

#include "core/matcher.h"

#include <utility>

namespace {

ProfileRecord makeRecord(QString opaqueId,
                         QString name,
                         QString server,
                         QStringList labels = {})
{
    return {
        .opaqueId = std::move(opaqueId),
        .sourcePath = QStringLiteral("/profiles/profile.remmina"),
        .launchPath = QStringLiteral("/launch/profile.remmina"),
        .name = std::move(name),
        .server = std::move(server),
        .labels = std::move(labels),
        .labelsDisplay = QStringLiteral("display labels"),
        .protocol = QStringLiteral("RDP"),
    };
}

void compareRecord(const ProfileRecord &actual, const ProfileRecord &expected)
{
    QCOMPARE(actual.opaqueId, expected.opaqueId);
    QCOMPARE(actual.sourcePath, expected.sourcePath);
    QCOMPARE(actual.launchPath, expected.launchPath);
    QCOMPARE(actual.name, expected.name);
    QCOMPARE(actual.server, expected.server);
    QCOMPARE(actual.labels, expected.labels);
    QCOMPARE(actual.labelsDisplay, expected.labelsDisplay);
    QCOMPARE(actual.protocol, expected.protocol);
}

} // namespace

class MatcherTest : public QObject {
    Q_OBJECT

private slots:
    void ignoresEmptyUnrelatedAndBareTrigger();
    void parsesCreateCaseInsensitively();
    void parsesLookupTokens();
    void handlesUnicodeWhitespaceAndCaseFolding();
    void rejectsNonStandaloneTrigger();
    void matchesUnicodeCaseFoldEquivalents();
    void matchesOnlyVisibleProfileFields();
    void matchesSupportedServerForms();
    void requiresEveryTokenAcrossVisibleFields();
    void ranksBestRelationshipAndWeakestToken();
    void usesWeakestMixedTokenRelationship();
    void sortsTiesByFoldedVisibleMetadataAndSourcePath();
    void returnsNoMatchesForEmptyTokens();
    void doesNotMutateInput();
};

void MatcherTest::ignoresEmptyUnrelatedAndBareTrigger()
{
    const QList<QString> queries{
        QString{},
        QStringLiteral("ssh server"),
        QStringLiteral("rem"),
        QStringLiteral("rem   \t\n"),
        QStringLiteral("REM\u2003\u2009"),
    };

    for (const QString &query : queries) {
        const ParsedQuery parsed = parseRunnerQuery(query);
        QVERIFY(parsed.kind == QueryKind::Ignore);
        QVERIFY(parsed.tokens.isEmpty());
    }
}

void MatcherTest::parsesCreateCaseInsensitively()
{
    const QList<QString> queries{
        QStringLiteral("rem new"),
        QStringLiteral("REM NEW"),
        QStringLiteral("ReM\tNeW"),
    };

    for (const QString &query : queries) {
        const ParsedQuery parsed = parseRunnerQuery(query);
        QVERIFY(parsed.kind == QueryKind::Create);
        QVERIFY(parsed.tokens.isEmpty());
    }
}

void MatcherTest::parsesLookupTokens()
{
    const ParsedQuery parsed = parseRunnerQuery(QStringLiteral("rem new york"));

    QVERIFY(parsed.kind == QueryKind::Lookup);
    QCOMPARE(parsed.tokens, QStringList({QStringLiteral("new"), QStringLiteral("york")}));
}

void MatcherTest::handlesUnicodeWhitespaceAndCaseFolding()
{
    const ParsedQuery create = parseRunnerQuery(QStringLiteral("ReM\u2003NeW"));
    QVERIFY(create.kind == QueryKind::Create);
    QVERIFY(create.tokens.isEmpty());

    const ParsedQuery lookup = parseRunnerQuery(QStringLiteral("REM\u2003Stra\u00dfe\u2009Z\u00dcRICH"));
    QVERIFY(lookup.kind == QueryKind::Lookup);
    QCOMPARE(lookup.tokens,
             QStringList({QStringLiteral("stra\u00dfe"), QStringLiteral("z\u00fcrich")}));

    const ParsedQuery finalSigma = parseRunnerQuery(QStringLiteral("rem \u03c2"));
    QVERIFY(finalSigma.kind == QueryKind::Lookup);
    QCOMPARE(finalSigma.tokens, QStringList({QStringLiteral("\u03c3")}));
}

void MatcherTest::rejectsNonStandaloneTrigger()
{
    const QList<QString> queries{
        QStringLiteral("remote"),
        QStringLiteral("remote host"),
        QStringLiteral("rem:host"),
        QStringLiteral("rem-host"),
        QStringLiteral("xrem host"),
        QStringLiteral(" rem host"),
    };

    for (const QString &query : queries) {
        const ParsedQuery parsed = parseRunnerQuery(query);
        QVERIFY(parsed.kind == QueryKind::Ignore);
        QVERIFY(parsed.tokens.isEmpty());
    }
}

void MatcherTest::matchesUnicodeCaseFoldEquivalents()
{
    const QList<ProfileRecord> profiles{
        makeRecord(QStringLiteral("sigma"),
                   QStringLiteral("\u03c2"),
                   QStringLiteral("sigma.example.com")),
    };

    const QList<SearchMatch> matches = matchProfiles(profiles, {QStringLiteral("\u03c3")});
    QCOMPARE(matches.size(), 1);
    QCOMPARE(matches.constFirst().record.opaqueId, QStringLiteral("sigma"));
    QCOMPARE(matches.constFirst().relevance, 1.00);
}

void MatcherTest::matchesOnlyVisibleProfileFields()
{
    ProfileRecord profile = makeRecord(QStringLiteral("opaque-secret"),
                                       QStringLiteral("Stra\u00dfe Office"),
                                       QStringLiteral("visible.example.com"),
                                       {QStringLiteral("Production")});
    profile.sourcePath = QStringLiteral("/home/alice/source-secret.remmina");
    profile.launchPath = QStringLiteral("/runtime/launch-secret.remmina");
    profile.labelsDisplay = QStringLiteral("Production, display-secret");
    profile.protocol = QStringLiteral("rdp-secret");

    const QList<ProfileRecord> profiles{profile};

    const QList<SearchMatch> nameMatches =
        matchProfiles(profiles, {QStringLiteral("stra\u00dfe")});
    QCOMPARE(nameMatches.size(), 1);
    const SearchMatch &nameMatch = nameMatches.constFirst();
    QCOMPARE(nameMatch.record.opaqueId, QStringLiteral("opaque-secret"));
    QCOMPARE(nameMatch.relevance, 0.90);

    const QList<SearchMatch> labelMatches =
        matchProfiles(profiles, {QStringLiteral("production")});
    QCOMPARE(labelMatches.size(), 1);
    const SearchMatch &labelMatch = labelMatches.constFirst();
    QCOMPARE(labelMatch.record.opaqueId, QStringLiteral("opaque-secret"));
    QCOMPARE(labelMatch.relevance, 1.00);

    const QStringList hiddenQueries{
        QStringLiteral("rdp-secret"),
        QStringLiteral("alice"),
        QStringLiteral("source-secret"),
        QStringLiteral("launch-secret"),
        QStringLiteral("opaque-secret"),
        QStringLiteral("display-secret"),
    };
    for (const QString &query : hiddenQueries) {
        QVERIFY2(matchProfiles(profiles, {query}).isEmpty(), qPrintable(query));
    }
}

void MatcherTest::matchesSupportedServerForms()
{
    const QList<ProfileRecord> profiles{
        makeRecord(QStringLiteral("ipv4"),
                   QStringLiteral("IPv4 host"),
                   QStringLiteral("192.0.2.10")),
        makeRecord(QStringLiteral("ipv6"),
                   QStringLiteral("IPv6 host"),
                   QStringLiteral("[2001:db8::42]")),
        makeRecord(QStringLiteral("hostname"),
                   QStringLiteral("Domain host"),
                   QStringLiteral("bastion.example.com")),
        makeRecord(QStringLiteral("port"),
                   QStringLiteral("Port host"),
                   QStringLiteral("rdp.example.com:3389")),
    };

    const QList<QPair<QString, QString>> cases{
        {QStringLiteral("192.0.2.10"), QStringLiteral("ipv4")},
        {QStringLiteral("[2001:db8::42]"), QStringLiteral("ipv6")},
        {QStringLiteral("bastion.example.com"), QStringLiteral("hostname")},
        {QStringLiteral("rdp.example.com:3389"), QStringLiteral("port")},
    };
    for (const auto &[token, expectedId] : cases) {
        const QList<SearchMatch> matches = matchProfiles(profiles, {token});
        QCOMPARE(matches.size(), 1);
        QCOMPARE(matches.constFirst().record.opaqueId, expectedId);
        QCOMPARE(matches.constFirst().relevance, 1.00);
    }
}

void MatcherTest::requiresEveryTokenAcrossVisibleFields()
{
    const QList<ProfileRecord> profiles{
        makeRecord(QStringLiteral("split"),
                   QStringLiteral("finance"),
                   QStringLiteral("10.1.2.3"),
                   {QStringLiteral("urgent")}),
    };

    const QList<SearchMatch> matches =
        matchProfiles(profiles,
                      {QStringLiteral("finance"),
                       QStringLiteral("10.1.2.3"),
                       QStringLiteral("urgent")});
    QCOMPARE(matches.size(), 1);
    QCOMPARE(matches.constFirst().record.opaqueId, QStringLiteral("split"));
    QCOMPARE(matches.constFirst().relevance, 1.00);

    QVERIFY(matchProfiles(profiles,
                          {QStringLiteral("finance"), QStringLiteral("missing")})
                .isEmpty());
}

void MatcherTest::ranksBestRelationshipAndWeakestToken()
{
    const QList<ProfileRecord> profiles{
        makeRecord(QStringLiteral("substring"),
                   QStringLiteral("xalphax"),
                   QStringLiteral("host"),
                   {QStringLiteral("xbetax")}),
        makeRecord(QStringLiteral("prefix"),
                   QStringLiteral("alphabet"),
                   QStringLiteral("host"),
                   {QStringLiteral("betamax")}),
        makeRecord(QStringLiteral("exact"),
                   QStringLiteral("alpha"),
                   QStringLiteral("host"),
                   {QStringLiteral("beta")}),
        makeRecord(QStringLiteral("best-field"),
                   QStringLiteral("alpha suffix"),
                   QStringLiteral("alpha"),
                   {QStringLiteral("beta")}),
    };

    const QList<SearchMatch> matches =
        matchProfiles(profiles, {QStringLiteral("alpha"), QStringLiteral("beta")});
    QCOMPARE(matches.size(), 4);
    QCOMPARE(matches.at(0).record.opaqueId, QStringLiteral("exact"));
    QCOMPARE(matches.at(0).relevance, 1.00);
    QCOMPARE(matches.at(1).record.opaqueId, QStringLiteral("best-field"));
    QCOMPARE(matches.at(1).relevance, 1.00);
    QCOMPARE(matches.at(2).record.opaqueId, QStringLiteral("prefix"));
    QCOMPARE(matches.at(2).relevance, 0.90);
    QCOMPARE(matches.at(3).record.opaqueId, QStringLiteral("substring"));
    QCOMPARE(matches.at(3).relevance, 0.75);
}

void MatcherTest::usesWeakestMixedTokenRelationship()
{
    const QList<ProfileRecord> profiles{
        makeRecord(QStringLiteral("substring-weakest"),
                   QStringLiteral("alpha"),
                   QStringLiteral("host"),
                   {QStringLiteral("xbetax")}),
        makeRecord(QStringLiteral("prefix-weakest"),
                   QStringLiteral("alpha"),
                   QStringLiteral("host"),
                   {QStringLiteral("betamax")}),
    };

    const QList<SearchMatch> matches =
        matchProfiles(profiles, {QStringLiteral("alpha"), QStringLiteral("beta")});
    QCOMPARE(matches.size(), 2);
    QCOMPARE(matches.at(0).record.opaqueId, QStringLiteral("prefix-weakest"));
    QCOMPARE(matches.at(0).relevance, 0.90);
    QCOMPARE(matches.at(1).record.opaqueId, QStringLiteral("substring-weakest"));
    QCOMPARE(matches.at(1).relevance, 0.75);
}

void MatcherTest::sortsTiesByFoldedVisibleMetadataAndSourcePath()
{
    ProfileRecord sourceB = makeRecord(QStringLiteral("source-b"),
                                       QStringLiteral("Alpha"),
                                       QStringLiteral("SAME"),
                                       {QStringLiteral("needle")});
    sourceB.sourcePath = QStringLiteral("/b.remmina");
    ProfileRecord sourceA = makeRecord(QStringLiteral("source-a"),
                                       QStringLiteral("aLPHA"),
                                       QStringLiteral("same"),
                                       {QStringLiteral("needle")});
    sourceA.sourcePath = QStringLiteral("/a.remmina");
    ProfileRecord serverBeta = makeRecord(QStringLiteral("server-beta"),
                                          QStringLiteral("ALPHA"),
                                          QStringLiteral("Beta"),
                                          {QStringLiteral("needle")});
    serverBeta.sourcePath = QStringLiteral("/z.remmina");
    ProfileRecord nameBeta = makeRecord(QStringLiteral("name-beta"),
                                        QStringLiteral("Beta"),
                                        QStringLiteral("a"),
                                        {QStringLiteral("needle")});

    const QList<SearchMatch> matches =
        matchProfiles({nameBeta, sourceB, serverBeta, sourceA}, {QStringLiteral("needle")});
    QCOMPARE(matches.size(), 4);
    QCOMPARE(matches.at(0).record.opaqueId, QStringLiteral("server-beta"));
    QCOMPARE(matches.at(1).record.opaqueId, QStringLiteral("source-a"));
    QCOMPARE(matches.at(2).record.opaqueId, QStringLiteral("source-b"));
    QCOMPARE(matches.at(3).record.opaqueId, QStringLiteral("name-beta"));
}

void MatcherTest::returnsNoMatchesForEmptyTokens()
{
    const QList<ProfileRecord> profiles{
        makeRecord(QStringLiteral("profile"), QStringLiteral("name"), QStringLiteral("server")),
    };

    QVERIFY(matchProfiles(profiles, {}).isEmpty());
    QVERIFY(matchProfiles(profiles, {QString{}}).isEmpty());
}

void MatcherTest::doesNotMutateInput()
{
    QList<ProfileRecord> profiles{
        makeRecord(QStringLiteral("second"),
                   QStringLiteral("Zulu"),
                   QStringLiteral("z.example"),
                   {QStringLiteral("shared")}),
        makeRecord(QStringLiteral("first"),
                   QStringLiteral("Alpha"),
                   QStringLiteral("a.example"),
                   {QStringLiteral("shared")}),
    };
    profiles[0].sourcePath = QStringLiteral("/second.remmina");
    profiles[1].sourcePath = QStringLiteral("/first.remmina");
    const QList<ProfileRecord> before = profiles;

    const QList<SearchMatch> matches = matchProfiles(profiles, {QStringLiteral("shared")});

    QCOMPARE(matches.size(), 2);
    QCOMPARE(profiles.size(), before.size());
    for (qsizetype index = 0; index < profiles.size(); ++index) {
        compareRecord(profiles.at(index), before.at(index));
    }
}

QTEST_APPLESS_MAIN(MatcherTest)

#include "test_matcher.moc"
