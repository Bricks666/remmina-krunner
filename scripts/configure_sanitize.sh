#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
# SPDX-License-Identifier: 0BSD
set -euo pipefail

if [[ $# -ne 2 || $1 != /* || $2 != /* ]]; then
    echo "Usage: $0 ABSOLUTE_REPOSITORY_ROOT ABSOLUTE_BUILD_DIRECTORY" >&2
    exit 64
fi
repository_root=$(readlink -f -- "$1")
build_directory=$(readlink -m -- "$2")
if [[ ! -d ${repository_root} || ! -f ${repository_root}/CMakeLists.txt || ${build_directory} == / ]]; then
    echo "Sanitizer paths are invalid" >&2
    exit 64
fi
sanitizer_flags="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake -S "${repository_root}" -B "${build_directory}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
    -DCMAKE_CXX_FLAGS="${sanitizer_flags}" \
    -DCMAKE_EXE_LINKER_FLAGS="${sanitizer_flags}" \
    -DCMAKE_SHARED_LINKER_FLAGS="${sanitizer_flags}"
