// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include "core/selection_store.h"

#include <QString>

class KConfigSelectionStore final : public SelectionStore {
public:
    KConfigSelectionStore();
    explicit KConfigSelectionStore(QString filePath);

    [[nodiscard]] QString selectedId() const override;
    bool writeSelectedId(QStringView id) override;

private:
    QString configFile_;
};
