// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include "platform/qt_profile_watcher.h"

#include <QDir>
#include <QSet>

#include <utility>

QtProfileWatcher::QtProfileWatcher(QObject *parent) : QObject(parent), watcher_(this) {}

QtProfileWatcher::~QtProfileWatcher() { clear(); }

bool QtProfileWatcher::replacePaths(const QStringList &paths, ChangedCallback callback) {
  clear();
  if (paths.isEmpty() || !callback) {
    return false;
  }

  QStringList normalizedPaths;
  QSet<QString> seenPaths;
  normalizedPaths.reserve(paths.size());
  for (const QString &path : paths) {
    if (path.isEmpty() || !QDir::isAbsolutePath(path)) {
      return false;
    }
    const QString normalized = QDir::cleanPath(path);
    if (normalized.isEmpty() || !QDir::isAbsolutePath(normalized)) {
      return false;
    }
    if (!seenPaths.contains(normalized)) {
      seenPaths.insert(normalized);
      normalizedPaths.append(normalized);
    }
  }
  if (normalizedPaths.isEmpty()) {
    return false;
  }

  const QStringList failedPaths = watcher_.addPaths(normalizedPaths);
  if (!failedPaths.isEmpty()) {
    clear();
    return false;
  }

  callback_ = std::move(callback);
  const quint64 installedGeneration = generation_;
  directoryConnection_ =
      connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, [this, installedGeneration](const QString &) {
        try {
          if (installedGeneration == generation_ && callback_) {
            const ChangedCallback callback = callback_;
            callback();
          }
        } catch (...) {
        }
      });
  fileConnection_ =
      connect(&watcher_, &QFileSystemWatcher::fileChanged, this, [this, installedGeneration](const QString &) {
        try {
          if (installedGeneration == generation_ && callback_) {
            const ChangedCallback callback = callback_;
            callback();
          }
        } catch (...) {
        }
      });
  return true;
}

void QtProfileWatcher::clear() {
  ++generation_;
  callback_ = {};
  QObject::disconnect(directoryConnection_);
  QObject::disconnect(fileConnection_);
  directoryConnection_ = {};
  fileConnection_ = {};

  QStringList watchedPaths = watcher_.files();
  watchedPaths.append(watcher_.directories());
  if (!watchedPaths.isEmpty()) {
    watcher_.removePaths(watchedPaths);
  }
}
