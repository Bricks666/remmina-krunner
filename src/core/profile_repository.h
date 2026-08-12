// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include "core/profile_parser.h"
#include "core/remmina_instance.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringView>

#include <functional>
#include <variant>

struct FileFingerprint {
    QString path;
    qint64 size;
    qint64 modifiedMilliseconds;
};

struct ProfileSnapshot {
    QList<ProfileRecord> profiles;
    QList<FileFingerprint> fingerprint;
};

enum class ProfileRepositoryError {
    NoProfileDirectory,
    UnreadableDirectory,
};

using ProfileParseResult = std::variant<ProfileRecord, ProfileParseError>;
using ProfileParserFunction =
    std::function<ProfileParseResult(const QString &, const QString &, QString)>;

namespace profile_repository_detail {

[[nodiscard]] QString opaqueProfileId(QStringView instanceId, QStringView canonicalIdentity);

} // namespace profile_repository_detail

class ProfileRepository {
public:
    // A repository retains mutable parse-cache state and must remain confined to one thread.
    explicit ProfileRepository(ProfileParserFunction parser = parseRemminaProfile);

    [[nodiscard]] std::variant<ProfileSnapshot, ProfileRepositoryError> load(
        const RemminaInstance &instance);

private:
    struct CacheEntry {
        QString instanceId;
        QString directory;
        QString canonicalIdentity;
        FileFingerprint fingerprint;
        quint64 device;
        quint64 inode;
        qint64 size;
        qint64 modifiedSeconds;
        qint64 modifiedNanoseconds;
        qint64 changedSeconds;
        qint64 changedNanoseconds;
        ProfileParseResult result;
    };

    ProfileParserFunction parser_;
    QHash<QString, CacheEntry> cache_;
    QString activeInstanceId_;
    QString activeDirectory_;
    bool hasActiveScope_ = false;
};
