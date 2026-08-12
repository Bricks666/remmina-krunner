#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
# SPDX-License-Identifier: 0BSD
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 REPOSITORY_ROOT" >&2
    exit 64
fi

repository_root=$(readlink -f -- "$1")
if [[ ! -d ${repository_root} ]]; then
    echo "Repository root is not a directory" >&2
    exit 64
fi

repository_git() {
    git -c "safe.directory=${repository_root}" -C "${repository_root}" "$@"
}

if [[ ${CI:-} == true && -z ${CI_DIFF_BASE:-} ]]; then
    echo "CI=true requires CI_DIFF_BASE" >&2
    exit 64
fi

if ! repository_git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    if [[ -f ${repository_root}/.git && ! -L ${repository_root}/.git ]]; then
        git_pointer=
        IFS= read -r git_pointer <"${repository_root}/.git" || true
        if [[ ${git_pointer} == 'gitdir: '* ]]; then
            git_directory=${git_pointer#gitdir: }
            if [[ ${git_directory} != /* ]]; then
                git_directory=${repository_root}/${git_directory}
            fi
            if [[ ! -e ${git_directory} && ! -L ${git_directory} ]]; then
                echo \
                    "Skipping Git repository checks: linked-worktree metadata is unavailable in this mount." \
                    >&2
                exit 0
            fi
        fi
    fi
    echo "Unable to inspect repository Git metadata" >&2
    exit 1
fi

repository_git diff --check --
repository_git diff --cached --check --

mapfile -d '' -t untracked_files < <(
    repository_git ls-files --others --exclude-standard -z --
)
for untracked_file in "${untracked_files[@]}"; do
    untracked_path=${repository_root}/${untracked_file}
    if [[ ! -f ${untracked_path} || -L ${untracked_path} ]]; then
        continue
    fi
    untracked_whitespace=
    set +e
    untracked_whitespace=$(repository_git diff --no-index --check -- \
        /dev/null "${untracked_path}" 2>&1)
    untracked_status=$?
    set -e
    case ${untracked_status} in
        0|1)
            ;;
        3)
            echo "Untracked file has whitespace errors: ${untracked_file}" >&2
            printf '%s\n' "${untracked_whitespace}" >&2
            exit 1
            ;;
        *)
            echo "Unable to inspect untracked file: ${untracked_file}" >&2
            printf '%s\n' "${untracked_whitespace}" >&2
            exit 1
            ;;
    esac
done

check_format_diff() {
    local description=$1
    shift
    local formatted_diff format_status
    set +e
    formatted_diff=$(
        repository_git diff --no-ext-diff --no-color --unified=0 "$@" -- \
            '*.cc' '*.cpp' '*.h' '*.hpp' \
            | (cd -- "${repository_root}" && clang-format-diff -p1 -style=file)
    )
    format_status=$?
    set -e
    if [[ ${format_status} -gt 1 || (${format_status} -eq 1 && -z ${formatted_diff}) ]]; then
        echo "Unable to check C++ formatting for ${description}" >&2
        exit 1
    fi
    if [[ -n ${formatted_diff} ]]; then
        echo "C++ formatting differs in ${description}:" >&2
        printf '%s\n' "${formatted_diff}" >&2
        exit 1
    fi
}

check_format_diff "working tree"
check_format_diff "index" --cached

if [[ -n ${CI_DIFF_BASE:-} ]]; then
    if [[ ${CI_DIFF_BASE} == ROOT ]]; then
        empty_tree=$(repository_git hash-object -t tree /dev/null)
        repository_git diff --check "${empty_tree}" HEAD --
        check_format_diff "committed ROOT range" "${empty_tree}" HEAD
    else
        if [[ ! ${CI_DIFF_BASE} =~ ^[0-9a-f]{40}$ ]] \
            || ! repository_git cat-file -e "${CI_DIFF_BASE}^{commit}" 2>/dev/null; then
            echo "CI_DIFF_BASE is not an available full commit hash" >&2
            exit 64
        fi
        if ! repository_git merge-base "${CI_DIFF_BASE}" HEAD >/dev/null; then
            echo "CI_DIFF_BASE has no merge base with HEAD" >&2
            exit 64
        fi
        repository_git diff --check "${CI_DIFF_BASE}...HEAD" --
        check_format_diff "committed CI range" "${CI_DIFF_BASE}...HEAD"
    fi
fi

untracked_format_files=()
for untracked_file in "${untracked_files[@]}"; do
    case ${untracked_file} in
        *.cc|*.cpp|*.h|*.hpp)
            untracked_path=${repository_root}/${untracked_file}
            if [[ -f ${untracked_path} && ! -L ${untracked_path} ]]; then
                untracked_format_files+=("${untracked_file}")
            fi
            ;;
    esac
done
if [[ ${#untracked_format_files[@]} -gt 0 ]]; then
    format_paths=()
    for format_file in "${untracked_format_files[@]}"; do
        format_paths+=("${repository_root}/${format_file}")
    done
    clang-format --dry-run --Werror --style=file -- "${format_paths[@]}"
fi
