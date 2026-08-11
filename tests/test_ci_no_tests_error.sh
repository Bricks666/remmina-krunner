#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
# SPDX-License-Identifier: 0BSD
set -euo pipefail

ci_script=$1

if "${ci_script}" test '^remmina-krunner-guaranteed-missing-test$'; then
    echo "Focused test mode succeeded without matching a test." >&2
    exit 1
fi
