#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
# SPDX-License-Identifier: 0BSD
set -euo pipefail

installer=${1:?installer path required}
uninstaller=${2:?uninstaller path required}
runner=${3:?runner path required}
root=$(mktemp -d /tmp/remmina-krunner-package-test.XXXXXX)
case ${root} in /tmp/remmina-krunner-package-test.*) ;; *) exit 1 ;; esac
cleanup() {
    local pid
    for pid in "${runner_pid:-}" "${unrelated_pid:-}" "${installer_pid:-}"; do
        if [[ ${pid} =~ ^[0-9]+$ ]] && kill -0 "${pid}" 2>/dev/null; then
            kill -KILL "${pid}" 2>/dev/null || true
            wait "${pid}" 2>/dev/null || true
        fi
    done
    case ${root} in /tmp/remmina-krunner-package-test.*) rm -rf -- "${root}" ;; esac
}
trap cleanup EXIT

fail() { echo "package_scripts: $*" >&2; exit 1; }
snapshot() {
    local path=$1
    [[ -e ${path} ]] || { printf 'absent\n'; return; }
    tar --sort=name --format=gnu --mtime=@0 --owner=0 --group=0 --numeric-owner \
        -cf - -C "${path}" . | sha256sum | cut -d' ' -f1
}
assert_failure_without_mutation() {
    local label=$1 watched=$2; shift 2
    local before after
    before=$(snapshot "${watched}")
    if "$@" >"${root}/${label}.out" 2>"${root}/${label}.err"; then
        fail "${label}: expected failure"
    fi
    after=$(snapshot "${watched}")
    [[ ${before} == "${after}" ]] || fail "${label}: mutated destinations"
}
assert_mode() {
    [[ -f $1 ]] || fail "missing $1"
    [[ $(stat -c '%a' -- "$1") == "$2" ]] || fail "wrong mode for $1"
}

fake_bin=${root}/fake-bin
calls=${root}/calls
helper_log=${root}/runner-calls
mkdir -p -- "${fake_bin}"
: >"${calls}"
: >"${helper_log}"
for tool in find install mkdir mktemp readlink rm sleep sort stat; do
    ln -s "/usr/bin/${tool}" "${fake_bin}/${tool}"
done
cat >"${fake_bin}/uname" <<'EOF'
#!/usr/bin/bash
[[ $1 == -s ]] && printf '%s\n' "${FAKE_UNAME_S:-Linux}" || printf '%s\n' "${FAKE_UNAME_M:-x86_64}"
EOF
cat >"${fake_bin}/ldd" <<'EOF'
#!/usr/bin/bash
printf '%s\n' "${FAKE_LDD_OUTPUT:-libQt6Core.so.6 => /usr/lib64/libQt6Core.so.6}"
[[ ${FAKE_LDD_MISSING:-0} == 0 ]]
EOF
cat >"${fake_bin}/kbuildsycoca6" <<'EOF'
#!/usr/bin/bash
printf 'kbuildsycoca6\n' >>"${FAKE_CALL_LOG:?}"
EOF
cat >"${fake_bin}/kquitapp6" <<'EOF'
#!/usr/bin/bash
printf 'kquitapp6 %s\n' "$*" >>"${FAKE_CALL_LOG:?}"
EOF
cat >"${fake_bin}/mv" <<'EOF'
#!/usr/bin/bash
printf 'mv %s\n' "$*" >>"${FAKE_CALL_LOG:?}"
count=0
[[ ! -f ${FAKE_MV_COUNT_FILE:-/nonexistent} ]] || read -r count <"${FAKE_MV_COUNT_FILE}"
count=$((count + 1))
[[ -z ${FAKE_MV_COUNT_FILE:-} ]] || printf '%s\n' "${count}" >"${FAKE_MV_COUNT_FILE}"
[[ ${FAKE_MV_FAIL_AT:-0} != "${count}" ]] || exit 73
exec /usr/bin/mv "$@"
EOF
chmod 0755 "${fake_bin}"/{uname,ldd,kbuildsycoca6,kquitapp6,mv}

