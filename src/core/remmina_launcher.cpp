// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include "core/remmina_launcher.h"

#include "core/instance_registry.h"
#include "core/profile_catalog.h"
#include "core/remmina_instance.h"
#include "platform/notifier.h"
#include "platform/process_launcher.h"

#include <QDir>
#include <QString>
#include <QStringList>

#include <utility>

namespace {

constexpr QLatin1StringView activationTokenName{"XDG_ACTIVATION_TOKEN"};

bool containsNul(QStringView value)
{
    return value.contains(QChar::Null);
}

bool validProgram(QStringView program)
{
    return !program.isEmpty() && QDir::isAbsolutePath(program.toString())
        && !containsNul(program);
}

bool validArguments(const QStringList &arguments)
{
    for (const QString &argument : arguments) {
        if (containsNul(argument)) {
            return false;
        }
    }
    return true;
}

} // namespace

RemminaLauncher::RemminaLauncher(InstanceRegistrySource &registry,
                                 ProfileCatalogSource &catalog,
                                 ProcessLauncher &processLauncher,
                                 Notifier &notifier)
    : RemminaLauncher(registry,
                      catalog,
                      processLauncher,
                      notifier,
                      [] { return QProcessEnvironment::systemEnvironment(); })
{
}

RemminaLauncher::RemminaLauncher(InstanceRegistrySource &registry,
                                 ProfileCatalogSource &catalog,
                                 ProcessLauncher &processLauncher,
                                 Notifier &notifier,
                                 EnvironmentProvider environmentProvider)
    : registry_(registry)
    , catalog_(catalog)
    , processLauncher_(processLauncher)
    , notifier_(notifier)
    , environmentProvider_(std::move(environmentProvider))
{
}

RemminaLaunchResult RemminaLauncher::create(QStringView activationToken) noexcept
{
    try {
        RemminaInstance instance;
        if (!selectedInstance(instance)) {
            return failure(RemminaLaunchResult::NoInstance);
        }
        QStringList arguments = instance.launcherPrefix;
        arguments.append(QStringLiteral("--new"));
        return launch(instance, std::move(arguments), activationToken);
    } catch (...) {
        return failure(RemminaLaunchResult::StartFailed);
    }
}

RemminaLaunchResult RemminaLauncher::connect(QStringView opaqueId,
                                             QStringView activationToken) noexcept
{
    try {
        RemminaInstance instance;
        if (!selectedInstance(instance)) {
            return failure(RemminaLaunchResult::NoInstance);
        }

        const ProfileRecord *record = catalog_.resolve(opaqueId);
        if (record == nullptr) {
            return failure(RemminaLaunchResult::MissingProfile);
        }
        const QString launchPath = record->launchPath;
        if (launchPath.isEmpty() || containsNul(launchPath)) {
            return failure(RemminaLaunchResult::StartFailed);
        }
        QStringList arguments = instance.launcherPrefix;
        arguments.append(QStringLiteral("--connect"));
        arguments.append(launchPath);
        return launch(instance, std::move(arguments), activationToken);
    } catch (...) {
        return failure(RemminaLaunchResult::StartFailed);
    }
}

bool RemminaLauncher::selectedInstance(RemminaInstance &selectedInstance) const
{
    const RegistrySnapshot snapshot = registry_.snapshot();
    if (snapshot.selectedId.isEmpty()) {
        return false;
    }

    const RemminaInstance *selected = nullptr;
    for (const RemminaInstance &instance : snapshot.instances) {
        if (instance.id != snapshot.selectedId) {
            continue;
        }
        if (selected != nullptr) {
            return false;
        }
        selected = &instance;
    }
    if (selected == nullptr) {
        return false;
    }
    selectedInstance = *selected;
    return true;
}

RemminaLaunchResult RemminaLauncher::launch(const RemminaInstance &instance,
                                            QStringList arguments,
                                            QStringView activationToken)
{
    if (!validProgram(instance.executable) || !validArguments(arguments)
        || containsNul(activationToken)) {
        return failure(RemminaLaunchResult::StartFailed);
    }
    QProcessEnvironment environment = environmentProvider_();
    environment.remove(QString{activationTokenName});
    if (!activationToken.isEmpty()) {
        environment.insert(QString{activationTokenName}, activationToken.toString());
    }
    const LaunchRequest request{
        .program = instance.executable,
        .arguments = std::move(arguments),
        .environment = std::move(environment),
    };
    if (!processLauncher_.startDetached(request)) {
        return failure(RemminaLaunchResult::StartFailed);
    }
    return RemminaLaunchResult::Started;
}

RemminaLaunchResult RemminaLauncher::failure(RemminaLaunchResult result) noexcept
{
    try {
        notifier_.showLaunchFailure();
    } catch (...) {
    }
    return result;
}
