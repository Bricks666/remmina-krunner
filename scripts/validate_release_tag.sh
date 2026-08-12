#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
# SPDX-License-Identifier: 0BSD
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "Usage: $0 TAG CMAKE_FILE" >&2
    exit 64
fi
tag=$1
cmake_file=$2
if [[ ! -f ${cmake_file} || -L ${cmake_file} ]]; then
    echo "Project CMake file does not exist or is unsafe: ${cmake_file}" >&2
    exit 66
fi
if [[ ! ${tag} =~ ^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]]; then
    echo "Release tag must use canonical vMAJOR.MINOR.PATCH form" >&2
    exit 64
fi
mapfile -t versions < <(
    sed -nE -- \
        's/^[[:space:]]*project\(remmina-krunner[[:space:]]+VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+)[[:space:]]+LANGUAGES[[:space:]]+CXX\)[[:space:]]*$/\1/p' \
        "${cmake_file}"
)
if [[ ${#versions[@]} -ne 1 || ! ${versions[0]} =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]]; then
    echo "Expected exactly one canonical remmina-krunner project version" >&2
    exit 65
fi
if [[ ${tag} != "v${versions[0]}" ]]; then
    echo "Release tag ${tag} does not match project version ${versions[0]}" >&2
    exit 1
fi
printf '%s\n' "${versions[0]}"
