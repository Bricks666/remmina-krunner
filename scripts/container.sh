#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
# SPDX-License-Identifier: 0BSD
set -euo pipefail

script_path=$(readlink -f -- "${BASH_SOURCE[0]}")
script_directory=$(dirname -- "${script_path}")
repository_root=$(cd -- "${script_directory}/.." && pwd -P)
image_name=localhost/remmina-krunner-dev:fedora44
host_user_id=$(id -u)
host_group_id=$(id -g)

usage() {
    echo "Usage: $0 {build|configure|test [ctest-regex]|check|sanitize|release-build}" >&2
}

if [[ $# -lt 1 || $# -gt 2 ]]; then
    usage
    exit 64
fi

mode=$1
case "${mode}" in
    build|configure|check|sanitize|release-build)
        if [[ $# -ne 1 ]]; then
            usage
            exit 64
        fi
        ;;
    test)
        ;;
    *)
        usage
        exit 64
        ;;
esac

podman build \
    --tag "${image_name}" \
    --file "${repository_root}/containers/Containerfile" \
    "${repository_root}"

podman run --rm --userns=keep-id \
    --env REMMINA_KRUNNER_CONTAINER=1 \
    --env HOME=/tmp \
    --user "${host_user_id}:${host_group_id}" \
    --volume "${repository_root}:/workspace:Z" \
    --workdir /workspace \
    "${image_name}" \
    ./scripts/ci.sh "$@"
