// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include "core/profile_repository.h"
#include "platform/profile_watcher.h"

#include <QList>
#include <QString>
#include <QStringView>

#include <memory>
#include <variant>

enum class ProfileCatalogError {
    NoProfileDirectory,
    UnreadableDirectory,
};

using CatalogResult = std::variant<QList<ProfileRecord>, ProfileCatalogError>;

class ProfileCatalog {
public:
    // The catalog is same-thread confined. Dependencies are non-owning, are used on
    // that same thread, and must outlive the catalog.
    ProfileCatalog(ProfileRepositorySource &repository, ProfileWatcher &watcher);
    ~ProfileCatalog() noexcept;

    ProfileCatalog(const ProfileCatalog &) = delete;
    ProfileCatalog &operator=(const ProfileCatalog &) = delete;
    ProfileCatalog(ProfileCatalog &&) = delete;
    ProfileCatalog &operator=(ProfileCatalog &&) = delete;

    // Dependency exceptions may propagate from records(); its refresh state remains
    // retry-safe. reset() and destruction contain watcher cleanup exceptions.
    [[nodiscard]] CatalogResult records(const RemminaInstance &instance);
    // The returned pointer remains valid only until the next mutating catalog call.
    [[nodiscard]] const ProfileRecord *resolve(QStringView opaqueId) const;
    void markDirty() noexcept;
    void endSession();
    void reset() noexcept;

private:
    struct CallbackLifetime;
    struct RefreshGuard;

    [[nodiscard]] CatalogResult repositoryError(ProfileRepositoryError error,
                                                const RemminaInstance &instance);
    [[nodiscard]] bool installWatches(const ProfileSnapshot &snapshot);
    void installCleanSnapshot(const ProfileSnapshot &snapshot);
    void abandonRefresh() noexcept;
    void deactivateCallback() noexcept;
    void clearWatcherNoexcept() noexcept;
    void clearSnapshotAndWatches() noexcept;

    ProfileRepositorySource &repository_;
    ProfileWatcher &watcher_;
    std::shared_ptr<CallbackLifetime> callbackLifetime_;
    QList<ProfileRecord> records_;
    LocatedProfileDirectory cleanDirectory_;
    DirectoryFingerprint cleanDirectoryFingerprint_;
    QString selectedInstanceId_;
    bool hasSelectedInstance_ = false;
    bool sessionActive_ = false;
    bool dirty_ = false;
    bool hasCleanSnapshot_ = false;
};
