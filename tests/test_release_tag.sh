#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
# SPDX-License-Identifier: 0BSD
set -euo pipefail

validator=$1
project_cmake=$2

test_directory=$(mktemp -d /tmp/remmina-krunner-release-tag.XXXXXX)
cleanup() {
    if [[ "${test_directory}" =~ ^/tmp/remmina-krunner-release-tag\.[[:alnum:]]+$ ]]; then
        rm -rf -- "${test_directory}"
    fi
}
trap cleanup EXIT

assert_equal() {
    local expected=$1
    local actual=$2
    local description=$3
    if [[ "${actual}" != "${expected}" ]]; then
        printf '%s: expected %q, got %q\n' "${description}" "${expected}" "${actual}" >&2
        exit 1
    fi
}

run_validator() {
    local expected_status=$1
    local expected_stdout=$2
    local tag=$3
    local cmake_file=$4
    local description=$5
    local stdout_file="${test_directory}/stdout"
    local stderr_file="${test_directory}/stderr"
    local status

    set +e
    "${validator}" "${tag}" "${cmake_file}" >"${stdout_file}" 2>"${stderr_file}"
    status=$?
    set -e

    assert_equal "${expected_status}" "${status}" "${description} exit status"
    assert_equal "${expected_stdout}" "$(<"${stdout_file}")" "${description} stdout"
    if [[ "${expected_status}" -ne 0 && ! -s "${stderr_file}" ]]; then
        printf '%s: expected diagnostic on stderr\n' "${description}" >&2
        exit 1
    fi
}

copied_cmake="${test_directory}/copied-CMakeLists.txt"
cp -- "${project_cmake}" "${copied_cmake}"
run_validator 0 0.1.0 v0.1.0 "${copied_cmake}" "matching canonical tag"

canonical_cmake="${test_directory}/canonical-CMakeLists.txt"
printf '%s\n' \
    'project(remmina-krunner VERSION 0.1.0 LANGUAGES CXX)' \
    >"${canonical_cmake}"

dash_prefixed_cmake="${test_directory}/-project-CMakeLists.txt"
cp -- "${canonical_cmake}" "${dash_prefixed_cmake}"
(
    cd -- "${test_directory}"
    run_validator 0 0.1.0 v0.1.0 -project-CMakeLists.txt \
        "dash-prefixed relative CMake file"
)

run_validator 64 '' 0.1.0 "${canonical_cmake}" "tag without v prefix"
run_validator 64 '' v01.1.0 "${canonical_cmake}" "tag with leading zero"
run_validator 64 '' v0.1 "${canonical_cmake}" "tag without patch version"
run_validator 64 '' v0.1.0-rc.1 "${canonical_cmake}" "prerelease tag"
run_validator 1 '' v0.2.0 "${canonical_cmake}" "mismatched project version"
run_validator 66 '' v0.1.0 "${test_directory}/missing-CMakeLists.txt" "missing CMake file"

zero_versions_cmake="${test_directory}/zero-versions-CMakeLists.txt"
printf '%s\n' \
    'project(unrelated VERSION 0.1.0 LANGUAGES CXX)' \
    >"${zero_versions_cmake}"
run_validator 65 '' v0.1.0 "${zero_versions_cmake}" "zero project version matches"

two_versions_cmake="${test_directory}/two-versions-CMakeLists.txt"
printf '%s\n' \
    'project(remmina-krunner VERSION 0.1.0 LANGUAGES CXX)' \
    'project(remmina-krunner VERSION 0.1.0 LANGUAGES CXX)' \
    >"${two_versions_cmake}"
run_validator 65 '' v0.1.0 "${two_versions_cmake}" "two project version matches"
