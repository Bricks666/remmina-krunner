// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include "core/selection_policy.h"

SelectionDecision validateSelection(const QList<RemminaInstance> &instances, QStringView savedId)
{
    if (!savedId.isEmpty()) {
        for (const RemminaInstance &instance : instances) {
            if (QStringView{instance.id} == savedId) {
                return {instance.id, false};
            }
        }
    }

    constexpr InstanceKind priority[]{
        InstanceKind::Native,
        InstanceKind::Flatpak,
        InstanceKind::Snap,
    };
    for (const InstanceKind kind : priority) {
        for (const RemminaInstance &instance : instances) {
            if (instance.kind == kind) {
                return {instance.id, true};
            }
        }
    }

    return {QString{}, !savedId.isEmpty()};
}
