// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include "core/matcher.h"

#include <algorithm>

namespace {

constexpr double exactScore = 1.00;
constexpr double prefixScore = 0.90;
constexpr double substringScore = 0.75;

ParsedQuery ignoredQuery()
{
    return {QueryKind::Ignore, {}};
}

double relationshipScore(const QString &field, const QString &token)
{
    if (field == token) {
        return exactScore;
    }
    if (field.startsWith(token)) {
        return prefixScore;
    }
    if (field.contains(token)) {
        return substringScore;
    }
    return 0.0;
}

QStringList foldedSearchableFields(const ProfileRecord &profile)
{
    QStringList fields;
    fields.reserve(2 + profile.labels.size());
    fields.append(profile.name.toCaseFolded());
    fields.append(profile.server.toCaseFolded());
    for (const QString &label : profile.labels) {
        fields.append(label.toCaseFolded());
    }
    return fields;
}

} // namespace

ParsedQuery parseRunnerQuery(QStringView query)
{
    constexpr qsizetype triggerLength = 3;
    if (query.size() < triggerLength
        || query.first(triggerLength).toString().toCaseFolded() != QStringLiteral("rem")) {
        return ignoredQuery();
    }

    if (query.size() == triggerLength) {
        return ignoredQuery();
    }
    if (!query.at(triggerLength).isSpace()) {
        return ignoredQuery();
    }

    QStringList tokens;
    qsizetype position = triggerLength;
    while (position < query.size()) {
        while (position < query.size() && query.at(position).isSpace()) {
            ++position;
        }
        const qsizetype tokenStart = position;
        while (position < query.size() && !query.at(position).isSpace()) {
            ++position;
        }
        if (position > tokenStart) {
            tokens.append(query.sliced(tokenStart, position - tokenStart).toString().toCaseFolded());
        }
    }

    if (tokens.isEmpty()) {
        return ignoredQuery();
    }
    if (tokens.size() == 1 && tokens.constFirst() == QStringLiteral("new")) {
        return {QueryKind::Create, {}};
    }
    return {QueryKind::Lookup, tokens};
}

QList<SearchMatch> matchProfiles(const QList<ProfileRecord> &profiles, const QStringList &tokens)
{
    if (tokens.isEmpty()) {
        return {};
    }

    QStringList foldedTokens;
    foldedTokens.reserve(tokens.size());
    for (const QString &token : tokens) {
        if (token.isEmpty()) {
            return {};
        }
        foldedTokens.append(token.toCaseFolded());
    }

    QList<SearchMatch> matches;
    matches.reserve(profiles.size());
    for (const ProfileRecord &profile : profiles) {
        const QStringList fields = foldedSearchableFields(profile);
        double relevance = exactScore;
        bool matchesAllTokens = true;

        for (const QString &token : foldedTokens) {
            double bestScore = 0.0;
            for (const QString &field : fields) {
                bestScore = std::max(bestScore, relationshipScore(field, token));
            }
            if (bestScore == 0.0) {
                matchesAllTokens = false;
                break;
            }
            relevance = std::min(relevance, bestScore);
        }

        if (matchesAllTokens) {
            matches.append({profile, relevance});
        }
    }

    std::stable_sort(matches.begin(), matches.end(), [](const SearchMatch &left,
                                                        const SearchMatch &right) {
        if (left.relevance != right.relevance) {
            return left.relevance > right.relevance;
        }

        const int nameOrder =
            left.record.name.toCaseFolded().compare(right.record.name.toCaseFolded());
        if (nameOrder != 0) {
            return nameOrder < 0;
        }

        const int serverOrder =
            left.record.server.toCaseFolded().compare(right.record.server.toCaseFolded());
        if (serverOrder != 0) {
            return serverOrder < 0;
        }
        return left.record.sourcePath < right.record.sourcePath;
    });

    return matches;
}
