#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
# SPDX-License-Identifier: 0BSD
set -euo pipefail

cmake_command=$1
bash_command=$2
timeout_command=$3
dbus_run_session_command=$4
gdbus_command=$5
source_root=$6
build_root=$7
built_executable=$8
dbus_daemon_command=$9

test_root="${build_root}/activation test"
"${cmake_command}" -E rm -rf "${test_root}"
install_prefix='install $dollar `backtick` "quote" \backslash'
case_root="${test_root}/${install_prefix}"
service_data="${test_root}/service data"
test_home="${test_root}/home"
fake_bin="${test_root}/fake bin"
service_file="${service_data}/dbus-1/services/org.remminakrunner.KRunner.service"
mkdir -p -- "${case_root}/bin" "${service_data}/dbus-1/services" \
    "${test_home}/cache" "${test_home}/config" "${fake_bin}"
cp -- "${built_executable}" "${case_root}/bin/remmina-krunner"
"${cmake_command}" -E create_symlink "${dbus_daemon_command}" "${fake_bin}/dbus-daemon"

# The service scans this isolated executable and cannot see host Remmina/Flatpak.
printf '#!/bin/sh\nexit 0\n' >"${fake_bin}/remmina"
chmod +x "${fake_bin}/remmina"
export ACTIVATION_EXECUTABLE_PATH="${case_root}/bin/remmina-krunner"
"${cmake_command}" \
    -DESCAPE_HELPER="${source_root}/cmake/EscapeDBusExec.cmake" \
    -DSERVICE_TEMPLATE="${source_root}/data/org.remminakrunner.KRunner.service.in" \
    -DOUTPUT_FILE="${service_file}" \
    -P "${source_root}/tests/configure_activation_service.cmake"

export XDG_DATA_DIRS="${service_data}"
export HOME="${test_home}"
export XDG_CONFIG_HOME="${test_home}/config"
export XDG_CACHE_HOME="${test_home}/cache"
export PATH="${fake_bin}"
export TEST_GDBUS="${gdbus_command}"
export TEST_TIMEOUT="${timeout_command}"
export TEST_EXPECTED_EXECUTABLE="${case_root}/bin/remmina-krunner"

"${timeout_command}" 15 "${dbus_run_session_command}" -- \
    "${bash_command}" -euo pipefail -c '
runner_pid=""
lookup_pid() {
    local reply
    reply=$("${TEST_GDBUS}" call --session \
        --dest org.freedesktop.DBus \
        --object-path /org/freedesktop/DBus \
        --method org.freedesktop.DBus.GetConnectionUnixProcessID \
        org.remminakrunner.KRunner 2>/dev/null) || return 1
    runner_pid=${reply##*uint32 }
    runner_pid=${runner_pid%%,*}
    runner_pid=${runner_pid%%)*}
    [[ ${runner_pid} =~ ^[0-9]+$ ]]
}
cleanup() {
    if [[ -z ${runner_pid} ]]; then lookup_pid || true; fi
    if [[ -n ${runner_pid} ]]; then kill -TERM "${runner_pid}" 2>/dev/null || true; fi
}
trap cleanup EXIT

introspection=$("${TEST_TIMEOUT}" 8 "${TEST_GDBUS}" introspect --session \
    --dest org.remminakrunner.KRunner --object-path /runner)
lookup_pid
[[ /proc/${runner_pid}/exe -ef "${TEST_EXPECTED_EXECUTABLE}" ]]
[[ ${introspection} == *"interface org.kde.krunner1"* ]]

config=$("${TEST_TIMEOUT}" 8 "${TEST_GDBUS}" call --session \
    --dest org.remminakrunner.KRunner --object-path /runner \
    --method org.kde.krunner1.Config)
[[ ${config} == *"(?i)^rem"* ]]

empty_match=$("${TEST_TIMEOUT}" 8 "${TEST_GDBUS}" call --session \
    --dest org.remminakrunner.KRunner --object-path /runner \
    --method org.kde.krunner1.Match rem)
[[ ${empty_match} == *"@a(sssida{sv}) []"* ]]

create_match=$("${TEST_TIMEOUT}" 8 "${TEST_GDBUS}" call --session \
    --dest org.remminakrunner.KRunner --object-path /runner \
    --method org.kde.krunner1.Match "rem new")
[[ ${create_match} == *"action:new"* ]]

"${TEST_TIMEOUT}" 8 "${TEST_GDBUS}" call --session \
    --dest org.remminakrunner.KRunner --object-path /runner \
    --method org.kde.krunner1.Teardown >/dev/null
kill -0 "${runner_pid}"
'
