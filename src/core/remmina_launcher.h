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
class ProfileCatalogSource;
struct RemminaInstance;

enum class RemminaLaunchResult {
    Started,
    NoInstance,
    MissingProfile,
    StartFailed,
};

class RemminaLauncher {
public:
    using EnvironmentProvider = std::function<QProcessEnvironment()>;

    // The launcher is single-thread confined. All four object dependencies are
    // non-owning, are used on that same thread, and must outlive the launcher.
    RemminaLauncher(InstanceRegistrySource &registry,
                    ProfileCatalogSource &catalog,
                    ProcessLauncher &processLauncher,
                    Notifier &notifier);
    RemminaLauncher(InstanceRegistrySource &registry,
                    ProfileCatalogSource &catalog,
                    ProcessLauncher &processLauncher,
                    Notifier &notifier,
                    EnvironmentProvider environmentProvider);

    RemminaLaunchResult create(QStringView activationToken = {}) noexcept;
    RemminaLaunchResult connect(QStringView opaqueId,
                                QStringView activationToken = {}) noexcept;

private:
    [[nodiscard]] bool selectedInstance(RemminaInstance &instance) const;
    [[nodiscard]] RemminaLaunchResult launch(const RemminaInstance &instance,
                                             QStringList arguments,
                                             QStringView activationToken);
    [[nodiscard]] RemminaLaunchResult failure(RemminaLaunchResult result) noexcept;

    InstanceRegistrySource &registry_;
    ProfileCatalogSource &catalog_;
    ProcessLauncher &processLauncher_;
    Notifier &notifier_;
    EnvironmentProvider environmentProvider_;
};
