// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include "platform/freedesktop_notifier.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QString>
#include <QStringList>
#include <QVariantMap>

QDBusMessage freedesktop_notifier_detail::launchFailureMessage()
{
    QDBusMessage message = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("/org/freedesktop/Notifications"),
        QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("Notify"));
    message << QString{launchFailureTitle} << uint{0} << QString{}
            << QString{launchFailureTitle} << QString{launchFailureBody}
            << QStringList{} << QVariantMap{} << -1;
    return message;
}

void FreedesktopNotifier::showLaunchFailure() noexcept
{
    try {
        constexpr int notificationTimeoutMilliseconds = 1000;
        static_cast<void>(QDBusConnection::sessionBus().asyncCall(
            freedesktop_notifier_detail::launchFailureMessage(),
            notificationTimeoutMilliseconds));
    } catch (...) {
        // Notification delivery must never cross the runner boundary.
    }
}
