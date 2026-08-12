# Runtime Integration Fixes Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make the user-local KRunner configuration module discoverable and make retained profile results launch reliably after KRunner teardown.

**Architecture:** The installer will stage desktop metadata containing the exact installed KCM path, avoiding global Qt plugin-path changes. The D-Bus service will treat the repository's strict opaque-ID format as input and delegate authorization to the catalog's current-instance and freshness validation instead of retaining match-session IDs.

**Tech Stack:** C++20, Qt 6 Core/DBus/Test, KDE Frameworks 6 KCMUtils/Config, CMake, Bash, private D-Bus test sessions, Podman.

---

### Task 1: Preserve valid profile activation across teardown

**Files:**
- Modify: `tests/test_runner_service.cpp`
- Modify: `src/dbus/runner_service.h`
- Modify: `src/dbus/runner_service.cpp`

**Step 1: Write the failing tests**

Change the lifecycle test to require a valid 64-character lowercase hexadecimal
profile ID to remain actionable after `Teardown` and idle session cleanup. Add
rows proving malformed identifiers do not reach the launcher. Retain catalog
tests proving unknown, stale, and cross-instance identifiers fail resolution.

**Step 2: Run the focused test to verify RED**

Run: `./scripts/container.sh test '^runner_service$'`

Expected: FAIL because `Teardown` clears `offeredProfileIds_`.

**Step 3: Implement the minimal service change**

Remove `offeredProfileIds_`. Add a local predicate accepting exactly 64 ASCII
characters in `[0-9a-f]`. In `Run`, pass only IDs satisfying that predicate to
`RemminaLaunchSource::connect`; retain the existing action/error handling and
launcher/catalog validation.

**Step 4: Run the focused test to verify GREEN**

Run: `./scripts/container.sh test '^runner_service$'`

Expected: PASS.

### Task 2: Install discoverable KCM metadata

**Files:**
- Modify: `tests/test_package_scripts.sh`
- Modify: `packaging/install.sh`

**Step 1: Write the failing package regression**

Extend the hostile-prefix package test to require exactly one installed
`X-KDE-ConfigModule` entry whose decoded value is the exact installed KCM path.
Verify the staged source metadata remains relative and the installed metadata
does not depend on `QT_PLUGIN_PATH`.

**Step 2: Run the focused test to verify RED**

Run: `./scripts/container.sh test '^package_scripts$'`

Expected: FAIL because the installed metadata still contains the relative KDE
plugin namespace.

**Step 3: Implement transactional desktop transformation**

Stage the desktop destination as an empty regular file, copy all non-KCM lines,
and replace the single KCM entry with the escaped absolute `plugin_path`.
Validate that source metadata contains exactly one KCM entry before beginning
destination replacement. Keep the existing four-path rollback transaction.

**Step 4: Run the focused test to verify GREEN**

Run: `./scripts/container.sh test '^package_scripts$'`

Expected: PASS.

### Task 3: Prove the real KDE loader and D-Bus launch boundary

**Files:**
- Modify: `tests/CMakeLists.txt`
- Modify: `tests/test_kcm.cpp`
- Modify: `tests/test_dbus_activation.sh`

**Step 1: Add failing integration assertions**

Use KF6's plugin model in the KCM test to load metadata whose
`X-KDE-ConfigModule` is an absolute KCM path and require valid configuration
metadata. Extend the isolated D-Bus activation test with a synthetic Remmina
profile and a fake Remmina executable that records only its fixed argument
contract; require `Match`, `Teardown`, `Run` to produce `--connect` and the
isolated profile path.

**Step 2: Run both integration tests**

Run: `./scripts/container.sh test '^(kcm|activation)$'`

Expected: the activation case is RED before Task 1 and the installed-metadata
case is RED before Task 2; both are GREEN after their implementations.

**Step 3: Run the complete verification gates**

Run: `./scripts/container.sh check`

Expected: all tests and exact staged inventory pass.

Run: `./scripts/container.sh sanitize`

Expected: all tests pass under ASan/UBSan with no diagnostics.

Run: `./scripts/container.sh source-bundle`

Expected: Release build succeeds and the exact source bundle is staged under
`build-source-bundle/remmina-krunner`.

**Step 4: Review and commit**

Run `git diff --check`, review the scoped diff for profile-data disclosure, and
commit the implementation without pushing or installing it outside the
workspace.
