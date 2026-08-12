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
require_commands() {
    local command_name
    for command_name in find flock grep id install ldd mkdir mktemp mv readlink rm sleep sort stat uname kbuildsycoca6; do
        command -v "${command_name}" >/dev/null 2>&1 ||
            die "Required command is unavailable: ${command_name}" 69
    done
}

home_directory=${HOME:-}
validate_path HOME "${home_directory}"
install_prefix=${REMMINA_KRUNNER_INSTALL_PREFIX:-${home_directory}/.local}
validate_path REMMINA_KRUNNER_INSTALL_PREFIX "${install_prefix}"
if [[ -n ${XDG_DATA_HOME:-} ]]; then
    data_home=${XDG_DATA_HOME}
else
    data_home=${home_directory}/.local/share
fi
validate_path XDG_DATA_HOME "${data_home}"

require_commands
[[ $(uname -s) == Linux ]] || die "This release bundle supports Linux only" 65
case $(uname -m) in x86_64|aarch64) ;; *) die "Unsupported Linux architecture" 65 ;; esac

script_path=$(readlink -f -- "${BASH_SOURCE[0]}") || die "Unable to resolve installer path" 66
package_root=${script_path%/*}
plugin_relative=lib64/plugins/kf6/krunner/kcms/kcm_remmina_krunner.so
expected_files=(
    LICENSE
    LICENSES/0BSD.txt
    LICENSES/LGPL-2.0-or-later.txt
    bin/remmina-krunner
    install.sh
    "${plugin_relative}"
    share/dbus-1/services/org.remminakrunner.KRunner.service
    share/krunner/dbusplugins/org.remminakrunner.KRunner.desktop
    uninstall.sh
)
mapfile -t actual_files < <(find "${package_root}" -mindepth 1 -type f -printf '%P\n' | LC_ALL=C sort)
mapfile -t sorted_expected < <(printf '%s\n' "${expected_files[@]}" | LC_ALL=C sort)
[[ ${actual_files[*]} == "${sorted_expected[*]}" ]] || die "Release bundle inventory is invalid" 66

while IFS= read -r -d '' entry; do
    relative=${entry#"${package_root}"/}
    [[ ! -L ${entry} ]] || die "Release bundle contains a symbolic link: ${relative}" 66
    if [[ -d ${entry} ]]; then
        case ${relative} in
            LICENSES|bin|lib64|lib64/plugins|lib64/plugins/kf6|lib64/plugins/kf6/krunner|lib64/plugins/kf6/krunner/kcms|share|share/dbus-1|share/dbus-1/services|share/krunner|share/krunner/dbusplugins) ;;
            *) die "Release bundle contains an unexpected directory: ${relative}" 66 ;;
        esac
    elif [[ -f ${entry} ]]; then
        [[ $(stat -c '%h' -- "${entry}") == 1 ]] ||
            die "Release bundle file must not be hard-linked: ${relative}" 66
    else
        die "Release bundle contains a non-regular entry: ${relative}" 66
    fi
done < <(find "${package_root}" -mindepth 1 -print0)

payload_binary=${package_root}/bin/remmina-krunner
payload_plugin=${package_root}/${plugin_relative}
payload_desktop=${package_root}/share/krunner/dbusplugins/org.remminakrunner.KRunner.desktop
payload_service=${package_root}/share/dbus-1/services/org.remminakrunner.KRunner.service
for executable in "${payload_binary}" "${package_root}/install.sh" "${package_root}/uninstall.sh"; do
    [[ $(stat -c '%a' -- "${executable}") == 755 ]] ||
        die "Release bundle executable must have mode 0755: ${executable##*/}" 66
done
[[ $(stat -c '%a' -- "${payload_plugin}") == 755 ]] ||
    die "Release bundle plugin must have mode 0755" 66
for data_file in "${payload_desktop}" "${payload_service}" \
                 "${package_root}/LICENSE" "${package_root}/LICENSES/0BSD.txt" \
                 "${package_root}/LICENSES/LGPL-2.0-or-later.txt"; do
    [[ $(stat -c '%a' -- "${data_file}") == 644 ]] ||
        die "Release bundle data files must have mode 0644" 66
done

for runtime_file in "${payload_binary}" "${payload_plugin}"; do
    if ! ldd_output=$(LC_ALL=C ldd -- "${runtime_file}" 2>&1) || [[ ${ldd_output} == *'not found'* ]]; then
        printf '%s\n' "${ldd_output}" >&2
        die "Install the missing runtime libraries for your Linux distribution, then retry." 67
    fi
done

