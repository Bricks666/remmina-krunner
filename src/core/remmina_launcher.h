// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include <QProcessEnvironment>
#include <QStringList>
#include <QStringView>

#include <functional>

class InstanceRegistrySource;
class Notifier;
class ProcessLauncher;
class ProfileCatalogReadSource;
struct RemminaInstance;

enum class RemminaLaunchResult {
    Started,
    NoInstance,
    MissingProfile,
    StartFailed,
};

class RemminaLaunchSource {
public:
    virtual ~RemminaLaunchSource() = default;
    // Calls are same-thread and receive only opaque IDs and a one-shot token.
    virtual RemminaLaunchResult create(QStringView activationToken = {}) = 0;
    virtual RemminaLaunchResult connect(QStringView opaqueId,
                                        QStringView activationToken = {}) = 0;
};

class RemminaLauncher final : public RemminaLaunchSource {
public:
    using EnvironmentProvider = std::function<QProcessEnvironment()>;

    // The launcher is single-thread confined. All four object dependencies are
    // non-owning, are used on that same thread, and must outlive the launcher.
    RemminaLauncher(InstanceRegistrySource &registry,
                    ProfileCatalogReadSource &catalog,
                    ProcessLauncher &processLauncher,
                    Notifier &notifier);
    RemminaLauncher(InstanceRegistrySource &registry,
                    ProfileCatalogReadSource &catalog,
                    ProcessLauncher &processLauncher,
                    Notifier &notifier,
                    EnvironmentProvider environmentProvider);

    RemminaLaunchResult create(QStringView activationToken = {}) noexcept override;
    RemminaLaunchResult connect(QStringView opaqueId,
                                QStringView activationToken = {}) noexcept override;

private:
    [[nodiscard]] bool selectedInstance(RemminaInstance &instance) const;
    [[nodiscard]] RemminaLaunchResult launch(const RemminaInstance &instance,
                                             QStringList arguments,
                                             QStringView activationToken);
    [[nodiscard]] RemminaLaunchResult failure(RemminaLaunchResult result) noexcept;

    InstanceRegistrySource &registry_;
    ProfileCatalogReadSource &catalog_;
    ProcessLauncher &processLauncher_;
    Notifier &notifier_;
    EnvironmentProvider environmentProvider_;
};
