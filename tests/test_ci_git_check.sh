#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
# SPDX-License-Identifier: 0BSD
set -euo pipefail

unset CI CI_DIFF_BASE

git_check=$1
git_executable=$2

test_directory=$(mktemp -d /tmp/remmina-krunner-git-check.XXXXXX)
cleanup() {
    if [[ ${test_directory} =~ ^/tmp/remmina-krunner-git-check\.[[:alnum:]]+$ ]]; then
        rm -rf -- "${test_directory}"
    fi
}
trap cleanup EXIT

repository=${test_directory}/repository
"${git_executable}" init -q "${repository}"
"${git_executable}" -C "${repository}" config user.name "Synthetic Test"
"${git_executable}" -C "${repository}" config user.email "test@example.invalid"
printf '%s\n' clean >"${repository}/fixture.txt"
"${git_executable}" -C "${repository}" add fixture.txt
"${git_executable}" -C "${repository}" commit -q -m initial

"${git_check}" "${repository}"
CI=true CI_DIFF_BASE=ROOT "${git_check}" "${repository}"

printf '%s\n' 'BasedOnStyle: LLVM' >"${repository}/.clang-format"
printf '%s\n' 'int  legacy=0;' >"${repository}/legacy.cpp"
"${git_executable}" -C "${repository}" add .clang-format legacy.cpp
"${git_executable}" -C "${repository}" commit -q -m "legacy formatting baseline"
base_commit=$("${git_executable}" -C "${repository}" rev-parse HEAD)

clean_untracked=${repository}/clean\ untracked.md
printf '%s\n' 'clean untracked text' >"${clean_untracked}"
"${git_check}" "${repository}"
printf 'bad untracked whitespace \n' >"${clean_untracked}"
if "${git_check}" "${repository}" >/dev/null 2>&1; then
    echo "Untracked whitespace errors were not detected" >&2
    exit 1
fi
rm -f -- "${clean_untracked}"

newline_untracked=${repository}/$'line\nbreak.md'
printf '%s\n' 'clean newline-named text' >"${newline_untracked}"
"${git_check}" "${repository}"
printf 'bad newline-named whitespace \n' >"${newline_untracked}"
if "${git_check}" "${repository}" >/dev/null 2>&1; then
    echo "Whitespace errors in a newline-named untracked file were not detected" >&2
    exit 1
fi
rm -f -- "${newline_untracked}"

untracked_binary=${repository}/synthetic\ binary.bin
printf '\0binary payload \n' >"${untracked_binary}"
"${git_check}" "${repository}"
rm -f -- "${untracked_binary}"

untracked_script=${repository}/synthetic\ script.sh
printf '%s\n' '#!/usr/bin/env bash' 'printf "%s\\n" clean' >"${untracked_script}"
"${git_check}" "${repository}"
printf '%s\n' '#!/usr/bin/env bash' 'printf "%s\\n" clean  ' >"${untracked_script}"
if "${git_check}" "${repository}" >/dev/null 2>&1; then
    echo "Untracked shell whitespace errors were not detected" >&2
    exit 1
fi
rm -f -- "${untracked_script}"

untracked_cpp=${repository}/synthetic\ source.cpp
printf '%s\n' 'int current = 0;' >"${untracked_cpp}"
"${git_check}" "${repository}"
printf '%s\n' 'int  current=0;' >"${untracked_cpp}"
if "${git_check}" "${repository}" >/dev/null 2>&1; then
    echo "Untracked C++ formatting errors were not detected" >&2
    exit 1
fi
rm -f -- "${untracked_cpp}"

printf '%s\n' 'int current = 0;' >>"${repository}/legacy.cpp"
"${git_check}" "${repository}"
printf '%s\n' 'int  current=0;' >>"${repository}/legacy.cpp"
if "${git_check}" "${repository}" >/dev/null 2>&1; then
    echo "Changed-line formatting errors were not detected" >&2
    exit 1
fi
"${git_executable}" -C "${repository}" restore legacy.cpp

printf 'working tree whitespace \n' >"${repository}/fixture.txt"
if "${git_check}" "${repository}" >/dev/null 2>&1; then
    echo "Working-tree whitespace errors were not detected" >&2
    exit 1
fi
"${git_executable}" -C "${repository}" restore fixture.txt

printf 'index whitespace \n' >"${repository}/fixture.txt"
"${git_executable}" -C "${repository}" add fixture.txt
if "${git_check}" "${repository}" >/dev/null 2>&1; then
    echo "Index whitespace errors were not detected" >&2
    exit 1
