#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
# SPDX-License-Identifier: 0BSD
set -euo pipefail

if [[ "${REMMINA_KRUNNER_CONTAINER:-}" != 1 ]]; then
    echo "scripts/ci.sh must run inside the Remmina KRunner development container." >&2
    exit 1
fi

script_path=$(readlink -f -- "${BASH_SOURCE[0]}")
script_directory=$(dirname -- "${script_path}")
repository_root=$(cd -- "${script_directory}/.." && pwd -P)
parallel_jobs=${CMAKE_BUILD_PARALLEL_LEVEL:-2}

usage() {
    echo "Usage: $0 {build|configure|test [ctest-regex]|check|sanitize|release-build|source-bundle|release-package TAG OUTPUT_DIR}" >&2
}

configure_build() {
    local build_directory=$1
    local build_type=$2
    local build_testing=$3
    shift 3

    cmake \
        -S "${repository_root}" \
        -B "${build_directory}" \
        -G Ninja \
        -DCMAKE_BUILD_TYPE="${build_type}" \
        -DBUILD_TESTING="${build_testing}" \
        "$@"
}

build_debug() {
    local build_directory="${repository_root}/build-dev"
    configure_build "${build_directory}" Debug ON
    cmake --build "${build_directory}" --parallel "${parallel_jobs}"
}

run_tests() {
    local test_regex=${1:-}
    local build_directory="${repository_root}/build-test"
    configure_build "${build_directory}" Debug ON
    cmake --build "${build_directory}" --parallel "${parallel_jobs}"

    local -a ctest_arguments=(
        --test-dir "${build_directory}"
        --no-tests=error
        --output-on-failure
    )
    if [[ -n "${test_regex}" ]]; then
        ctest_arguments+=(--tests-regex "${test_regex}")
    fi
    QT_QPA_PLATFORM=offscreen ctest "${ctest_arguments[@]}"
}

run_check() {
    local build_directory="${repository_root}/build-ci"
    "${script_directory}/check_repository_diff.sh" "${repository_root}"
    configure_build "${build_directory}" Debug ON -DCMAKE_INSTALL_PREFIX=/usr
    cmake --build "${build_directory}" --parallel "${parallel_jobs}"
    QT_QPA_PLATFORM=offscreen \
        ctest --test-dir "${build_directory}" --no-tests=error --output-on-failure

    local staging_directory
    staging_directory=$(mktemp -d /tmp/remmina-krunner-install-stage.XXXXXX)
    if [[ ! ${staging_directory} =~ ^/tmp/remmina-krunner-install-stage\.[[:alnum:]]{6}$ ||
          ! -d ${staging_directory} || -L ${staging_directory} ]]; then
        echo "Refusing unexpected install staging directory: ${staging_directory}" >&2
        exit 1
    fi
    cleanup_staging_directory() {
        if [[ -z ${staging_directory} ]]; then
            return
        fi
        if [[ ${staging_directory} =~ ^/tmp/remmina-krunner-install-stage\.[[:alnum:]]{6}$ &&
              -d ${staging_directory} && ! -L ${staging_directory} ]]; then
            rm -rf -- "${staging_directory}"
            staging_directory=
        else
            echo "Refusing unsafe install staging cleanup: ${staging_directory}" >&2
            return 1
        fi
    }
    trap cleanup_staging_directory EXIT
    trap 'exit 129' HUP
    trap 'exit 130' INT
    trap 'exit 143' TERM
    DESTDIR="${staging_directory}" cmake --install "${build_directory}"
    cmake \
        -DSTAGE_ROOT="${staging_directory}" \
        -DINSTALL_PREFIX_RELATIVE=usr \
        -DLAYOUT_FILE="${build_directory}/RemminaKRunnerInstallLayout.cmake" \
        -P "${repository_root}/cmake/ValidateInstallInventory.cmake"
    cleanup_staging_directory
    trap - EXIT HUP INT TERM
}

run_sanitize() {
    local build_directory="${repository_root}/build-sanitize"
    "${script_directory}/configure_sanitize.sh" "${repository_root}" "${build_directory}"
    cmake --build "${build_directory}" --parallel "${parallel_jobs}"
    ASAN_OPTIONS=halt_on_error=1:abort_on_error=1:detect_leaks=1 \
        UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
        QT_QPA_PLATFORM=offscreen \
        ctest --test-dir "${build_directory}" --no-tests=error --output-on-failure
}

