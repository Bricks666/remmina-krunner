#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
# SPDX-License-Identifier: 0BSD
set -euo pipefail

resolver=$1
git_executable=$2

test_directory=$(mktemp -d /tmp/remmina-krunner-diff-base.XXXXXX)
cleanup() {
    if [[ ${test_directory} =~ ^/tmp/remmina-krunner-diff-base\.[[:alnum:]]+$ ]]; then
        rm -rf -- "${test_directory}"
    fi
}
trap cleanup EXIT

assert_equal() {
    local expected=$1 actual=$2 description=$3
    if [[ ${actual} != "${expected}" ]]; then
        printf '%s: expected %q, got %q\n' \
            "${description}" "${expected}" "${actual}" >&2
        exit 1
    fi
}

repository=${test_directory}/repository
"${git_executable}" init -q "${repository}"
"${git_executable}" -C "${repository}" config user.name "Synthetic Test"
"${git_executable}" -C "${repository}" config user.email "test@example.invalid"
printf '%s\n' first >"${repository}/fixture.txt"
"${git_executable}" -C "${repository}" add fixture.txt
"${git_executable}" -C "${repository}" commit -q -m first
first_commit=$("${git_executable}" -C "${repository}" rev-parse HEAD)
printf '%s\n' second >"${repository}/fixture.txt"
"${git_executable}" -C "${repository}" commit -qam second

assert_equal "${first_commit}" \
    "$("${resolver}" "${repository}" workflow_dispatch '' '' '')" \
    "Manual run uses HEAD parent"
assert_equal "${first_commit}" \
    "$("${resolver}" "${repository}" pull_request "${first_commit}" '' '')" \
    "Pull request uses the verified base SHA"
"${git_executable}" -C "${repository}" branch synthetic-base "${first_commit}"
assert_equal "${first_commit}" \
    "$("${resolver}" "${repository}" pull_request '' synthetic-base '')" \
    "Pull request can resolve a verified base ref"
assert_equal "${first_commit}" \
    "$("${resolver}" "${repository}" push '' '' "${first_commit}")" \
    "Push uses its available before SHA"
assert_equal ROOT \
    "$("${resolver}" "${repository}" push '' '' 0000000000000000000000000000000000000000)" \
    "Initial push uses ROOT"

orphan_tree=$("${git_executable}" -C "${repository}" mktree </dev/null)
orphan_commit=$(printf '%s\n' orphan \
    | "${git_executable}" -C "${repository}" commit-tree "${orphan_tree}")
assert_equal ROOT \
    "$("${resolver}" "${repository}" push '' '' "${orphan_commit}")" \
    "Disconnected push base falls back to ROOT"

for invalid in not-a-sha --help 'HEAD;touch /tmp/nope'; do
    if "${resolver}" "${repository}" push '' '' "${invalid}" >/dev/null 2>&1; then
        echo "Push accepted invalid before SHA: ${invalid}" >&2
        exit 1
    fi
done
if "${resolver}" "${repository}" pull_request '' '--help' '' >/dev/null 2>&1; then
    echo "Pull request accepted a revision option as a base ref" >&2
    exit 1
fi
if "${resolver}" "${repository}" unexpected '' '' '' >/dev/null 2>&1; then
    echo "Resolver accepted an unsupported event" >&2
    exit 1
fi

single=${test_directory}/single
"${git_executable}" init -q "${single}"
"${git_executable}" -C "${single}" config user.name "Synthetic Test"
"${git_executable}" -C "${single}" config user.email "test@example.invalid"
printf '%s\n' only >"${single}/fixture.txt"
"${git_executable}" -C "${single}" add fixture.txt
"${git_executable}" -C "${single}" commit -q -m only
assert_equal ROOT \
    "$("${resolver}" "${single}" workflow_dispatch '' '' '')" \
    "Root commit manual run uses ROOT"

missing_commit=1111111111111111111111111111111111111111
assert_equal ROOT \
    "$("${resolver}" "${repository}" push '' '' "${missing_commit}")" \
    "Unavailable push base falls back safely to ROOT"