fi
"${git_executable}" -C "${repository}" restore --staged fixture.txt
"${git_executable}" -C "${repository}" restore fixture.txt

printf '%s\n' range-clean >"${repository}/fixture.txt"
"${git_executable}" -C "${repository}" commit -qam "clean range"
CI=true CI_DIFF_BASE="${base_commit}" "${git_check}" "${repository}"

printf '%s\n' 'int  committed=0;' >>"${repository}/legacy.cpp"
"${git_executable}" -C "${repository}" commit -qam "bad committed formatting"
committed_format_diagnostic=${test_directory}/committed-format-diagnostic.txt
if CI=true CI_DIFF_BASE="${base_commit}" \
    "${git_check}" "${repository}" >/dev/null 2>"${committed_format_diagnostic}"; then
    echo "Committed-range C++ formatting errors were not detected" >&2
    exit 1
fi
if ! grep -Fq "C++ formatting differs in committed CI range:" \
    "${committed_format_diagnostic}"; then
    echo "Committed-range C++ formatting failure was misdiagnosed" >&2
    exit 1
fi
if grep -Fq "Unable to check C++ formatting" "${committed_format_diagnostic}"; then
    echo "Formatting differences were reported as a formatter execution failure" >&2
    exit 1
fi
printf '%s\n' 'int  legacy=0;' 'int committed = 0;' >"${repository}/legacy.cpp"
"${git_executable}" -C "${repository}" commit -qam "fix committed formatting"
CI=true CI_DIFF_BASE="${base_commit}" "${git_check}" "${repository}"

if CI=true env -u CI_DIFF_BASE "${git_check}" "${repository}" >/dev/null 2>&1; then
    echo "CI check accepted a missing CI_DIFF_BASE" >&2
    exit 1
fi
if CI=true CI_DIFF_BASE=--help "${git_check}" "${repository}" >/dev/null 2>&1; then
    echo "CI check accepted a revision option as CI_DIFF_BASE" >&2
    exit 1
fi
injection_marker=${test_directory}/injected
if CI=true CI_DIFF_BASE="HEAD;touch ${injection_marker}" \
    "${git_check}" "${repository}" >/dev/null 2>&1; then
    echo "CI check accepted an invalid CI_DIFF_BASE" >&2
    exit 1
fi
if [[ -e ${injection_marker} ]]; then
    echo "CI_DIFF_BASE was evaluated as shell input" >&2
    exit 1
fi

printf 'committed whitespace \n' >"${repository}/fixture.txt"
"${git_executable}" -C "${repository}" commit -qam "bad range"
if CI=true CI_DIFF_BASE="${base_commit}" \
    "${git_check}" "${repository}" >/dev/null 2>&1; then
    echo "Committed-range whitespace errors were not detected" >&2
    exit 1
fi
if CI=true CI_DIFF_BASE=ROOT "${git_check}" "${repository}" >/dev/null 2>&1; then
    echo "ROOT did not inspect the complete committed tree" >&2
    exit 1
fi

linked_checkout=${test_directory}/linked-checkout
mkdir "${linked_checkout}"
printf '%s\n' 'gitdir: /metadata/not-mounted/worktrees/example' \
    >"${linked_checkout}/.git"
diagnostic_file=${test_directory}/diagnostic.txt
"${git_check}" "${linked_checkout}" 2>"${diagnostic_file}"
if ! grep -Fq \
    "Skipping Git repository checks: linked-worktree metadata is unavailable" \
    "${diagnostic_file}"; then
    echo "Unavailable linked-worktree metadata was not diagnosed safely" >&2
    exit 1
fi

non_repository=${test_directory}/non-repository
mkdir "${non_repository}"
if "${git_check}" "${non_repository}" >/dev/null 2>&1; then
    echo "A normal non-repository was incorrectly treated as a linked worktree" >&2
    exit 1
fi

corrupt_checkout=${test_directory}/corrupt-checkout
mkdir -p "${corrupt_checkout}" "${test_directory}/present-metadata"
printf 'gitdir: %s\n' "${test_directory}/present-metadata" >"${corrupt_checkout}/.git"
if "${git_check}" "${corrupt_checkout}" >/dev/null 2>&1; then
    echo "Present but invalid Git metadata was incorrectly skipped" >&2
    exit 1
fi
