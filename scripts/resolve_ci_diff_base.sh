#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
# SPDX-License-Identifier: 0BSD
set -euo pipefail

if [[ $# -ne 5 ]]; then
    echo \
        "Usage: $0 REPOSITORY_ROOT EVENT_NAME PR_BASE_SHA PR_BASE_REF PUSH_BEFORE_SHA" \
        >&2
    exit 64
fi

repository_root=$(readlink -f -- "$1")
event_name=$2
pr_base_sha=$3
pr_base_ref=$4
push_before_sha=$5

repository_git() {
    git -c "safe.directory=${repository_root}" -C "${repository_root}" "$@"
}

if ! repository_git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "Unable to inspect repository Git metadata" >&2
    exit 64
fi

validate_hash() {
    local candidate=$1
    if [[ ! ${candidate} =~ ^[0-9a-f]{40}$ ]]; then
        echo "Event diff base is not a full lowercase commit hash" >&2
        return 1
    fi
}

commit_exists() {
    repository_git cat-file -e "$1^{commit}" 2>/dev/null
}

has_merge_base() {
    repository_git merge-base "$1" HEAD >/dev/null 2>&1
}

resolve_pull_request_ref() {
    local base_ref=$1 qualified_ref candidate
    if [[ -z ${base_ref} || ${base_ref} == -* ]] \
        || ! repository_git check-ref-format --branch "${base_ref}" >/dev/null 2>&1; then
        return 1
    fi
    for qualified_ref in \
        "refs/remotes/origin/${base_ref}" \
        "refs/heads/${base_ref}"; do
        candidate=$(repository_git rev-parse --verify --quiet \
            "${qualified_ref}^{commit}") || continue
        if has_merge_base "${candidate}"; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    done
    return 1
}

case ${event_name} in
    pull_request)
        if [[ -n ${pr_base_sha} ]]; then
            validate_hash "${pr_base_sha}" || exit 64
            if commit_exists "${pr_base_sha}" && has_merge_base "${pr_base_sha}"; then
                printf '%s\n' "${pr_base_sha}"
                exit 0
            fi
        fi
        if ! resolve_pull_request_ref "${pr_base_ref}"; then
            echo "Pull-request base commit and ref are unavailable" >&2
            exit 64
        fi
        ;;
    push)
        if [[ ${push_before_sha} == 0000000000000000000000000000000000000000 ]]; then
            printf '%s\n' ROOT
            exit 0
        fi
        validate_hash "${push_before_sha}" || exit 64
        if commit_exists "${push_before_sha}"; then
            if has_merge_base "${push_before_sha}"; then
                printf '%s\n' "${push_before_sha}"
            else
                printf '%s\n' ROOT
            fi
            exit 0
        fi
        if repository_git fetch --no-tags --depth=1 origin "${push_before_sha}" \
            >/dev/null 2>&1 \
            && commit_exists "${push_before_sha}" \
            && has_merge_base "${push_before_sha}"; then
            printf '%s\n' "${push_before_sha}"
        else
            printf '%s\n' ROOT
        fi
        ;;
    workflow_dispatch)
        if candidate=$(repository_git rev-parse --verify --quiet HEAD^); then
            printf '%s\n' "${candidate}"
        else
            printf '%s\n' ROOT
        fi
        ;;
    *)
        echo "Unsupported event: ${event_name}" >&2
        exit 64
        ;;
esac
