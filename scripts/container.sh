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
image_id_file=""
container_environment=(
    --env REMMINA_KRUNNER_CONTAINER=1
    --env HOME=/tmp
)

cleanup_image_id_file() {
    if [[ -z "${image_id_file}" || (! -e "${image_id_file}" && ! -L "${image_id_file}") ]]; then
        return
    fi
    if [[ "${image_id_file}" =~ ^/tmp/remmina-krunner-image-id\.[[:alnum:]]{6}$ \
        && -f "${image_id_file}" && ! -L "${image_id_file}" ]]; then
        rm -f -- "${image_id_file}"
        return
    fi
    echo "Refusing to remove unexpected image ID file: ${image_id_file}" >&2
}

trap cleanup_image_id_file EXIT

usage() {
    echo "Usage: $0 {build|configure|test [ctest-regex]|check|sanitize|release-build|source-bundle}" >&2
}

if [[ $# -lt 1 || $# -gt 2 ]]; then
    usage
    exit 64
fi

mode=$1
case "${mode}" in
    build|configure|check|sanitize|release-build|source-bundle)
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

if [[ ${CI:-} == true ]]; then
    container_environment+=(--env CI=true)
fi
if [[ -n ${CI_DIFF_BASE:-} ]]; then
    if [[ ${CI_DIFF_BASE} != ROOT && ! ${CI_DIFF_BASE} =~ ^[0-9a-f]{40}$ ]]; then
        echo "CI_DIFF_BASE must be ROOT or a full lowercase commit hash." >&2
        exit 64
    fi
    container_environment+=(--env "CI_DIFF_BASE=${CI_DIFF_BASE}")
fi

image_id_file=$(mktemp /tmp/remmina-krunner-image-id.XXXXXX)
if [[ ! "${image_id_file}" =~ ^/tmp/remmina-krunner-image-id\.[[:alnum:]]{6}$ \
    || ! -f "${image_id_file}" || -L "${image_id_file}" ]]; then
    echo "Refusing to use unexpected image ID file: ${image_id_file}" >&2
    exit 1
fi

podman build \
    --iidfile "${image_id_file}" \
    --tag "${image_name}" \
    --file "${repository_root}/containers/Containerfile" \
    "${repository_root}/containers"

mapfile -t image_id_lines < "${image_id_file}"
image_id_pattern='^(sha256:)?[0-9a-f]{64}$'
if [[ ${#image_id_lines[@]} -ne 1 ]]; then
    echo "Podman returned an invalid image ID." >&2
    exit 1
fi
image_id=${image_id_lines[0]}
if [[ ! "${image_id}" =~ ${image_id_pattern} ]]; then
    echo "Podman returned an invalid image ID." >&2
    exit 1
fi

podman run --rm --userns=keep-id \
    "${container_environment[@]}" \
    --user "${host_user_id}:${host_group_id}" \
    --volume "${repository_root}:/workspace:Z" \
    --workdir /workspace \
    "${image_id}" \
    ./scripts/ci.sh "$@"
