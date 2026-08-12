// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include "kcm/remmina_runner_config.h"

#include <KPluginFactory>

namespace {

QObject *createRemminaRunnerConfig(QWidget *,
                                   QObject *parent,
                                   const KPluginMetaData &data,
                                   const QVariantList &)
{
    return new RemminaRunnerConfig(qobject_cast<QWidget *>(parent), data);
}

} // namespace

K_PLUGIN_FACTORY(RemminaRunnerConfigFactory,
                 registerPlugin<RemminaRunnerConfig>(createRemminaRunnerConfig);)

#include "remmina_runner_config_plugin.moc"
