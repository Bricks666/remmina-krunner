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
    echo "Usage: $0 {build|configure|test [ctest-regex]|check|sanitize|release-build}" >&2
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
    local sanitizer_flags="-fsanitize=address,undefined -fno-omit-frame-pointer"
    configure_build "${build_directory}" Debug ON \
        -DCMAKE_CXX_FLAGS="${sanitizer_flags}" \
        -DCMAKE_EXE_LINKER_FLAGS="${sanitizer_flags}" \
        -DCMAKE_SHARED_LINKER_FLAGS="${sanitizer_flags}"
    cmake --build "${build_directory}" --parallel "${parallel_jobs}"
    ASAN_OPTIONS=halt_on_error=1:abort_on_error=1:detect_leaks=1 \
        UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
        QT_QPA_PLATFORM=offscreen \
        ctest --test-dir "${build_directory}" --no-tests=error --output-on-failure
}

release_build() {
    local build_directory="${repository_root}/build-release"
    configure_build "${build_directory}" Release OFF
    cmake --build "${build_directory}" --parallel "${parallel_jobs}"
}

if [[ $# -lt 1 || $# -gt 2 ]]; then
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
    *)
        usage
        exit 64
        ;;
esac