make_bundle() {
    local bundle=$1 payload=${2:-${runner}}
    mkdir -p -- "${bundle}/bin" \
        "${bundle}/lib64/plugins/kf6/krunner/kcms" \
        "${bundle}/share/dbus-1/services" \
        "${bundle}/share/krunner/dbusplugins" "${bundle}/LICENSES"
    cp -- "${payload}" "${bundle}/bin/remmina-krunner"
    printf 'plugin\n' >"${bundle}/lib64/plugins/kf6/krunner/kcms/kcm_remmina_krunner.so"
    printf '[Desktop Entry]\nType=Service\n' >"${bundle}/share/krunner/dbusplugins/org.remminakrunner.KRunner.desktop"
    printf '[D-BUS Service]\nName=org.remminakrunner.KRunner\nExec="/staged/bin/remmina-krunner"\n' \
        >"${bundle}/share/dbus-1/services/org.remminakrunner.KRunner.service"
    printf 'license\n' >"${bundle}/LICENSE"
    printf '0bsd\n' >"${bundle}/LICENSES/0BSD.txt"
    printf 'lgpl\n' >"${bundle}/LICENSES/LGPL-2.0-or-later.txt"
    cp -- "${installer}" "${bundle}/install.sh"
    cp -- "${uninstaller}" "${bundle}/uninstall.sh"
    chmod 0755 "${bundle}/bin/remmina-krunner" "${bundle}/install.sh" "${bundle}/uninstall.sh"
    chmod 0755 "${bundle}/lib64/plugins/kf6/krunner/kcms/kcm_remmina_krunner.so"
    chmod 0644 "${bundle}/share/dbus-1/services/org.remminakrunner.KRunner.service" \
        "${bundle}/share/krunner/dbusplugins/org.remminakrunner.KRunner.desktop" \
        "${bundle}/LICENSE" "${bundle}/LICENSES/0BSD.txt" "${bundle}/LICENSES/LGPL-2.0-or-later.txt"
}
run_install() {
    local bundle=$1 home=$2 data=${3:-} prefix=${4:-}
    HOME=${home} XDG_CONFIG_HOME="${home}/.config" XDG_DATA_HOME=${data} \
        REMMINA_KRUNNER_INSTALL_PREFIX=${prefix} FAKE_CALL_LOG=${calls} \
        PACKAGE_HELPER_LOG=${helper_log} \
        PACKAGE_HELPER_BLOCK_READY=${PACKAGE_HELPER_BLOCK_READY:-} \
        PACKAGE_HELPER_BLOCK_RELEASE=${PACKAGE_HELPER_BLOCK_RELEASE:-} \
        FAKE_LDD_MISSING=${FAKE_LDD_MISSING:-0} FAKE_MV_FAIL_AT=${FAKE_MV_FAIL_AT:-0} \
        FAKE_MV_COUNT_FILE=${FAKE_MV_COUNT_FILE:-} \
        PATH="${fake_bin}:/usr/bin:/bin" /usr/bin/bash "${bundle}/install.sh"
}
run_uninstall() {
    local bundle=$1 home=$2 data=${3:-} prefix=${4:-}
    HOME=${home} XDG_CONFIG_HOME="${home}/.config" XDG_DATA_HOME=${data} \
        REMMINA_KRUNNER_INSTALL_PREFIX=${prefix} FAKE_CALL_LOG=${calls} \
        PACKAGE_HELPER_LOG=${helper_log} \
        PATH="${fake_bin}:/usr/bin:/bin" /usr/bin/bash "${bundle}/uninstall.sh"
}

# Default locations, modes, exact runtime inventory, and initial rescan.
bundle=${root}/bundle
home=${root}/home
make_bundle "${bundle}"
mkdir -p -- "${home}"
run_install "${bundle}" "${home}"
binary=${home}/.local/bin/remmina-krunner
plugin=${home}/.local/lib64/plugins/kf6/krunner/kcms/kcm_remmina_krunner.so
desktop=${home}/.local/share/krunner/dbusplugins/org.remminakrunner.KRunner.desktop
service=${home}/.local/share/dbus-1/services/org.remminakrunner.KRunner.service
assert_mode "${binary}" 755
assert_mode "${plugin}" 755
assert_mode "${desktop}" 644
assert_mode "${service}" 644
mapfile -t files < <(find "${home}" -type f -printf '%P\n' | LC_ALL=C sort)
expected=(.local/bin/remmina-krunner .local/lib64/plugins/kf6/krunner/kcms/kcm_remmina_krunner.so .local/share/dbus-1/services/org.remminakrunner.KRunner.service .local/share/krunner/dbusplugins/org.remminakrunner.KRunner.desktop)
[[ ${files[*]} == "${expected[*]}" ]] || fail "default runtime inventory differs: ${files[*]}"
[[ $(grep -c '^--rescan$' "${helper_log}") == 1 ]] || fail "initial --rescan was not invoked exactly once"

