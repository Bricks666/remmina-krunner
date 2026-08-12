// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include "application.h"

#include <QCoreApplication>
#include <QTextStream>

#include <optional>

namespace RemminaKRunner {
namespace {

QString kindName(InstanceKind kind)
{
    switch (kind) {
    case InstanceKind::Native:
        return QStringLiteral("native");
    case InstanceKind::Flatpak:
        return QStringLiteral("flatpak");
    case InstanceKind::Snap:
        return QStringLiteral("snap");
    }
    return QStringLiteral("none");
}

std::optional<InstanceKind> selectedKind(const RegistrySnapshot &snapshot)
{
    if (snapshot.selectedId.isEmpty()) {
        return std::nullopt;
    }
    const RemminaInstance *selected = nullptr;
    for (const RemminaInstance &instance : snapshot.instances) {
        if (instance.id != snapshot.selectedId) {
            continue;
        }
        if (selected != nullptr) {
            return std::nullopt;
        }
        selected = &instance;
    }
    return selected == nullptr ? std::nullopt
                               : std::optional<InstanceKind>{selected->kind};
}

int runRescan(ApplicationBackend &backend, QTextStream &output) noexcept
{
    try {
        const RegistrySnapshot snapshot = backend.rescanAndRepair();
        int nativeCount = 0;
        int flatpakCount = 0;
        int snapCount = 0;
        for (const RemminaInstance &instance : snapshot.instances) {
            switch (instance.kind) {
            case InstanceKind::Native:
                ++nativeCount;
                break;
            case InstanceKind::Flatpak:
                ++flatpakCount;
                break;
            case InstanceKind::Snap:
                ++snapCount;
                break;
            }
        }

        const QString status = !snapshot.failedBackends.isEmpty()
            ? QStringLiteral("partial")
            : snapshot.instances.isEmpty() ? QStringLiteral("empty")
                                           : QStringLiteral("available");
        const std::optional<InstanceKind> selection = selectedKind(snapshot);
        output << QStringLiteral("scan_status=") << status << QLatin1Char('\n')
               << QStringLiteral("instances=") << snapshot.instances.size()
               << QLatin1Char('\n') << QStringLiteral("native=") << nativeCount
               << QLatin1Char('\n') << QStringLiteral("flatpak=") << flatpakCount
               << QLatin1Char('\n') << QStringLiteral("snap=") << snapCount
               << QLatin1Char('\n') << QStringLiteral("selected_type=")
               << (selection.has_value() ? kindName(*selection)
                                         : QStringLiteral("none"))
               << QLatin1Char('\n');
        output.flush();
        return output.status() == QTextStream::Ok ? 0 : 1;
    } catch (...) {
        output << QStringLiteral("scan_status=error\n");
        output.flush();
        return 1;
    }
}

} // namespace

int runApplication(QCoreApplication &application,
                   const QStringList &arguments,
                   ApplicationBackend &backend,
                   QTextStream &standardOutput,
                   QTextStream &standardError) noexcept
{
    const bool normalMode = arguments.size() == 1;
    const bool rescanMode = arguments.size() == 2
        && arguments.at(1) == QStringLiteral("--rescan");
    if (!normalMode && !rescanMode) {
        standardError << QStringLiteral("Usage: remmina-krunner [--rescan]\n");
        standardError.flush();
        return 64;
    }
    if (rescanMode) {
        return runRescan(backend, standardOutput);
    }

    try {
        backend.rescanAndRepair();
        return backend.startService(application);
    } catch (...) {
        standardError << QStringLiteral("Unable to start Remmina KRunner.\n");
        standardError.flush();
        return 1;
    }
}

} // namespace RemminaKRunner
