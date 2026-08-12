// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include "dbus/dbus_types.h"

#include <QObject>
#include <QString>
#include <QVariantMap>

#include <chrono>

class InstanceRegistryControlSource;
class ProfileCatalogAccess;
class QTimer;
class RemminaLaunchSource;

namespace RemminaKRunner {

class RunnerService final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.krunner1")

public:
    // Dependencies are non-owning, same-thread confined, and must outlive the
    // service. The service does not register an object or bus name.
    RunnerService(InstanceRegistryControlSource &registry,
                  ProfileCatalogAccess &catalog,
                  RemminaLaunchSource &launcher,
                  QObject *parent = nullptr);
    RunnerService(InstanceRegistryControlSource &registry,
                  ProfileCatalogAccess &catalog,
                  RemminaLaunchSource &launcher,
                  std::chrono::milliseconds catalogIdleTimeout,
                  QObject *parent = nullptr);
    ~RunnerService() noexcept override;

    RunnerService(const RunnerService &) = delete;
    RunnerService &operator=(const RunnerService &) = delete;

public slots:
    RemoteMatches Match(const QString &query) noexcept;
    RunnerActions Actions() noexcept;
    void Run(const QString &matchId, const QString &actionId) noexcept;
    void Teardown() noexcept;
    QVariantMap Config() noexcept;
    void SetActivationToken(const QString &token) noexcept;

private:
    void endActiveSession(bool clearActivationToken) noexcept;
    void teardownEveryCall() noexcept;

    InstanceRegistryControlSource &registry_;
    ProfileCatalogAccess &catalog_;
    RemminaLaunchSource &launcher_;
    QTimer *catalogIdleTimer_;
    QString activationToken_;
    bool sessionActive_ = false;
    const RemoteMatches creationResult_;
    const RemoteMatches noInstanceError_;
    const RemoteMatches noProfilesError_;
    const RemoteMatches unreadableError_;
    const RemoteMatches internalError_;
    const QVariantMap config_;
};

} // namespace RemminaKRunner
