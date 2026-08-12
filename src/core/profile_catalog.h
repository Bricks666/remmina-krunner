// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include "core/profile_repository.h"
#include "platform/profile_watcher.h"

#include <QList>
#include <QString>
#include <QStringView>

#include <memory>
#include <optional>
#include <variant>

enum class ProfileCatalogError {
  NoProfileDirectory,
  UnreadableDirectory,
};

using CatalogResult = std::variant<QList<ProfileRecord>, ProfileCatalogError>;

class ProfileCatalogReadSource {
public:
  virtual ~ProfileCatalogReadSource() = default;
  // Resolution is bound to the caller's current registry selection and returns
  // an independent value rather than catalog-owned storage.
  [[nodiscard]] virtual std::optional<ProfileRecord> resolve(QStringView expectedInstanceId,
                                                             QStringView opaqueId) const = 0;
};

class ProfileCatalogAccess : public ProfileCatalogReadSource {
public:
  ~ProfileCatalogAccess() override = default;
  // Implementations are non-owning service dependencies and are called on
  // their owning thread.
  [[nodiscard]] virtual CatalogResult records(const RemminaInstance &instance) = 0;
  virtual void endSession() = 0;
  virtual void reset() = 0;
};

class ProfileCatalog final : public ProfileCatalogAccess {
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
  [[nodiscard]] CatalogResult records(const RemminaInstance &instance) override;
  [[nodiscard]] std::optional<ProfileRecord> resolve(QStringView expectedInstanceId,
                                                     QStringView opaqueId) const override;
  void markDirty() noexcept;
  void endSession() override;
  void reset() noexcept override;

private:
  struct CallbackLifetime;
  struct RefreshGuard;

  [[nodiscard]] CatalogResult repositoryError(ProfileRepositoryError error, const RemminaInstance &instance);
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
