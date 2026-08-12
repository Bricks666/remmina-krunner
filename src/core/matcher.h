// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include "core/profile_record.h"

#include <QList>
#include <QStringList>
#include <QStringView>

enum class QueryKind {
  Ignore,
  Create,
  Lookup,
};

struct ParsedQuery {
  QueryKind kind;
  QStringList tokens;
};

ParsedQuery parseRunnerQuery(QStringView query);

struct SearchMatch {
  ProfileRecord record;
  double relevance;
};

QList<SearchMatch> matchProfiles(const QList<ProfileRecord> &profiles, const QStringList &tokens);