# Script behavior is independent of the caller's working directory.
(cd / && run_install "${bundle}" "${home}")

# Custom prefix and XDG home survive spaces, quotes, and backslashes in Exec.
custom_home=${root}/'home "quote" \slash'
custom_data=${root}/'xdg data'
custom_prefix=${root}/'local prefix "quote" \slash'
mkdir -p -- "${custom_home}" "${custom_data}" "${custom_prefix}"
run_install "${bundle}" "${custom_home}" "${custom_data}" "${custom_prefix}"
custom_service=${custom_data}/dbus-1/services/org.remminakrunner.KRunner.service
grep -Fq 'Exec="' "${custom_service}" || fail "service Exec is not quoted"
expected_exec='Exec="'"${root}"'/local prefix \\"quote\\" \\\\slash/bin/remmina-krunner"'
[[ $(grep '^Exec=' "${custom_service}") == "${expected_exec}" ]] || fail "hostile custom-prefix Exec escaping differs"
[[ -f ${custom_prefix}/bin/remmina-krunner ]] || fail "custom-prefix runner missing"
[[ -f ${custom_prefix}/lib64/plugins/kf6/krunner/kcms/kcm_remmina_krunner.so ]] || fail "custom-prefix plugin missing"

# Exercise the GLib key-file and D-Bus activation parsing layers. The activated
# process must be the executable installed at the hostile path, not any helper
# found through PATH.
activation_marker=${root}/activation-path
HOME=${custom_home} XDG_DATA_HOME=${custom_data} \
    XDG_DATA_DIRS="${custom_data}:/usr/local/share:/usr/share" \
    PACKAGE_HELPER_ACTIVATION_LOG=${activation_marker} \
    TEST_ACTIVATION_MARKER=${activation_marker} \
    TEST_ACTIVATION_BINARY=${custom_prefix}/bin/remmina-krunner \
    timeout 15 dbus-run-session -- /usr/bin/bash -euo pipefail -c '
        activated_pid=
        cleanup_activation() {
            if [[ ${activated_pid:-} =~ ^[0-9]+$ ]] &&
               [[ -e /proc/${activated_pid}/exe ]] &&
               [[ /proc/${activated_pid}/exe -ef "${TEST_ACTIVATION_BINARY}" ]]; then
                kill -TERM "${activated_pid}" 2>/dev/null || true
            fi
        }
        trap cleanup_activation EXIT
        gdbus call --session --dest org.freedesktop.DBus \
            --object-path /org/freedesktop/DBus \
            --method org.freedesktop.DBus.StartServiceByName \
            org.remminakrunner.KRunner 0 >/dev/null
        for _ in {1..100}; do [[ -s ${TEST_ACTIVATION_MARKER} ]] && break; sleep 0.01; done
        [[ -s ${TEST_ACTIVATION_MARKER} ]]
        [[ $(<"${TEST_ACTIVATION_MARKER}") == "${TEST_ACTIVATION_BINARY}" ]]
        reply=$(gdbus call --session --dest org.freedesktop.DBus \
            --object-path /org/freedesktop/DBus \
            --method org.freedesktop.DBus.GetConnectionUnixProcessID \
            org.remminakrunner.KRunner)
        activated_pid=$(sed -n "s/.*uint32 \([0-9][0-9]*\).*/\1/p" <<<"${reply}")
        [[ ${activated_pid} =~ ^[0-9]+$ ]]
        [[ /proc/${activated_pid}/exe -ef "${TEST_ACTIVATION_BINARY}" ]]
    '

