// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include "platform/profile_watcher.h"

#include <QFileSystemWatcher>
#include <QMetaObject>
#include <QObject>

class QtProfileWatcher final : public QObject, public ProfileWatcher {
public:
    explicit QtProfileWatcher(QObject *parent = nullptr);
    ~QtProfileWatcher() override;

    [[nodiscard]] bool replacePaths(const QStringList &paths,
                                    ChangedCallback callback) override;
    void clear() override;

private:
    QFileSystemWatcher watcher_;
    ChangedCallback callback_;
    QMetaObject::Connection directoryConnection_;
    QMetaObject::Connection fileConnection_;
    quint64 generation_ = 0;
};
