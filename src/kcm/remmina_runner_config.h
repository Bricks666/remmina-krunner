// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#pragma once

#include <KCModule>

#include <memory>

class InstanceSettingsModel;

namespace Ui {
class RemminaRunnerConfig;
}

class RemminaRunnerConfig final : public KCModule {
    Q_OBJECT

public:
    explicit RemminaRunnerConfig(QWidget *parent, const KPluginMetaData &data);
    // The injected model is non-owning and must outlive the module.
    RemminaRunnerConfig(InstanceSettingsModel &model,
                        QWidget *parent,
                        const KPluginMetaData &data);
    ~RemminaRunnerConfig() override;

    void load() override;
    void save() override;
    void defaults() override;

private:
    struct ProductionDependencies;

    explicit RemminaRunnerConfig(std::unique_ptr<ProductionDependencies> dependencies,
                                 QWidget *parent,
                                 const KPluginMetaData &data);
    void initializeUi();
    void populate();
    void restorePendingSelection();
    void updateStatus();
    void setDirtyState();
    void instanceChanged(int index);

    std::unique_ptr<ProductionDependencies> dependencies_;
    InstanceSettingsModel *model_ = nullptr;
    std::unique_ptr<Ui::RemminaRunnerConfig> ui_;
    bool populating_ = false;
    bool lastScanSucceeded_ = false;
};