# A failed first install restores the complete destination tree, including
# directory components created while preparing the transaction.
first_failure_root=${root}/first-failure-root
mkdir -p -- "${first_failure_root}/home"
first_before=$(snapshot "${first_failure_root}")
first_mv_count=${root}/first-mv-count
if FAKE_MV_FAIL_AT=2 FAKE_MV_COUNT_FILE=${first_mv_count} \
    run_install "${bundle}" "${first_failure_root}/home"; then
    fail "first-install transaction failure succeeded"
fi
first_after=$(snapshot "${first_failure_root}")
[[ ${first_before} == "${first_after}" ]] ||
    fail "first-install rollback did not restore the complete tree"

# Reinstall atomically replaces all four exact owned files and invokes rescan.
printf 'replacement plugin\n' >"${bundle}/lib64/plugins/kf6/krunner/kcms/kcm_remmina_krunner.so"
: >"${calls}"
run_install "${bundle}" "${home}"
grep -Fq replacement "${plugin}" || fail "repeat install did not replace plugin"
grep -Fq '.remmina-krunner.stage.' "${calls}" || fail "runner replacement was not staged beside destination"

# Upgrade and uninstall stop only the exact installed runner executable.
wait_for_identity() {
    local pid=$1 expected_path=$2 actual=
    for _ in {1..100}; do
        actual=$(readlink -- "/proc/${pid}/exe" 2>/dev/null) || actual=
        [[ ${actual} == "${expected_path}" ]] && return 0
        sleep 0.01
    done
    fail "process ${pid} never acquired ${expected_path}"
}
"${binary}" hold &
runner_pid=$!
wait_for_identity "${runner_pid}" "${binary}"
/usr/bin/sleep 300 &
unrelated_pid=$!
run_install "${bundle}" "${home}"
for _ in {1..100}; do kill -0 "${runner_pid}" 2>/dev/null || break; sleep 0.01; done
! kill -0 "${runner_pid}" 2>/dev/null || fail "upgrade did not stop exact runner"
wait "${runner_pid}" 2>/dev/null || true
runner_pid=
kill -0 "${unrelated_pid}" 2>/dev/null || fail "upgrade stopped unrelated process"

# Missing dependencies and injected rename failure occur without partial state.
missing_root=${root}/missing-root
mkdir -p -- "${missing_root}/home"
FAKE_LDD_MISSING=1 assert_failure_without_mutation missing "${missing_root}" \
    run_install "${bundle}" "${missing_root}/home"
rollback_root=${root}/rollback-root
mkdir -p -- "${rollback_root}/home"
run_install "${bundle}" "${rollback_root}/home"
before=$(snapshot "${rollback_root}")
printf 'third plugin\n' >"${bundle}/lib64/plugins/kf6/krunner/kcms/kcm_remmina_krunner.so"
mv_count=${root}/mv-count
if FAKE_MV_FAIL_AT=6 FAKE_MV_COUNT_FILE=${mv_count} run_install "${bundle}" "${rollback_root}/home"; then
    fail "injected transaction failure succeeded"
fi
after=$(snapshot "${rollback_root}")
[[ ${before} == "${after}" ]] || fail "transaction rollback did not restore all files"

# A concurrent installer for this user is rejected while the first owns the lock.
concurrent_root=${root}/concurrent-root
concurrent_ready=${root}/concurrent-ready
concurrent_release=${root}/concurrent-release
mkdir -p -- "${concurrent_root}/home"
PACKAGE_HELPER_BLOCK_READY=${concurrent_ready} \
PACKAGE_HELPER_BLOCK_RELEASE=${concurrent_release} \
    run_install "${bundle}" "${concurrent_root}/home" &
installer_pid=$!
for _ in {1..200}; do [[ -f ${concurrent_ready} ]] && break; sleep 0.01; done
[[ -f ${concurrent_ready} ]] || fail "concurrent install fixture did not acquire the lock"
concurrent_before=$(snapshot "${concurrent_root}")
assert_failure_without_mutation concurrent "${concurrent_root}" \
    run_install "${bundle}" "${concurrent_root}/home"
