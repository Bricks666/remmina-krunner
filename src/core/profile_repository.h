// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include "core/profile_locator.h"
#include "core/profile_parser.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <functional>
#include <optional>
#include <variant>

struct FileFingerprint {
  QString path;
  qint64 size;
  qint64 modifiedMilliseconds;
};

struct DirectoryFingerprint {
  QString canonicalPath;
  quint64 device = 0;
  quint64 inode = 0;
  QStringList symlinkParentPaths;
};

struct ProfileSnapshot {
  QList<ProfileRecord> profiles;
  QList<FileFingerprint> fingerprint;
  LocatedProfileDirectory directory;
  DirectoryFingerprint directoryFingerprint;
};

enum class ProfileRepositoryError {
  NoProfileDirectory,
  UnreadableDirectory,
};

using RepositoryLoadResult = std::variant<ProfileSnapshot, ProfileRepositoryError>;
using ProfileParseResult = std::variant<ProfileRecord, ProfileParseError>;
using ProfileParserFunction = std::function<ProfileParseResult(const QString &, const QString &, QString)>;

class ProfileRepositorySource {
public:
  // Repository sources are confined to the thread that owns their catalog.
  virtual ~ProfileRepositorySource() = default;
  [[nodiscard]] virtual RepositoryLoadResult load(const RemminaInstance &instance) = 0;
};

namespace profile_repository_detail {

[[nodiscard]] QString opaqueProfileId(QStringView instanceId, QStringView canonicalIdentity);
[[nodiscard]] std::optional<DirectoryFingerprint> inspectDirectory(QStringView lexicalPath);
[[nodiscard]] bool directoryMatches(QStringView lexicalPath, const DirectoryFingerprint &fingerprint);

} // namespace profile_repository_detail

class ProfileRepository final : public ProfileRepositorySource {
public:
  // A repository retains mutable parse-cache state and must remain confined to one thread.
  explicit ProfileRepository(ProfileParserFunction parser = parseRemminaProfile);

  [[nodiscard]] RepositoryLoadResult load(const RemminaInstance &instance) override;

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
