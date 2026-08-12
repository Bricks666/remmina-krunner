#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
# SPDX-License-Identifier: 0BSD
set -euo pipefail

installer=${1:?installer path required}
uninstaller=${2:?uninstaller path required}
runner=${3:?runner path required}
plugin_relative=${4:?configured plugin-relative path required}
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
for tool in find install mktemp readlink rm sleep sort stat; do
    ln -s "/usr/bin/${tool}" "${fake_bin}/${tool}"
done
cat >"${fake_bin}/mkdir" <<'EOF'
#!/usr/bin/bash
if [[ -n ${FAKE_MKDIR_RACE_PATH:-} && ! -e ${FAKE_MKDIR_RACE_MARKER:?} ]]; then
    for argument in "$@"; do
        if [[ ${argument} == "${FAKE_MKDIR_RACE_PATH}" ||
              ${argument} == "${FAKE_MKDIR_RACE_PATH}"/* ]]; then
            /usr/bin/mkdir -m 0700 -- "${FAKE_MKDIR_RACE_PATH}"
            printf 'created externally\n' >"${FAKE_MKDIR_RACE_MARKER}"
            break
        fi
    done
fi
exec /usr/bin/mkdir "$@"
EOF
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
destination=${@: -1}
destination_name=${destination##*/}
if [[ ${FAKE_MV_SIGNAL_BEFORE_BACKUP:-0} == 1 &&
      ${destination_name} == *.backup.* ]]; then
    kill -TERM "${PPID}"
    exit 143
fi
if [[ ${FAKE_MV_SIGNAL_AFTER_REPLACEMENT:-0} == 1 &&
      ${destination_name} == remmina-krunner ]]; then
    /usr/bin/mv "$@" || exit $?
    kill -TERM "${PPID}"
    exit 143
fi
count=0
[[ ! -f ${FAKE_MV_COUNT_FILE:-/nonexistent} ]] || read -r count <"${FAKE_MV_COUNT_FILE}"
count=$((count + 1))
[[ -z ${FAKE_MV_COUNT_FILE:-} ]] || printf '%s\n' "${count}" >"${FAKE_MV_COUNT_FILE}"
[[ ${FAKE_MV_FAIL_AT:-0} != "${count}" ]] || exit 73
exec /usr/bin/mv "$@"
EOF
chmod 0755 "${fake_bin}"/{uname,ldd,kbuildsycoca6,kquitapp6,mkdir,mv}

make_bundle() {
    local bundle=$1 payload=${2:-${runner}}
    mkdir -p -- "${bundle}/bin" \
        "${bundle}/${plugin_relative%/*}" \
        "${bundle}/share/dbus-1/services" \
        "${bundle}/share/krunner/dbusplugins" "${bundle}/LICENSES"
    cp -- "${payload}" "${bundle}/bin/remmina-krunner"
    printf 'plugin\n' >"${bundle}/${plugin_relative}"
    printf '[Desktop Entry]\nType=Service\n' >"${bundle}/share/krunner/dbusplugins/org.remminakrunner.KRunner.desktop"
    printf '[D-BUS Service]\nName=org.remminakrunner.KRunner\nExec="/staged/bin/remmina-krunner"\n' \
        >"${bundle}/share/dbus-1/services/org.remminakrunner.KRunner.service"
    printf 'license\n' >"${bundle}/LICENSE"
    printf '0bsd\n' >"${bundle}/LICENSES/0BSD.txt"
    printf 'lgpl\n' >"${bundle}/LICENSES/LGPL-2.0-or-later.txt"
    cp -- "${installer}" "${bundle}/install.sh"
    cp -- "${uninstaller}" "${bundle}/uninstall.sh"
    chmod 0755 "${bundle}/bin/remmina-krunner" "${bundle}/install.sh" "${bundle}/uninstall.sh"
    chmod 0755 "${bundle}/${plugin_relative}"
    chmod 0644 "${bundle}/share/dbus-1/services/org.remminakrunner.KRunner.service" \
        "${bundle}/share/krunner/dbusplugins/org.remminakrunner.KRunner.desktop" \
        "${bundle}/LICENSE" "${bundle}/LICENSES/0BSD.txt" "${bundle}/LICENSES/LGPL-2.0-or-later.txt"
}
run_install() {
    local bundle=$1 home=$2 data=${3:-} prefix=${4:-}
    local os_release_file=${5-${REMMINA_KRUNNER_OS_RELEASE_FILE:-${supported_os_release_file:-/etc/os-release}}}
    HOME=${home} XDG_CONFIG_HOME="${home}/.config" XDG_DATA_HOME=${data} \
        REMMINA_KRUNNER_INSTALL_PREFIX=${prefix} FAKE_CALL_LOG=${calls} \
        REMMINA_KRUNNER_OS_RELEASE_FILE=${os_release_file} \
        PACKAGE_HELPER_LOG=${helper_log} \
        PACKAGE_HELPER_BLOCK_READY=${PACKAGE_HELPER_BLOCK_READY:-} \
        PACKAGE_HELPER_BLOCK_RELEASE=${PACKAGE_HELPER_BLOCK_RELEASE:-} \
        FAKE_LDD_MISSING=${FAKE_LDD_MISSING:-0} FAKE_MV_FAIL_AT=${FAKE_MV_FAIL_AT:-0} \
        FAKE_MV_COUNT_FILE=${FAKE_MV_COUNT_FILE:-} \
        FAKE_MV_SIGNAL_BEFORE_BACKUP=${FAKE_MV_SIGNAL_BEFORE_BACKUP:-0} \
        FAKE_MV_SIGNAL_AFTER_REPLACEMENT=${FAKE_MV_SIGNAL_AFTER_REPLACEMENT:-0} \
        FAKE_MKDIR_RACE_PATH=${FAKE_MKDIR_RACE_PATH:-} \
        FAKE_MKDIR_RACE_MARKER=${FAKE_MKDIR_RACE_MARKER:-} \
        FAKE_UNAME_S=${FAKE_UNAME_S:-Linux} FAKE_UNAME_M=${FAKE_UNAME_M:-x86_64} \
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

# Rollback relies on rmdir to remove only directories created by this
# transaction.  Exercise preflight with an isolated PATH that provides every
# other declared dependency and cannot fall through to the host's rmdir.
no_rmdir_bin=${root}/no-rmdir-bin
mkdir -p -- "${no_rmdir_bin}"
for tool in find install ldd mkdir mktemp mv readlink rm sleep sort stat uname kbuildsycoca6; do
    ln -s -- "${fake_bin}/${tool}" "${no_rmdir_bin}/${tool}"
done
for tool in flock grep id; do
    ln -s -- "/usr/bin/${tool}" "${no_rmdir_bin}/${tool}"
done
if (PATH="${no_rmdir_bin}"; command -v rmdir >/dev/null 2>&1); then
    fail "isolated missing-rmdir PATH unexpectedly resolves rmdir"
fi
missing_rmdir_home=${root}/missing-rmdir-home
mkdir -p -- "${missing_rmdir_home}"
missing_rmdir_before=$(snapshot "${missing_rmdir_home}")
if HOME=${missing_rmdir_home} XDG_CONFIG_HOME=${missing_rmdir_home}/.config \
    FAKE_CALL_LOG=${calls} PACKAGE_HELPER_LOG=${helper_log} \
    PATH="${no_rmdir_bin}" /usr/bin/bash "${bundle}/install.sh" \
    >"${root}/missing-rmdir.out" 2>"${root}/missing-rmdir.err"; then
    fail "missing rmdir dependency unexpectedly succeeded"
fi
missing_rmdir_after=$(snapshot "${missing_rmdir_home}")
[[ ${missing_rmdir_before} == "${missing_rmdir_after}" ]] ||
    fail "missing rmdir dependency mutated destinations"
grep -Fq 'Required command is unavailable: rmdir' "${root}/missing-rmdir.err" ||
    fail "missing rmdir dependency was not rejected by preflight"

# The Fedora-built binary bundle is accepted only on Fedora 44 x86_64. The
# os-release override is test-only and each rejection must precede mutation.
platform_files=${root}/platform-files
mkdir -p -- "${platform_files}"
printf 'ID=fedora\nVERSION_ID=44\nNAME=Fedora Linux\n' >"${platform_files}/fedora-44"
printf 'VERSION_ID="44"\nID="fedora"\nNAME="Fedora Linux"\n' >"${platform_files}/fedora-44-quoted"
printf 'ID=ubuntu\nVERSION_ID="24.04"\n' >"${platform_files}/ubuntu"
printf 'ID=debian\nVERSION_ID=13\n' >"${platform_files}/debian"
printf 'ID=fedora\nVERSION_ID=45\n' >"${platform_files}/fedora-45"
printf 'ID =fedora\nVERSION_ID=44\n' >"${platform_files}/malformed"
printf 'ID=fedora\nID="fedora"\nVERSION_ID=44\n' >"${platform_files}/duplicate"
ln -s -- "${platform_files}/fedora-44" "${platform_files}/symlink"
mkdir -- "${platform_files}/directory"
supported_os_release_file=${platform_files}/fedora-44

for platform_name in fedora-44 fedora-44-quoted; do
    platform_pass_root=${root}/platform-pass-${platform_name}
    mkdir -p -- "${platform_pass_root}/home"
    REMMINA_KRUNNER_OS_RELEASE_FILE=${platform_files}/${platform_name} \
        run_install "${bundle}" "${platform_pass_root}/home"
    [[ -f ${platform_pass_root}/home/.local/bin/remmina-krunner ]] ||
        fail "${platform_name}: supported platform was not installed"
done
platform_default_root=${root}/platform-pass-system-default
mkdir -p -- "${platform_default_root}/home"
run_install "${bundle}" "${platform_default_root}/home" '' '' ''
[[ -f ${platform_default_root}/home/.local/bin/remmina-krunner ]] ||
    fail "canonical Fedora system os-release was not installed"

for platform_name in ubuntu debian fedora-45 malformed duplicate symlink directory missing; do
    platform_failure_root=${root}/platform-failure-${platform_name}
    mkdir -p -- "${platform_failure_root}/home" "${platform_failure_root}/runtime"
    XDG_RUNTIME_DIR=${platform_failure_root}/runtime \
        REMMINA_KRUNNER_OS_RELEASE_FILE=${platform_files}/${platform_name} \
        assert_failure_without_mutation "platform-${platform_name}" \
        "${platform_failure_root}" run_install "${bundle}" "${platform_failure_root}/home"
done
relative_platform_root=${root}/platform-failure-relative
mkdir -p -- "${relative_platform_root}/home" "${relative_platform_root}/runtime"
XDG_RUNTIME_DIR=${relative_platform_root}/runtime REMMINA_KRUNNER_OS_RELEASE_FILE=relative \
    assert_failure_without_mutation platform-relative "${relative_platform_root}" \
    run_install "${bundle}" "${relative_platform_root}/home"
aarch64_root=${root}/platform-failure-aarch64
mkdir -p -- "${aarch64_root}/home" "${aarch64_root}/runtime"
XDG_RUNTIME_DIR=${aarch64_root}/runtime FAKE_UNAME_M=aarch64 \
    REMMINA_KRUNNER_OS_RELEASE_FILE=${platform_files}/fedora-44 \
    assert_failure_without_mutation platform-aarch64 "${aarch64_root}" \
    run_install "${bundle}" "${aarch64_root}/home"
: >"${helper_log}"

run_install "${bundle}" "${home}"
binary=${home}/.local/bin/remmina-krunner
plugin=${home}/.local/${plugin_relative}
desktop=${home}/.local/share/krunner/dbusplugins/org.remminakrunner.KRunner.desktop
service=${home}/.local/share/dbus-1/services/org.remminakrunner.KRunner.service
assert_mode "${binary}" 755
assert_mode "${plugin}" 755
assert_mode "${desktop}" 644
assert_mode "${service}" 644
mapfile -t files < <(find "${home}" -type f -printf '%P\n' | LC_ALL=C sort)
expected=(.local/bin/remmina-krunner ".local/${plugin_relative}" .local/share/dbus-1/services/org.remminakrunner.KRunner.service .local/share/krunner/dbusplugins/org.remminakrunner.KRunner.desktop)
mapfile -t expected < <(printf '%s\n' "${expected[@]}" | LC_ALL=C sort)
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
[[ -f ${custom_prefix}/${plugin_relative} ]] || fail "custom-prefix plugin missing"

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

# A directory created by another actor after the absence check is not owned by
# this transaction and must survive rollback with its original mode.
mkdir_race_root=${root}/mkdir-race-root
mkdir_race_home=${mkdir_race_root}/home
mkdir_race_path=${mkdir_race_home}/.local
mkdir_race_marker=${root}/mkdir-race-marker
mkdir_race_mv_count=${root}/mkdir-race-mv-count
mkdir -p -- "${mkdir_race_home}"
if FAKE_MKDIR_RACE_PATH=${mkdir_race_path} \
    FAKE_MKDIR_RACE_MARKER=${mkdir_race_marker} \
    FAKE_MV_FAIL_AT=2 FAKE_MV_COUNT_FILE=${mkdir_race_mv_count} \
    run_install "${bundle}" "${mkdir_race_home}"; then
    fail "mkdir-race transaction failure succeeded"
fi
[[ -f ${mkdir_race_marker} ]] || fail "mkdir race was not exercised"
[[ -d ${mkdir_race_path} && ! -L ${mkdir_race_path} ]] ||
    fail "rollback removed a directory created by another actor"
[[ $(stat -c '%a' -- "${mkdir_race_path}") == 700 ]] ||
    fail "installer changed the external directory mode"
find "${mkdir_race_path}" -mindepth 1 -print -quit | grep -q . &&
    fail "rollback left installer-owned entries in the external directory"

# Reinstall atomically replaces all four exact owned files and invokes rescan.
printf 'replacement plugin\n' >"${bundle}/${plugin_relative}"
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
printf 'third plugin\n' >"${bundle}/${plugin_relative}"
mv_count=${root}/mv-count
if FAKE_MV_FAIL_AT=6 FAKE_MV_COUNT_FILE=${mv_count} run_install "${bundle}" "${rollback_root}/home"; then
    fail "injected transaction failure succeeded"
fi
after=$(snapshot "${rollback_root}")
[[ ${before} == "${after}" ]] || fail "transaction rollback did not restore all files"

# TERM between backup reservation and moving an existing destination must not
# replace the original with the empty reservation file.
signal_backup_root=${root}/signal-backup-root
signal_backup_home=${signal_backup_root}/home
mkdir -p -- "${signal_backup_home}"
run_install "${bundle}" "${signal_backup_home}"
signal_backup_before=$(snapshot "${signal_backup_root}")
if FAKE_MV_SIGNAL_BEFORE_BACKUP=1 \
    run_install "${bundle}" "${signal_backup_home}"; then
    fail "backup-window TERM unexpectedly succeeded"
fi
signal_backup_after=$(snapshot "${signal_backup_root}")
[[ ${signal_backup_before} == "${signal_backup_after}" ]] ||
    fail "backup-window TERM did not preserve the complete original tree"

# TERM after stage->destination rename but before bookkeeping must infer that
# the first-install replacement completed and remove it during rollback.
signal_replacement_root=${root}/signal-replacement-root
signal_replacement_home=${signal_replacement_root}/home
mkdir -p -- "${signal_replacement_home}"
signal_replacement_before=$(snapshot "${signal_replacement_root}")
if FAKE_MV_SIGNAL_AFTER_REPLACEMENT=1 \
    run_install "${bundle}" "${signal_replacement_home}"; then
    fail "replacement-window TERM unexpectedly succeeded"
fi
signal_replacement_after=$(snapshot "${signal_replacement_root}")
[[ ${signal_replacement_before} == "${signal_replacement_after}" ]] ||
    fail "replacement-window TERM left a partial first installation"

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
FAKE_UNAME_M=aarch64 REMMINA_KRUNNER_OS_RELEASE_FILE=${platform_files}/fedora-45 \
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
