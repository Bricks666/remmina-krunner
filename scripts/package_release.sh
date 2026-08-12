#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
# SPDX-License-Identifier: 0BSD
set -euo pipefail

if [[ $# -ne 4 ]]; then
    echo "Usage: $0 SOURCE_ROOT BUILD_DIRECTORY OUTPUT_DIRECTORY TAG" >&2
    exit 64
fi
source_root=$1; build_directory=$2; output_directory=$3; release_tag=$4
temporary_root=; publication_rollback=0; archive_identity=; checksum_identity=
archive_guard_fd=; checksum_guard_fd=
die() { echo "$1" >&2; exit "${2:-1}"; }
validate_path() {
    local name=$1 value=$2
    [[ -n ${value} && ${value} == /* && ${value} != / && ${value} != *$'\n'* && ${value} != *$'\r'* ]] ||
        die "${name} must be a bounded absolute path without newlines" 64
}
require_regular() {
    [[ -f $1 && ! -L $1 && $(stat -c '%h' -- "$1") == 1 ]] ||
        die "Missing, linked, or unsafe $2" 66
}
cleanup() {
    local status=$? path expected current
    trap - EXIT
    set +e
    if [[ ${publication_rollback} == 1 ]]; then
        for path in "${archive_path:-}" "${checksum_path:-}"; do
            [[ ${path} == "${archive_path:-}" ]] && expected=${archive_identity} || expected=${checksum_identity}
            if [[ -n ${path} && -n ${expected} && -f ${path} && ! -L ${path} ]]; then
                current=$(stat -c '%d:%i' -- "${path}" 2>/dev/null || true)
                [[ ${current} != "${expected}" ]] || rm -f -- "${path}"
            fi
        done
    fi
    [[ -z ${archive_guard_fd} ]] || exec {archive_guard_fd}<&-
    [[ -z ${checksum_guard_fd} ]] || exec {checksum_guard_fd}<&-
    if [[ -n ${temporary_root} && ${temporary_root%/*} == "${output_directory}" &&
          ${temporary_root##*/} == .remmina-release.* && ${temporary_root} != "${output_directory}" ]]; then
        rm -rf -- "${temporary_root}"
    fi
    exit "${status}"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

validate_path SOURCE_ROOT "${source_root}"
validate_path BUILD_DIRECTORY "${build_directory}"
validate_path OUTPUT_DIRECTORY "${output_directory}"
[[ ${SOURCE_DATE_EPOCH:-} =~ ^[0-9]+$ ]] || die "SOURCE_DATE_EPOCH must be decimal" 64
epoch_trimmed=$(printf '%s\n' "${SOURCE_DATE_EPOCH}" | sed 's/^0*//')
epoch_trimmed=${epoch_trimmed:-0}
if [[ ${#epoch_trimmed} -gt 12 ||
      (${#epoch_trimmed} -eq 12 && ${epoch_trimmed} > 253402300799) ]]; then
    die "SOURCE_DATE_EPOCH is outside the supported range" 64
fi
for directory in "${source_root}" "${build_directory}" "${output_directory}"; do
    [[ -d ${directory} && ! -L ${directory} ]] || die "Release paths must be real directories" 66
done
for command_name in chmod find gzip install mktemp mv readlink rm sed sha256sum sort stat tar; do
    command -v "${command_name}" >/dev/null 2>&1 || die "Missing release command: ${command_name}" 69
done
[[ $(tar --version 2>/dev/null) == 'tar (GNU tar)'* ]] || die "Release packaging requires GNU tar" 69
[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] || die "Release packaging supports Linux x86_64 only" 65

source_root=$(readlink -f -- "${source_root}") || die "Unable to resolve source" 66
build_directory=$(readlink -f -- "${build_directory}") || die "Unable to resolve build" 66
output_directory=$(readlink -f -- "${output_directory}") || die "Unable to resolve output" 66
[[ ${source_root} != / && ${build_directory} != / && ${output_directory} != / ]] || die "Release paths must be bounded" 64
source_identity=$(stat -c '%d:%i' -- "${source_root}")
build_identity=$(stat -c '%d:%i' -- "${build_directory}")
output_identity=$(stat -c '%d:%i' -- "${output_directory}")
if [[ ${output_directory} == "${source_root}" || ${output_directory} == "${source_root}"/* ||
      ${output_directory} == "${build_directory}" || ${output_directory} == "${build_directory}"/* ||
      ${output_identity} == "${source_identity}" || ${output_identity} == "${build_identity}" ]]; then
    die "Release output must be outside source and build directories" 64
fi

validator=${source_root}/scripts/validate_release_tag.sh
cmake_file=${source_root}/CMakeLists.txt
layout_file=${build_directory}/RemminaKRunnerInstallLayout.cmake
built_binary=${build_directory}/remmina-krunner
built_desktop=${build_directory}/org.remminakrunner.KRunner.desktop
built_service=${build_directory}/org.remminakrunner.KRunner.service
built_install=${build_directory}/packaging/install.sh
built_uninstall=${build_directory}/packaging/uninstall.sh
for record in "${validator}|tag validator" "${cmake_file}|project file" "${layout_file}|install layout" \
    "${source_root}/LICENSE|license" "${source_root}/LICENSES/0BSD.txt|0BSD license text" \
    "${source_root}/LICENSES/LGPL-2.0-or-later.txt|LGPL license text" "${built_binary}|runner" \
    "${built_desktop}|KRunner metadata" "${built_service}|D-Bus service" \
    "${built_install}|configured installer" "${built_uninstall}|configured uninstaller"; do
    require_regular "${record%%|*}" "${record#*|}"
done
mapfile -t plugins < <(sed -nE 's/^[[:space:]]*\[\[([^]]+)\]\][[:space:]]*\)?[[:space:]]*$/\1/p' "${layout_file}")
if [[ ${#plugins[@]} -ne 1 || -z ${plugins[0]} || ${plugins[0]} == /* || ${plugins[0]} == *..* ||
      ${plugins[0]} == *$'\n'* || ${plugins[0]} != */kcm_remmina_krunner.so ]]; then
    die "Configured plugin layout is invalid" 66
fi
plugin_relative=${plugins[0]}
built_plugin=${build_directory}/bin/kf6/krunner/kcms/kcm_remmina_krunner.so
require_regular "${built_plugin}" "KCModule plugin"
version=$("${validator}" "${release_tag}" "${cmake_file}")
[[ ${version} == "${release_tag#v}" ]] || die "Release validator returned an invalid version" 65
exec_count=$(grep -c '^Exec=' "${built_service}" || true)
[[ ${exec_count} -eq 1 ]] || die "Generated service must contain exactly one Exec" 66

package_name=remmina-krunner-${release_tag}-linux-x86_64
archive_name=${package_name}.tar.gz; checksum_name=${archive_name}.sha256
archive_path=${output_directory}/${archive_name}; checksum_path=${output_directory}/${checksum_name}
[[ ! -e ${archive_path} && ! -L ${archive_path} && ! -e ${checksum_path} && ! -L ${checksum_path} ]] ||
    die "Release output already exists" 73
temporary_root=$(mktemp -d -- "${output_directory}/.remmina-release.XXXXXX")
[[ ${temporary_root%/*} == "${output_directory}" && ${temporary_root##*/} == .remmina-release.* &&
   -d ${temporary_root} && ! -L ${temporary_root} ]] || die "Unable to create bounded staging" 73
payload=${temporary_root}/payload/${package_name}
install -d -m 0755 -- "${payload}/bin" "${payload}/${plugin_relative%/*}" \
    "${payload}/share/dbus-1/services" "${payload}/share/krunner/dbusplugins" "${payload}/LICENSES"
install -m 0644 -- "${source_root}/LICENSE" "${payload}/LICENSE"
install -m 0644 -- "${source_root}/LICENSES/0BSD.txt" "${payload}/LICENSES/0BSD.txt"
install -m 0644 -- "${source_root}/LICENSES/LGPL-2.0-or-later.txt" "${payload}/LICENSES/LGPL-2.0-or-later.txt"
install -m 0755 -- "${built_binary}" "${payload}/bin/remmina-krunner"
install -m 0755 -- "${built_plugin}" "${payload}/${plugin_relative}"
install -m 0755 -- "${built_install}" "${payload}/install.sh"
install -m 0755 -- "${built_uninstall}" "${payload}/uninstall.sh"
install -m 0644 -- "${built_desktop}" "${payload}/share/krunner/dbusplugins/org.remminakrunner.KRunner.desktop"
service_output=${payload}/share/dbus-1/services/org.remminakrunner.KRunner.service
while IFS= read -r line || [[ -n ${line} ]]; do
    [[ ${line} == Exec=* ]] && printf '%s\n' 'Exec=@REMMINA_KRUNNER_EXEC@' || printf '%s\n' "${line}"
done <"${built_service}" >"${service_output}"
chmod 0644 -- "${service_output}"

expected_files=(LICENSE LICENSES/0BSD.txt LICENSES/LGPL-2.0-or-later.txt bin/remmina-krunner install.sh
    "${plugin_relative}" share/dbus-1/services/org.remminakrunner.KRunner.service
    share/krunner/dbusplugins/org.remminakrunner.KRunner.desktop uninstall.sh)
mapfile -t actual_files < <(find "${payload}" -mindepth 1 -type f -printf '%P\n' | LC_ALL=C sort)
mapfile -t sorted_expected < <(printf '%s\n' "${expected_files[@]}" | LC_ALL=C sort)
[[ ${actual_files[*]} == "${sorted_expected[*]}" ]] || die "Release staging inventory is invalid" 70
while IFS= read -r -d '' entry; do
    [[ ! -L ${entry} ]] || die "Release staging contains a symlink" 70
    if [[ -f ${entry} ]]; then
        [[ $(stat -c '%h' -- "${entry}") == 1 ]] || die "Release staging contains a hard link" 70
    elif [[ ! -d ${entry} ]]; then
        die "Release staging contains a special entry" 70
    fi
done < <(find "${payload}" -mindepth 1 -print0)

staged_archive=${temporary_root}/${archive_name}; staged_checksum=${temporary_root}/${checksum_name}
LC_ALL=C TZ=UTC tar --sort=name --format=gnu --mtime="@${SOURCE_DATE_EPOCH}" --owner=0 --group=0 \
    --numeric-owner -C "${temporary_root}/payload" -cf - "${package_name}" | gzip -n -9 >"${staged_archive}"
(cd -- "${temporary_root}"; sha256sum "${archive_name}" >"${checksum_name}"; sha256sum --check "${checksum_name}" >/dev/null)
checksum_line=$(<"${staged_checksum}")
[[ ${checksum_line} =~ ^[0-9a-f]{64}[[:space:]][[:space:]]${archive_name}$ ]] || die "Invalid checksum output" 70

# Validate the final bytes by extracting into a bounded staging directory.
verify=${temporary_root}/verify; install -d -m 0755 -- "${verify}"
tar -xzf "${staged_archive}" -C "${verify}" --no-same-owner
mapfile -t verified_files < <(find "${verify}/${package_name}" -mindepth 1 -type f -printf '%P\n' | LC_ALL=C sort)
[[ ${verified_files[*]} == "${sorted_expected[*]}" ]] || die "Final archive inventory is invalid" 70
if find "${verify}" -type l -o -type p -o -type s -o -type b -o -type c | grep . >/dev/null; then
    die "Final archive contains an unsafe type" 70
fi

exec {archive_guard_fd}<"${staged_archive}"; exec {checksum_guard_fd}<"${staged_checksum}"
archive_identity=$(stat -Lc '%d:%i' -- "/proc/self/fd/${archive_guard_fd}")
checksum_identity=$(stat -Lc '%d:%i' -- "/proc/self/fd/${checksum_guard_fd}")
[[ ! -e ${archive_path} && ! -L ${archive_path} && ! -e ${checksum_path} && ! -L ${checksum_path} ]] || die "Release collision" 73
publication_rollback=1
mv -n -T -- "${staged_archive}" "${archive_path}"
[[ ! -e ${staged_archive} && -f ${archive_path} && ! -L ${archive_path} &&
   $(stat -c '%d:%i' -- "${archive_path}") == "${archive_identity}" ]] || die "Archive publication failed" 73
mv -n -T -- "${staged_checksum}" "${checksum_path}"
[[ ! -e ${staged_checksum} && -f ${checksum_path} && ! -L ${checksum_path} &&
   $(stat -c '%d:%i' -- "${checksum_path}") == "${checksum_identity}" ]] || die "Checksum publication failed" 73
(cd -- "${output_directory}"; sha256sum --check "${checksum_name}" >/dev/null)
[[ $(stat -c '%d:%i' -- "${archive_path}") == "${archive_identity}" &&
   $(stat -c '%d:%i' -- "${checksum_path}") == "${checksum_identity}" ]] || die "Published outputs changed" 73
publication_rollback=0
exec {archive_guard_fd}<&-; archive_guard_fd=
exec {checksum_guard_fd}<&-; checksum_guard_fd=
printf '%s\n%s\n' "${archive_path}" "${checksum_path}"
