#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
# SPDX-License-Identifier: 0BSD
set -euo pipefail

die() { echo "$1" >&2; exit "${2:-1}"; }
validate_path() {
    local name=$1 value=$2
    [[ -n ${value} && ${value} == /* && ${value} != *$'\n'* && ${value} != *$'\r'* ]] ||
        die "${name} must be a nonempty absolute path without newlines" 64
    [[ ${value} != *'/../'* && ${value} != */.. &&
       ${value} != *'/./'* && ${value} != */. && ${value} != *//* ]] ||
        die "${name} must be a normalized absolute path" 64
    [[ ${value} != / && ${value} != /usr && ${value} != /usr/* &&
       ${value} != /etc && ${value} != /etc/* && ${value} != /var && ${value} != /var/* ]] ||
        die "${name} must identify a user-local path" 64
}
for command_name in flock id mkdir readlink rm sleep stat uname kbuildsycoca6; do
    command -v "${command_name}" >/dev/null 2>&1 || die "Required command is unavailable: ${command_name}" 69
done
[[ $(uname -s) == Linux ]] || die "This release bundle supports Linux only" 65
script_path=$(readlink -f -- "${BASH_SOURCE[0]}") || die "Unable to resolve uninstaller path" 66
home_directory=${HOME:-}
validate_path HOME "${home_directory}"
install_prefix=${REMMINA_KRUNNER_INSTALL_PREFIX:-${home_directory}/.local}
validate_path REMMINA_KRUNNER_INSTALL_PREFIX "${install_prefix}"
data_home=${XDG_DATA_HOME:-${home_directory}/.local/share}
validate_path XDG_DATA_HOME "${data_home}"

binary_path=${install_prefix}/bin/remmina-krunner
plugin_relative=@REMMINA_KRUNNER_PLUGIN_RELATIVE@
plugin_path=${install_prefix}/${plugin_relative}
desktop_path=${data_home}/krunner/dbusplugins/org.remminakrunner.KRunner.desktop
service_path=${data_home}/dbus-1/services/org.remminakrunner.KRunner.service
owned_paths=("${binary_path}" "${plugin_path}" "${desktop_path}" "${service_path}")

acquire_install_lock() {
    local runtime_root=${XDG_RUNTIME_DIR:-/tmp}
    [[ ${runtime_root} == /* && ${runtime_root} != *$'\n'* && ${runtime_root} != *$'\r'* &&
       ${runtime_root} != *'/../'* && ${runtime_root} != */.. &&
       ${runtime_root} != *'/./'* && ${runtime_root} != */. && ${runtime_root} != *//* ]] ||
        die "XDG_RUNTIME_DIR must be a normalized absolute path" 64
    local user_id lock_directory lock_file
    user_id=$(id -u)
    lock_directory=${runtime_root}/remmina-krunner-${user_id}
    if [[ ! -e ${lock_directory} && ! -L ${lock_directory} ]]; then
        mkdir -m 0700 -- "${lock_directory}"
    fi
    [[ -d ${lock_directory} && ! -L ${lock_directory} &&
       $(stat -c '%u' -- "${lock_directory}") == "${user_id}" &&
       $(stat -c '%a' -- "${lock_directory}") == 700 ]] ||
        die "Refusing unsafe installation lock directory: ${lock_directory}" 68
    lock_file=${lock_directory}/transaction.lock
    if [[ -e ${lock_file} || -L ${lock_file} ]]; then
        [[ -f ${lock_file} && ! -L ${lock_file} &&
           $(stat -c '%u' -- "${lock_file}") == "${user_id}" ]] ||
            die "Refusing unsafe installation lock file: ${lock_file}" 68
    fi
    (umask 077; : >>"${lock_file}")
    exec {install_lock_fd}<>"${lock_file}"
    flock --exclusive --nonblock "${install_lock_fd}" ||
        die "Another Remmina KRunner install or uninstall is already running." 75
}

acquire_install_lock
for destination in "${owned_paths[@]}"; do
    [[ ! -d ${destination} ]] || die "Uninstall destination must not be a directory: ${destination}" 68
done

process_start_time() {
    local pid=$1 stat_line tail
    IFS= read -r stat_line 2>/dev/null <"/proc/${pid}/stat" || return 1
    [[ ${stat_line} == *') '* ]] || return 1
    tail=${stat_line##*) }
    local -a fields=()
    read -r -a fields <<<"${tail}"
    [[ ${#fields[@]} -ge 20 && ${fields[19]} =~ ^[0-9]+$ ]] || return 1
    printf '%s\n' "${fields[19]}"
}
stop_installed_runner() {
    [[ -e ${binary_path} || -L ${binary_path} ]] || return 0
    local canonical_directory canonical_binary exe_link pid identity before after current
    canonical_directory=$(readlink -f -- "${binary_path%/*}") || return 0
    canonical_binary=${canonical_directory}/${binary_path##*/}
    local -a pids=() starts=()
    for exe_link in /proc/[0-9]*/exe; do
        pid=${exe_link#/proc/}; pid=${pid%/exe}
        [[ ${pid} =~ ^[0-9]+$ && ${pid} != $$ ]] || continue
        identity=$(readlink -- "${exe_link}" 2>/dev/null) || continue
        [[ ${identity} == "${canonical_binary}" || ${identity} == "${canonical_binary} (deleted)" ]] || continue
        before=$(process_start_time "${pid}") || continue
        after=$(process_start_time "${pid}") || continue
        current=$(readlink -- "${exe_link}" 2>/dev/null) || continue
        [[ ${before} == "${after}" && ( ${current} == "${canonical_binary}" || ${current} == "${canonical_binary} (deleted)" ) ]] || continue
        kill -TERM "${pid}" 2>/dev/null || true
        pids+=("${pid}"); starts+=("${before}")
    done
    local attempt index all_stopped
    for attempt in {1..40}; do
        all_stopped=1
        for index in "${!pids[@]}"; do
            current=$(readlink -- "/proc/${pids[index]}/exe" 2>/dev/null) || continue
            after=$(process_start_time "${pids[index]}") || continue
            if [[ ${after} == "${starts[index]}" &&
                  ( ${current} == "${canonical_binary}" || ${current} == "${canonical_binary} (deleted)" ) ]]; then
                all_stopped=0
            fi
        done
        [[ ${all_stopped} == 1 ]] && return 0
        sleep 0.05
    done
    die "Running Remmina KRunner process did not terminate; no files were removed." 68
}

stop_installed_runner
rm -f -- "${owned_paths[@]}"
cache_status=0
if ! kbuildsycoca6 >/dev/null; then
    echo "Files were removed, but KDE service cache refresh failed. Run kbuildsycoca6, then restart KRunner." >&2
    cache_status=70
fi
if command -v kquitapp6 >/dev/null 2>&1; then
    kquitapp6 krunner >/dev/null 2>&1 || true
fi
[[ ${cache_status} == 0 ]] || exit "${cache_status}"
printf 'Removed Remmina KRunner integration. Configuration was preserved.\n'