mapfile -t exec_lines < <(grep '^Exec=' "${payload_service}" || true)
[[ ${#exec_lines[@]} -eq 1 ]] || die "D-Bus service metadata must contain exactly one Exec entry" 66

binary_path=${install_prefix}/bin/remmina-krunner
plugin_path=${install_prefix}/${plugin_relative}
desktop_path=${data_home}/krunner/dbusplugins/org.remminakrunner.KRunner.desktop
service_path=${data_home}/dbus-1/services/org.remminakrunner.KRunner.service
destination_paths=("${binary_path}" "${plugin_path}" "${desktop_path}" "${service_path}")
for destination in "${destination_paths[@]}"; do
    [[ ! -d ${destination} && ! -L ${destination} ]] ||
        die "Install destination must be an absent or regular file: ${destination}" 68
done

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
    [[ -e ${binary_path} ]] || return 0
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
    die "Running Remmina KRunner process did not terminate; no installed files were changed." 68
}

directories=("${binary_path%/*}" "${plugin_path%/*}" "${desktop_path%/*}" "${service_path%/*}")
mkdir -p -m 0755 -- "${directories[@]}"
staged_paths=("" "" "" "")
backup_paths=("" "" "" "")
had_original=(0 0 0 0)
replacement_installed=(0 0 0 0)
transaction_active=0
cleanup() {
    local status=$? index
    trap - EXIT
    set +e
    if [[ ${transaction_active} == 1 ]]; then
        for index in 3 2 1 0; do
            if [[ ${replacement_installed[index]} == 1 ]]; then
                rm -f -- "${destination_paths[index]}"
            fi
            if [[ ${had_original[index]} == 1 &&
                  ( -e ${backup_paths[index]} || -L ${backup_paths[index]} ) ]]; then
                mv -fT -- "${backup_paths[index]}" "${destination_paths[index]}" || status=74
                backup_paths[index]=
            fi
        done
    fi
    for index in 0 1 2 3; do
        [[ -z ${staged_paths[index]} || ! -e ${staged_paths[index]} ]] || rm -f -- "${staged_paths[index]}" || status=74
        [[ -z ${backup_paths[index]} || ! -e ${backup_paths[index]} ]] || rm -f -- "${backup_paths[index]}" || status=74
    done
    exit "${status}"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

for index in 0 1 2 3; do
    staged_paths[index]=$(mktemp "${destination_paths[index]%/*}/.${destination_paths[index]##*/}.stage.XXXXXX")
done
install -m 0755 -- "${payload_binary}" "${staged_paths[0]}"
install -m 0755 -- "${payload_plugin}" "${staged_paths[1]}"
install -m 0644 -- "${payload_desktop}" "${staged_paths[2]}"
install -m 0644 /dev/null "${staged_paths[3]}"
escaped_binary=${binary_path//\/\\\\}
escaped_binary=${escaped_binary//"/\\"}
while IFS= read -r line || [[ -n ${line} ]]; do
    if [[ ${line} == Exec=* ]]; then
        printf 'Exec="%s"\n' "${escaped_binary}" >>"${staged_paths[3]}"
    else
        printf '%s\n' "${line}" >>"${staged_paths[3]}"
    fi
done <"${payload_service}"

stop_installed_runner
transaction_active=1
for index in 0 1 2 3; do
    if [[ -e ${destination_paths[index]} ]]; then
        had_original[index]=1
        backup_paths[index]=$(mktemp "${destination_paths[index]%/*}/.${destination_paths[index]##*/}.backup.XXXXXX")
        rm -f -- "${backup_paths[index]}"
        mv -fT -- "${destination_paths[index]}" "${backup_paths[index]}"
    fi
done
for index in 0 1 2 3; do
    mv -fT -- "${staged_paths[index]}" "${destination_paths[index]}"
    staged_paths[index]=
    replacement_installed[index]=1
done
transaction_active=0
for index in 0 1 2 3; do
    [[ -z ${backup_paths[index]} ]] || rm -f -- "${backup_paths[index]}"
    backup_paths[index]=
done

post_status=0
if ! "${binary_path}" --rescan; then
    echo "Files were installed, but the initial Remmina instance scan failed. Run '${binary_path}' --rescan." >&2
    post_status=70
fi
if ! kbuildsycoca6 >/dev/null; then
    echo "Files were installed, but KDE service cache refresh failed. Run kbuildsycoca6, then restart KRunner." >&2
    post_status=70
fi
if command -v kquitapp6 >/dev/null 2>&1; then
    kquitapp6 krunner >/dev/null 2>&1 || true
fi
[[ ${post_status} == 0 ]] || exit "${post_status}"
printf 'Installed Remmina KRunner integration.\n'
