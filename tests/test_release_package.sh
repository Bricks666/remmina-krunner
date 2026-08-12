#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
# SPDX-License-Identifier: 0BSD
set -euo pipefail

if [[ $# -ne 18 ]]; then
    echo "Usage: $0 PACKAGE VALIDATOR SOURCE BINARY PLUGIN DESKTOP SERVICE INSTALL UNINSTALL LAYOUT TAR GZIP SHA FIND STAT CMP MV TOUCH" >&2
    exit 64
fi

package_script=$1; validate_script=$2; source_root=$3; built_binary=$4
built_plugin=$5; built_desktop=$6; built_service=$7; built_install=$8
built_uninstall=$9; layout_file=${10}; tar_command=${11}; gzip_command=${12}
sha256sum_command=${13}; find_command=${14}; stat_command=${15}
cmp_command=${16}; mv_command=${17}; touch_command=${18}
for path in "$@"; do
    [[ ${path} == /* ]] || { echo "Test argument is not absolute: ${path}" >&2; exit 64; }
done

test_root=$(mktemp -d /tmp/remmina-release-package-test.XXXXXX)
cleanup() { rm -rf -- "${test_root}"; }
trap cleanup EXIT
fixture_source=${test_root}/source; fixture_build=${test_root}/build
mkdir -p -- "${fixture_source}/LICENSES" "${fixture_source}/scripts" \
    "${fixture_build}/bin/kf6/krunner/kcms" "${fixture_build}/packaging"
cp -- "${source_root}/CMakeLists.txt" "${fixture_source}/CMakeLists.txt"
cp -- "${source_root}/LICENSE" "${fixture_source}/LICENSE"
cp -- "${source_root}/LICENSES/0BSD.txt" "${fixture_source}/LICENSES/0BSD.txt"
cp -- "${source_root}/LICENSES/LGPL-2.0-or-later.txt" "${fixture_source}/LICENSES/LGPL-2.0-or-later.txt"
cp -- "${validate_script}" "${fixture_source}/scripts/validate_release_tag.sh"
cp -- "${built_binary}" "${fixture_build}/remmina-krunner"
cp -- "${built_plugin}" "${fixture_build}/bin/kf6/krunner/kcms/kcm_remmina_krunner.so"
cp -- "${built_desktop}" "${fixture_build}/org.remminakrunner.KRunner.desktop"
cp -- "${built_service}" "${fixture_build}/org.remminakrunner.KRunner.service"
cp -- "${built_install}" "${fixture_build}/packaging/install.sh"
cp -- "${built_uninstall}" "${fixture_build}/packaging/uninstall.sh"
cp -- "${layout_file}" "${fixture_build}/RemminaKRunnerInstallLayout.cmake"
chmod 0755 -- "${fixture_source}/scripts/validate_release_tag.sh" \
    "${fixture_build}/remmina-krunner" "${fixture_build}/bin/kf6/krunner/kcms/kcm_remmina_krunner.so" \
    "${fixture_build}/packaging/install.sh" "${fixture_build}/packaging/uninstall.sh"

archive=remmina-krunner-v0.1.0-linux-x86_64.tar.gz; checksum=${archive}.sha256; top=${archive%.tar.gz}
assert_empty() { [[ -z $(${find_command} "$1" -mindepth 1 -maxdepth 1 -print -quit) ]] || { echo "Failure left partial output in $1" >&2; exit 1; }; }
expect_clean_failure() { local output=$1; shift; mkdir -p -- "${output}"; if "$@"; then echo "Expected packaging failure: $*" >&2; exit 1; fi; assert_empty "${output}"; }

output_one=${test_root}/output-one; output_two=${test_root}/output-two
mkdir -p -- "${output_one}" "${output_two}"
SOURCE_DATE_EPOCH=1700000000 "${package_script}" "${fixture_source}" "${fixture_build}" "${output_one}" v0.1.0
"${touch_command}" -d '@1200000000' -- "${fixture_source}/LICENSE" "${fixture_source}/LICENSES/0BSD.txt" \
    "${fixture_source}/LICENSES/LGPL-2.0-or-later.txt" "${fixture_build}/remmina-krunner" \
    "${fixture_build}/bin/kf6/krunner/kcms/kcm_remmina_krunner.so" \
    "${fixture_build}/org.remminakrunner.KRunner.desktop" "${fixture_build}/org.remminakrunner.KRunner.service" \
    "${fixture_build}/packaging/install.sh" "${fixture_build}/packaging/uninstall.sh"
chmod 0600 -- "${fixture_build}/remmina-krunner" "${fixture_build}/bin/kf6/krunner/kcms/kcm_remmina_krunner.so" \
    "${fixture_build}/packaging/install.sh" "${fixture_build}/packaging/uninstall.sh"
chmod 0755 -- "${fixture_source}/LICENSE" "${fixture_build}/org.remminakrunner.KRunner.desktop" \
    "${fixture_build}/org.remminakrunner.KRunner.service"
SOURCE_DATE_EPOCH=1700000000 "${package_script}" "${fixture_source}" "${fixture_build}" "${output_two}" v0.1.0

expected_outputs=$(printf '%s\n' "${archive} f" "${checksum} f")
actual_outputs=$(${find_command} "${output_one}" -mindepth 1 -maxdepth 1 -printf '%f %y\n' | LC_ALL=C sort)
[[ ${actual_outputs} == "${expected_outputs}" ]] || { echo "Unexpected release outputs: ${actual_outputs}" >&2; exit 1; }
(cd -- "${output_one}"; "${sha256sum_command}" --check "${checksum}")
checksum_line=$(<"${output_one}/${checksum}")
[[ ${checksum_line} =~ ^[0-9a-f]{64}[[:space:]][[:space:]]${archive}$ ]] || { echo "Invalid checksum line" >&2; exit 1; }
hash_one=$("${sha256sum_command}" "${output_one}/${archive}"); hash_one=${hash_one%% *}
hash_two=$("${sha256sum_command}" "${output_two}/${archive}"); hash_two=${hash_two%% *}
[[ ${hash_one} == "${hash_two}" ]]; "${cmp_command}" -- "${output_one}/${archive}" "${output_two}/${archive}"

mapfile -t files < <("${tar_command}" -tzf "${output_one}/${archive}" | sed -n '/\/$/!p' | LC_ALL=C sort)
expected_files=("${top}/LICENSE" "${top}/LICENSES/0BSD.txt" "${top}/LICENSES/LGPL-2.0-or-later.txt" \
    "${top}/bin/remmina-krunner" "${top}/install.sh" \
    "${top}/lib64/plugins/kf6/krunner/kcms/kcm_remmina_krunner.so" \
    "${top}/share/dbus-1/services/org.remminakrunner.KRunner.service" \
    "${top}/share/krunner/dbusplugins/org.remminakrunner.KRunner.desktop" "${top}/uninstall.sh")
[[ ${files[*]} == "${expected_files[*]}" ]] || { printf 'Unexpected 9-file archive inventory:\n%s\n' "${files[*]}" >&2; exit 1; }
regular_count=0
while IFS= read -r line; do
    mode=${line%% *}; owner_and_rest=${line#* }; owner=${owner_and_rest%% *}
    [[ ${owner} == 0/0 && ${line} == *' 2023-11-14 22:13:20 '* ]] || { echo "Nondeterministic archive metadata: ${line}" >&2; exit 1; }
    case ${mode:0:1} in -) ((regular_count += 1));; d) ;; *) echo "Unsafe archive member: ${line}" >&2; exit 1;; esac
done < <(TZ=UTC LC_ALL=C "${tar_command}" --full-time --numeric-owner -tvzf "${output_one}/${archive}")
[[ ${regular_count} -eq 9 ]]
if "${gzip_command}" -dc -- "${output_one}/${archive}" | LC_ALL=C grep -aE '[0-9]+ (atime|ctime)=' >/dev/null; then echo "Ambient pax metadata" >&2; exit 1; fi

extract=${test_root}/extract; mkdir -p -- "${extract}"; "${tar_command}" -xzf "${output_one}/${archive}" -C "${extract}"
package_root=${extract}/${top}
for executable in bin/remmina-krunner install.sh lib64/plugins/kf6/krunner/kcms/kcm_remmina_krunner.so uninstall.sh; do
    [[ $("${stat_command}" -c '%a' -- "${package_root}/${executable}") == 755 ]] || exit 1
done
for data in LICENSE LICENSES/0BSD.txt LICENSES/LGPL-2.0-or-later.txt \
    share/dbus-1/services/org.remminakrunner.KRunner.service share/krunner/dbusplugins/org.remminakrunner.KRunner.desktop; do
    [[ $("${stat_command}" -c '%a' -- "${package_root}/${data}") == 644 ]] || exit 1
done
service=${package_root}/share/dbus-1/services/org.remminakrunner.KRunner.service
[[ $(grep -c '^Exec=' "${service}") -eq 1 ]]; grep -qx 'Exec=@REMMINA_KRUNNER_EXEC@' "${service}"
if grep -R -a -E '/workspace|build-(test|ci|release)|/home/|\.config/remmina(/|$)|offline_authenticator\.db' "${package_root}/share" "${package_root}/install.sh" "${package_root}/uninstall.sh" >/dev/null; then echo "Release exposes build/host configuration" >&2; exit 1; fi

wrong=${test_root}/wrong; expect_clean_failure "${wrong}" env SOURCE_DATE_EPOCH=1700000000 "${package_script}" "${fixture_source}" "${fixture_build}" "${wrong}" v0.1.1
for epoch in '' not-a-number -1 253402300800; do
    output=${test_root}/bad-epoch-${epoch//[^A-Za-z0-9]/x}
    if [[ -z ${epoch} ]]; then expect_clean_failure "${output}" env -u SOURCE_DATE_EPOCH "${package_script}" "${fixture_source}" "${fixture_build}" "${output}" v0.1.0
    else expect_clean_failure "${output}" env SOURCE_DATE_EPOCH="${epoch}" "${package_script}" "${fixture_source}" "${fixture_build}" "${output}" v0.1.0; fi
done
source_child=${fixture_source}/out; build_child=${fixture_build}/out; mkdir -p -- "${source_child}" "${build_child}"
expect_clean_failure "${source_child}" env SOURCE_DATE_EPOCH=1700000000 "${package_script}" "${fixture_source}/." "${fixture_build}" "${source_child}" v0.1.0
expect_clean_failure "${build_child}" env SOURCE_DATE_EPOCH=1700000000 "${package_script}" "${fixture_source}" "${fixture_build}" "${build_child}" v0.1.0
collision=${test_root}/collision; mkdir -p -- "${collision}"; printf 'foreign\n' >"${collision}/${archive}"
if env SOURCE_DATE_EPOCH=1700000000 "${package_script}" "${fixture_source}" "${fixture_build}" "${collision}" v0.1.0; then exit 1; fi
[[ $(<"${collision}/${archive}") == foreign && ! -e ${collision}/${checksum} ]]
unsafe_build=${test_root}/unsafe-build; cp -a -- "${fixture_build}" "${unsafe_build}"; rm -f -- "${unsafe_build}/remmina-krunner"; ln -s -- /dev/null "${unsafe_build}/remmina-krunner"
unsafe_output=${test_root}/unsafe-output; expect_clean_failure "${unsafe_output}" env SOURCE_DATE_EPOCH=1700000000 "${package_script}" "${fixture_source}" "${unsafe_build}" "${unsafe_output}" v0.1.0
hardlink_build=${test_root}/hardlink-build; cp -a -- "${fixture_build}" "${hardlink_build}"; ln -- "${hardlink_build}/remmina-krunner" "${hardlink_build}/runner-hardlink"
hardlink_output=${test_root}/hardlink-output; expect_clean_failure "${hardlink_output}" env SOURCE_DATE_EPOCH=1700000000 "${package_script}" "${fixture_source}" "${hardlink_build}" "${hardlink_output}" v0.1.0
printf 'release_archive_sha256=%s\n' "${hash_one}"
