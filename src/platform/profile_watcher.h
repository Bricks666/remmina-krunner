// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include <QStringList>

#include <functional>

class ProfileWatcher {
public:
  using ChangedCallback = std::function<void()>;

  // Watchers and callbacks are confined to one thread. Callbacks run on that thread.
  // clear() synchronously deactivates the registered callback. A failed replacement
  // leaves no callback or path active.
  virtual ~ProfileWatcher() = default;
  [[nodiscard]] virtual bool replacePaths(const QStringList &paths, ChangedCallback callback) = 0;
  virtual void clear() = 0;
};
