// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include <QString>
#include <QStringList>

enum class InstanceKind {
    Native,
    Flatpak,
    Snap,
};

struct ProfileEnvironment {
    QString configHome;
    QString dataHome;
    QString legacyHome;
    QStringList systemDataHomes;
};

struct RemminaInstance {
    QString id;
    InstanceKind kind;
    QString displayName;
    QString executable;
    QStringList launcherPrefix;
    ProfileEnvironment profiles;
};
