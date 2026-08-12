#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
# SPDX-License-Identifier: 0BSD
set -euo pipefail

if [[ $# -lt 2 ]]; then
    echo "Usage: $0 ACTIONLINT WORKFLOW..." >&2
    exit 64
fi

actionlint_executable=$1
shift

for workflow in "$@"; do
    "${actionlint_executable}" "${workflow}"
done

test_directory=$(mktemp -d /tmp/remmina-krunner-actionlint.XXXXXX)
cleanup() {
    if [[ ${test_directory} =~ ^/tmp/remmina-krunner-actionlint\.[[:alnum:]]+$ ]]; then
        rm -rf -- "${test_directory}"
    fi
}
trap cleanup EXIT

malformed=${test_directory}/malformed.yml
printf '%s\n' 'name: Broken' 'on: [push' 'jobs: {}' >"${malformed}"
if "${actionlint_executable}" "${malformed}" >/dev/null 2>&1; then
    echo "actionlint accepted malformed workflow YAML" >&2
    exit 1
fi
