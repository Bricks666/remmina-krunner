// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include "core/kconfig_selection_store.h"

#include <KConfig>
#include <KConfigGroup>

#include <utility>

namespace {

constexpr auto selectedInstanceKey = "selectedInstance";

} // namespace

KConfigSelectionStore::KConfigSelectionStore()
    : configFile_(QStringLiteral("remmina-krunnerrc"))
{
}

KConfigSelectionStore::KConfigSelectionStore(QString filePath)
    : configFile_(std::move(filePath))
{
}

QString KConfigSelectionStore::selectedId() const
{
    KConfig config(configFile_, KConfig::NoGlobals);
    const KConfigGroup general(&config, QStringLiteral("General"));
    return general.readEntry(selectedInstanceKey, QString{});
}

bool KConfigSelectionStore::writeSelectedId(QStringView id)
{
    KConfig config(configFile_, KConfig::NoGlobals);
    KConfigGroup general(&config, QStringLiteral("General"));
    if (id.isEmpty()) {
        general.deleteEntry(selectedInstanceKey);
    } else {
        general.writeEntry(selectedInstanceKey, id.toString());
    }
    return config.sync();
}