release_package() {
    local release_tag=$1 output_directory=$2
    local build_directory="${repository_root}/build-release"
    local temporary_output archive checksum archive_path checksum_path
    local staged_archive= staged_checksum= archive_identity= checksum_identity=
    local publication_rollback=0 current_identity
    [[ ${output_directory} == "${repository_root}"/* &&
       ${output_directory} != *$'\n'* && ${output_directory} != *$'\r'* ]] || {
        echo "Release output must be a bounded directory in the workspace." >&2
        exit 64
    }
    if [[ ! -d ${output_directory} || -L ${output_directory} ||
          -n $(find "${output_directory}" -mindepth 1 -maxdepth 1 -print -quit) ]]; then
        echo "Release output must be an empty real directory." >&2
        exit 64
    fi
    [[ ${SOURCE_DATE_EPOCH:-} =~ ^[0-9]+$ ]] || {
        echo "SOURCE_DATE_EPOCH must be set for release packaging." >&2
        exit 64
    }
    configure_build "${build_directory}" Release OFF
    cmake --build "${build_directory}" --parallel "${parallel_jobs}"
    temporary_output=$(mktemp -d /tmp/remmina-release-output.XXXXXX)
    cleanup_release_output() {
        local path expected
        if [[ ${publication_rollback} == 1 ]]; then
            for path in "${archive_path:-}" "${checksum_path:-}"; do
                [[ ${path} == "${archive_path:-}" ]] && expected=${archive_identity} || expected=${checksum_identity}
                if [[ -n ${expected} && -f ${path} && ! -L ${path} ]]; then
                    current_identity=$(stat -c '%d:%i' -- "${path}" 2>/dev/null || true)
                    [[ ${current_identity} != "${expected}" ]] || rm -f -- "${path}"
                fi
            done
        fi
        for path in "${staged_archive}" "${staged_checksum}"; do
            [[ ${path} == "${staged_archive}" ]] && expected=${archive_identity} || expected=${checksum_identity}
            if [[ -n ${path} && -n ${expected} && -f ${path} && ! -L ${path} ]]; then
                current_identity=$(stat -c '%d:%i' -- "${path}" 2>/dev/null || true)
                [[ ${current_identity} != "${expected}" ]] || rm -f -- "${path}"
            fi
        done
        if [[ -n ${temporary_output} && ${temporary_output} == /tmp/remmina-release-output.* &&
              -d ${temporary_output} && ! -L ${temporary_output} ]]; then
            rm -rf -- "${temporary_output}"
        fi
    }
    trap cleanup_release_output EXIT
    trap 'exit 129' HUP
    trap 'exit 130' INT
    trap 'exit 143' TERM
    "${script_directory}/package_release.sh" "${repository_root}" "${build_directory}" \
        "${temporary_output}" "${release_tag}"
    archive=remmina-krunner-${release_tag}-linux-x86_64.tar.gz
    checksum=${archive}.sha256
    archive_path=${output_directory}/${archive}
    checksum_path=${output_directory}/${checksum}
    staged_archive=$(mktemp "${output_directory}/.${archive}.stage.XXXXXX")
    archive_identity=$(stat -c '%d:%i' -- "${staged_archive}")
    staged_checksum=$(mktemp "${output_directory}/.${checksum}.stage.XXXXXX")
    checksum_identity=$(stat -c '%d:%i' -- "${staged_checksum}")
    cp --no-preserve=all -- "${temporary_output}/${archive}" "${staged_archive}"
    cp --no-preserve=all -- "${temporary_output}/${checksum}" "${staged_checksum}"
    chmod 0644 -- "${staged_archive}" "${staged_checksum}"
    [[ $(stat -c '%d:%i' -- "${staged_archive}") == "${archive_identity}" &&
       $(stat -c '%d:%i' -- "${staged_checksum}") == "${checksum_identity}" ]] || {
        echo "Release staging files changed while being populated." >&2
        exit 73
    }
    [[ ! -e ${archive_path} && ! -L ${archive_path} &&
       ! -e ${checksum_path} && ! -L ${checksum_path} ]] || {
        echo "Release output collision occurred during publication." >&2
        exit 73
    }
    publication_rollback=1
    mv -n -T -- "${staged_archive}" "${archive_path}"
    [[ ! -e ${staged_archive} && -f ${archive_path} && ! -L ${archive_path} &&
       $(stat -c '%d:%i' -- "${archive_path}") == "${archive_identity}" ]] || {
        echo "Unable to publish release archive without replacement." >&2
        exit 73
    }
    staged_archive=
    mv -n -T -- "${staged_checksum}" "${checksum_path}"
    [[ ! -e ${staged_checksum} && -f ${checksum_path} && ! -L ${checksum_path} &&
       $(stat -c '%d:%i' -- "${checksum_path}") == "${checksum_identity}" ]] || {
        echo "Unable to publish release checksum without replacement." >&2
        exit 73
    }
    staged_checksum=
    (cd -- "${output_directory}"; sha256sum --check "${checksum}" >/dev/null)
    [[ $(stat -c '%d:%i' -- "${archive_path}") == "${archive_identity}" &&
       $(stat -c '%d:%i' -- "${checksum_path}") == "${checksum_identity}" ]] || {
        echo "Published release assets changed during verification." >&2
        exit 73
    }
    publication_rollback=0
    cleanup_release_output
    temporary_output=
    trap - EXIT HUP INT TERM
}

release_build() {
    local build_directory="${repository_root}/build-release"
    configure_build "${build_directory}" Release OFF
    cmake --build "${build_directory}" --parallel "${parallel_jobs}"
}

source_bundle() {
    local build_directory="${repository_root}/build-source-bundle-build"
    local bundle_root="${repository_root}/build-source-bundle"
    local bundle_prefix=remmina-krunner
    local staging_root

    configure_build "${build_directory}" Release OFF \
        -DCMAKE_INSTALL_PREFIX="/${bundle_prefix}"
    cmake --build "${build_directory}" --parallel "${parallel_jobs}"

    staging_root=$(mktemp -d \
        "${repository_root}/build-source-bundle-stage.XXXXXX")
    if [[ ! ${staging_root} =~ ^${repository_root}/build-source-bundle-stage\.[[:alnum:]]{6}$ \
        || ! -d ${staging_root} || -L ${staging_root} ]]; then
        echo "Refusing unexpected source-bundle staging directory: ${staging_root}" >&2
        exit 1
    fi
    cleanup_source_bundle_stage() {
        if [[ -z ${staging_root} ]]; then
            return
        fi
        if [[ ${staging_root} =~ ^${repository_root}/build-source-bundle-stage\.[[:alnum:]]{6}$ \
            && -d ${staging_root} && ! -L ${staging_root} ]]; then
            cmake -E remove_directory "${staging_root}"
            staging_root=
        else
            echo "Refusing unsafe source-bundle staging cleanup: ${staging_root}" >&2
            return 1
        fi
    }
    trap cleanup_source_bundle_stage EXIT
    trap 'exit 129' HUP
    trap 'exit 130' INT
    trap 'exit 143' TERM

    DESTDIR="${staging_root}" cmake --install "${build_directory}"
    cmake \
        -DSTAGE_ROOT="${staging_root}" \
        -DINSTALL_PREFIX_RELATIVE="${bundle_prefix}" \
        -DLAYOUT_FILE="${build_directory}/RemminaKRunnerInstallLayout.cmake" \
        -P "${repository_root}/cmake/ValidateInstallInventory.cmake"

    if [[ -e ${bundle_root} || -L ${bundle_root} ]]; then
        if [[ ! -d ${bundle_root} || -L ${bundle_root} \
            || ${bundle_root} != "${repository_root}/build-source-bundle" ]]; then
            echo "Refusing unsafe existing source-bundle path: ${bundle_root}" >&2
            exit 1
        fi
        cmake -E remove_directory "${bundle_root}"
    fi
    mv -- "${staging_root}" "${bundle_root}"
    staging_root=
    trap - EXIT HUP INT TERM
    printf 'Source installation bundle: %s\n' \
        "${bundle_root}/${bundle_prefix}"
}

if [[ $# -lt 1 || $# -gt 3 ]]; then
    usage
    exit 64
fi

case "$1" in
    build)
        [[ $# -eq 1 ]] || { usage; exit 64; }
        build_debug
        ;;
    configure)
        [[ $# -eq 1 ]] || { usage; exit 64; }
        configure_build "${repository_root}/build-dev" Debug ON
        ;;
    test)
        run_tests "${2:-}"
        ;;
    check)
        [[ $# -eq 1 ]] || { usage; exit 64; }
        run_check
        ;;
    sanitize)
        [[ $# -eq 1 ]] || { usage; exit 64; }
        run_sanitize
        ;;
    release-build)
        [[ $# -eq 1 ]] || { usage; exit 64; }
        release_build
        ;;
    source-bundle)
        [[ $# -eq 1 ]] || { usage; exit 64; }
        source_bundle
        ;;
    release-package)
        [[ $# -eq 3 ]] || { usage; exit 64; }
        release_package "$2" "$3"
        ;;
    *)
        usage
        exit 64
        ;;
esac