concurrent_after=$(snapshot "${concurrent_root}")
[[ ${concurrent_before} == "${concurrent_after}" ]] || fail "concurrent install changed destinations"
: >"${concurrent_release}"
wait "${installer_pid}" || fail "lock-owning install failed"
installer_pid=

# Invalid environment and bundle tampering are rejected before mutation.
invalid_root=${root}/invalid-root
mkdir -p -- "${invalid_root}"
assert_failure_without_mutation relative-home "${invalid_root}" env HOME=relative \
    PATH="${fake_bin}:/usr/bin:/bin" /usr/bin/bash "${bundle}/install.sh"
newline_home="${root}/bad"$'\n'path
assert_failure_without_mutation newline-home "${invalid_root}" env HOME="${newline_home}" \
    PATH="${fake_bin}:/usr/bin:/bin" /usr/bin/bash "${bundle}/install.sh"
assert_failure_without_mutation relative-xdg "${invalid_root}" env HOME="${invalid_root}/home" \
    XDG_DATA_HOME=relative PATH="${fake_bin}:/usr/bin:/bin" /usr/bin/bash "${bundle}/install.sh"
assert_failure_without_mutation relative-prefix "${invalid_root}" env HOME="${invalid_root}/home" \
    REMMINA_KRUNNER_INSTALL_PREFIX=relative PATH="${fake_bin}:/usr/bin:/bin" \
    /usr/bin/bash "${bundle}/install.sh"
tampered=${root}/tampered
make_bundle "${tampered}"
printf 'surprise\n' >"${tampered}/unexpected"
assert_failure_without_mutation tampered "${invalid_root}" run_install "${tampered}" "${invalid_root}/home"
tampered_directory=${root}/tampered-directory
make_bundle "${tampered_directory}"
mkdir -- "${tampered_directory}/unexpected-empty-directory"
assert_failure_without_mutation tampered-directory "${invalid_root}" \
    run_install "${tampered_directory}" "${invalid_root}/home"
tampered_symlink=${root}/tampered-symlink
make_bundle "${tampered_symlink}"
ln -s LICENSE "${tampered_symlink}/license-link"
assert_failure_without_mutation tampered-symlink "${invalid_root}" \
    run_install "${tampered_symlink}" "${invalid_root}/home"
tampered_mode=${root}/tampered-mode
make_bundle "${tampered_mode}"
chmod 0700 "${tampered_mode}/bin/remmina-krunner"
assert_failure_without_mutation tampered-mode "${invalid_root}" \
    run_install "${tampered_mode}" "${invalid_root}/home"

if grep -Eq '(^|[[:space:]])(sudo|dnf|apt|pacman|zypper)([[:space:]]|$)' \
    "${installer}" "${uninstaller}"; then
    fail "package scripts must not elevate or invoke a package manager"
fi

# Idempotent uninstall removes only owned runtime files and preserves config/unrelated files.
mkdir -p -- "${home}/.config" "${home}/.local/bin"
printf 'selected=native\n' >"${home}/.config/remmina-krunnerrc"
printf 'keep\n' >"${home}/.local/bin/unrelated"
"${binary}" hold &
runner_pid=$!
wait_for_identity "${runner_pid}" "${binary}"
run_uninstall "${bundle}" "${home}"
for _ in {1..100}; do kill -0 "${runner_pid}" 2>/dev/null || break; sleep 0.01; done
! kill -0 "${runner_pid}" 2>/dev/null || fail "uninstall did not stop exact runner"
wait "${runner_pid}" 2>/dev/null || true
runner_pid=
run_uninstall "${bundle}" "${home}"
[[ ! -e ${binary} && ! -e ${plugin} && ! -e ${desktop} && ! -e ${service} ]] || fail "uninstall left owned files"
[[ -f ${home}/.config/remmina-krunnerrc ]] || fail "uninstall removed selection config"
[[ -f ${home}/.local/bin/unrelated ]] || fail "uninstall removed unrelated file"
kill -0 "${unrelated_pid}" 2>/dev/null || fail "uninstall stopped unrelated process"
kill "${unrelated_pid}" 2>/dev/null || true
wait "${unrelated_pid}" 2>/dev/null || true

printf 'package_scripts: all checks passed\n'
