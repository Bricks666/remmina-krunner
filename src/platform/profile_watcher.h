// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include <QStringList>

#include <functional>

class ProfileWatcher {
public:
    using ChangedCallback = std::function<void()>;

    virtual ~ProfileWatcher() = default;
    [[nodiscard]] virtual bool replacePaths(const QStringList &paths,
                                            ChangedCallback callback) = 0;
    virtual void clear() = 0;
};
