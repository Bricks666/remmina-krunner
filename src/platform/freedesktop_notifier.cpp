// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include "platform/freedesktop_notifier.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QString>
#include <QStringList>
#include <QVariantMap>

void FreedesktopNotifier::showLaunchFailure() noexcept
{
    try {
        QDBusInterface notifications(
            QStringLiteral("org.freedesktop.Notifications"),
            QStringLiteral("/org/freedesktop/Notifications"),
            QStringLiteral("org.freedesktop.Notifications"),
            QDBusConnection::sessionBus());
        notifications.asyncCall(QStringLiteral("Notify"),
                                QString{launchFailureTitle},
                                uint{0},
                                QString{},
                                QString{launchFailureTitle},
                                QString{launchFailureBody},
                                QStringList{},
                                QVariantMap{},
                                -1);
    } catch (...) {
        // Notification delivery must never cross the runner boundary.
    }
}
