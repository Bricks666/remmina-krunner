// SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
// SPDX-License-Identifier: 0BSD

#include "dbus/dbus_types.h"

#include <KRunner/Action>
#include <KRunner/QueryMatch>

#include <type_traits>

static_assert(std::is_same_v<KRunner::Actions::value_type, KRunner::Action>);
static_assert(!std::is_same_v<RemminaKRunner::RunnerActions, KRunner::Actions>);

int main() { return 0; }
