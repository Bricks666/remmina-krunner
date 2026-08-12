// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include "core/profile_locator.h"
#include "core/profile_repository.h"
#include "platform/profile_watcher.h"

#include <QList>
#include <QString>
#include <QStringView>

#include <functional>
#include <optional>
#include <variant>

enum class ProfileCatalogError {
    NoProfileDirectory,
    UnreadableDirectory,
};

using CatalogResult = std::variant<QList<ProfileRecord>, ProfileCatalogError>;
using ProfileDirectoryLocator =
    std::function<std::optional<LocatedProfileDirectory>(const RemminaInstance &)>;

class ProfileCatalog {
public:
    // Dependencies are non-owning and must outlive the catalog.
    ProfileCatalog(ProfileRepositorySource &repository, ProfileWatcher &watcher);
    ProfileCatalog(ProfileRepositorySource &repository,
                   ProfileWatcher &watcher,
                   ProfileDirectoryLocator locator);
    ~ProfileCatalog();

    ProfileCatalog(const ProfileCatalog &) = delete;
    ProfileCatalog &operator=(const ProfileCatalog &) = delete;
    ProfileCatalog(ProfileCatalog &&) = delete;
    ProfileCatalog &operator=(ProfileCatalog &&) = delete;

    [[nodiscard]] CatalogResult records(const RemminaInstance &instance);
    // The returned pointer remains valid only until the next mutating catalog call.
    [[nodiscard]] const ProfileRecord *resolve(QStringView opaqueId) const;
    void markDirty();
    void endSession();
    void reset();

private:
    void clearSnapshotAndWatches();

    ProfileRepositorySource &repository_;
    ProfileWatcher &watcher_;
    ProfileDirectoryLocator locator_;
    QList<ProfileRecord> records_;
    QString selectedInstanceId_;
    bool hasSelectedInstance_ = false;
    bool sessionActive_ = false;
    bool dirty_ = false;
};
