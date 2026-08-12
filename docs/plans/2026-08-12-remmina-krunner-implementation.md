<!-- SPDX-FileCopyrightText: 2026 Remmina KRunner contributors -->
<!-- SPDX-License-Identifier: 0BSD -->

# Remmina KRunner Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build a Plasma 6 KRunner service that searches one selected native,
Flatpak, or Snap Remmina installation by connection name, server, and labels,
and opens connections or Remmina's profile-creation window.

**Architecture:** A C++20/Qt 6 DBus2 runner and a KDE KCModule link a shared
static core. The core discovers installed Remmina instances, persists and
repairs one selection, reads only allowlisted metadata from that instance's
profiles, caches one freshness-aware search snapshot, and launches the exact
selected application without a shell. All builds and automated tests run
inside one digest-pinned Fedora 44 Podman image.

**Tech Stack:** C++20, Qt 6 Core/DBus/Widgets/Test, KDE Frameworks 6
CoreAddons/Config/KCMUtils/I18n/Notifications, CMake/ECM, QtTest/CTest, Bash,
Podman, GitHub Actions.

---

Implementation must follow `superpowers:test-driven-development`: add one
focused failing test, run it through `scripts/container.sh` and observe the
intended failure, add the minimum implementation, rerun the focused test, then
run the relevant wider suite. Do not compile or run tests directly on the
host.

The repository starts with only the approved design commit. Infrastructure
files in Task 1 are the unavoidable test-harness bootstrap; no runtime feature
code is introduced until the Podman-hosted smoke test is working.

## Task 1: Bootstrap the Podman-only build and test harness

**Files:**

- Create: `CMakeLists.txt`
- Create: `tests/CMakeLists.txt`
- Create: `tests/test_smoke.cpp`
- Create: `containers/Containerfile`
- Create: `scripts/container.sh`
- Create: `scripts/ci.sh`
- Create: `.gitignore`
- Create: `.clang-format`
- Create: `LICENSE`
- Create: `LICENSES/0BSD.txt`
- Create: `LICENSES/LGPL-2.0-or-later.txt`

**Step 1: Add the container entry point and demonstrate the missing build**

Create a digest-pinned Fedora 44 `containers/Containerfile` based on the Ente
Auth runner. Install Bash, CMake, Ninja or Make, GCC C++, Git, ECM, Qt 6 Core,
DBus, Widgets and Test development packages, KF6 CoreAddons, Config, KCMUtils,
I18n, Notifications and Runner development packages, `dbus-daemon`,
sanitizers, GNU archive tools, and pinned `actionlint`.

Create `scripts/container.sh` with these public modes:

```text
build
configure
test [ctest-regex]
check
sanitize
release-build
```

The wrapper must resolve the repository root, build a fixed local image name,
and use `podman run --rm --userns=keep-id` with only the repository mounted at
`/workspace:Z`. It must never mount the host home, session bus, runtime
directory, Remmina data, or container socket. `scripts/ci.sh` must reject
execution unless `REMMINA_KRUNNER_CONTAINER=1` is present.

Run:

```bash
./scripts/container.sh check
```

Expected: FAIL because the repository has no CMake project yet. This is the
observed red state for the harness bootstrap.

**Step 2: Add the smallest CMake project and smoke test**

Use this initial project shape:

```cmake
cmake_minimum_required(VERSION 3.25)
project(remmina-krunner VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

include(CTest)
find_package(Qt6 6.7 REQUIRED COMPONENTS Core)
if(BUILD_TESTING)
    find_package(Qt6 6.7 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
```

`tests/test_smoke.cpp` should instantiate `QCoreApplication` through
`QTEST_APPLESS_MAIN` and assert `true`. Register it with CTest.

Add SPDX headers, the canonical 0BSD and LGPL-2.0-or-later texts, ignored
`build-*` directories, and the Ente project's C++ formatting baseline.

**Step 3: Verify the green harness**

Run:

```bash
./scripts/container.sh check
./scripts/container.sh sanitize
```

Expected: PASS; both modes configure, build, and run `smoke` inside Podman.

**Step 4: Commit**

```bash
git add CMakeLists.txt tests containers scripts .gitignore .clang-format LICENSE LICENSES
git commit -m "build: add Podman test harness"
```

## Task 2: Parse runner queries and rank profile metadata

**Files:**

- Create: `src/core/profile_record.h`
- Create: `src/core/matcher.h`
- Create: `src/core/matcher.cpp`
- Create: `tests/test_matcher.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write failing query tests**

Define the intended API in the test:

```cpp
enum class QueryKind { Ignore, Create, Lookup };

struct ParsedQuery {
    QueryKind kind;
    QStringList tokens;
};

ParsedQuery parseRunnerQuery(QStringView query);
```

Cover:

- empty/unrelated queries;
- `rem` and whitespace-only suffix returning `Ignore`;
- case-insensitive exact `rem new` returning `Create`;
- `rem new york` returning lookup tokens `new`, `york`;
- Unicode whitespace and case folding;
- rejection of `remote`, `rem:host`, and other non-standalone triggers.

Run:

```bash
./scripts/container.sh test matcher
```

Expected: FAIL because matcher symbols do not exist.

**Step 2: Add failing ranking tests**

Use visible-only records:

```cpp
struct ProfileRecord {
    QString opaqueId;
    QString sourcePath;
    QString launchPath;
    QString name;
    QString server;
    QStringList labels;
    QString labelsDisplay;
    QString protocol;
};

struct SearchMatch {
    ProfileRecord record;
    double relevance;
};

QList<SearchMatch> matchProfiles(const QList<ProfileRecord> &profiles,
                                 const QStringList &tokens);
```

Test name, IPv4, bracketed IPv6, hostname/domain, server-with-port, and label
matches. Test that protocol, username-like text not present in visible fields,
source filename, and opaque ID do not match. Test tokens split across fields,
exact/prefix/substring ordering, weakest-token scoring, stable folded
name/server/path ties, and nonmutation of input.

**Step 3: Implement query parsing and matching**

Implement standalone trigger parsing and case-fold tokens. For each token,
take the best relationship across folded name, folded server, and each folded
label:

```cpp
exact     -> 1.00
prefix    -> 0.90
substring -> 0.75
missing   -> reject profile
```

Overall relevance is the weakest token score. Empty lookup tokens are never
passed to the matcher because `rem` is `Ignore`.

**Step 4: Verify and commit**

```bash
./scripts/container.sh test matcher
./scripts/container.sh check
git add src/core tests CMakeLists.txt
git commit -m "feat(search): match Remmina profiles"
```

Expected: all matcher and smoke tests pass.

## Task 3: Model instances and implement selection policy

**Files:**

- Create: `src/core/remmina_instance.h`
- Create: `src/core/selection_policy.h`
- Create: `src/core/selection_policy.cpp`
- Create: `tests/test_selection_policy.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write failing instance-order tests**

Use this model:

```cpp
enum class InstanceKind { Native, Flatpak, Snap };

struct ProfileEnvironment {
    QString configHome;
    QString dataHome;
    QString legacyHome;
    QStringList systemDataHomes;
};

struct RemminaInstance {
    QString id;
    InstanceKind kind;
    QString displayName;
    QString executable;
    QStringList launcherPrefix;
    ProfileEnvironment profiles;
};

struct SelectionDecision {
    QString selectedId;
    bool changed;
};

SelectionDecision validateSelection(const QList<RemminaInstance> &instances,
                                    QStringView savedId);
```

Test native-before-Flatpak-before-Snap ordering, preservation of a valid
manual selection, fallback after removal, first-install default, empty scan,
and deterministic order within each type. Ensure version/revision strings are
not required in stable IDs.

Run `./scripts/container.sh test selection_policy` and observe missing-symbol
failure.

**Step 2: Implement the minimal policy**

Keep the input scanner order authoritative after it has grouped types. Return
`changed=false` only when the saved ID exists unchanged. Return an empty ID
when no instances exist.

**Step 3: Verify and commit**

```bash
./scripts/container.sh test selection_policy
./scripts/container.sh check
git add src/core tests CMakeLists.txt
git commit -m "feat(config): select Remmina instance"
```

## Task 4: Discover native, Flatpak, and Snap instances

**Files:**

- Create: `src/platform/process_probe.h`
- Create: `src/platform/qt_process_probe.h`
- Create: `src/platform/qt_process_probe.cpp`
- Create: `src/core/instance_scanner.h`
- Create: `src/core/instance_scanner.cpp`
- Create: `tests/fakes.h`
- Create: `tests/test_instance_scanner.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write failing native discovery tests**

Inject all host-sensitive paths:

```cpp
struct ScanEnvironment {
    QStringList pathEntries;
    QString flatpakExecutable;
    QString snapLauncher;
    QString userHome;
    QString snapMountRoot = QStringLiteral("/snap");
};

struct InstanceScanResult {
    QList<RemminaInstance> instances;
    QStringList failedBackends;
};

class InstanceScanner {
public:
    InstanceScanner(ProcessProbe &probe, ScanEnvironment environment);
    InstanceScanResult scan() const;
};
```

Use `QTemporaryDir` to create executable `remmina` fixtures. Test PATH order,
non-executable rejection, canonical deduplication while preserving the first
clean absolute lexical launcher, stable identity across symlink retargets,
paths with spaces, and exclusion when the canonical path belongs to the
injected Snap mount root.

**Step 2: Write failing Flatpak and Snap tests**

The fake `ProcessProbe` returns bounded synthetic stdout for:

```text
flatpak list --app --columns=application,ref,installation
```

Test exact app-ID filtering, user plus multiple named system installations,
malformed lines, duplicate refs, deterministic order, command timeout, and
Flatpak failure alongside successful native results.

Test an executable Snap launcher, missing launcher, canonical launcher, and a
Snap failure that does not remove other results. Stable IDs must omit native
symlink targets, Flatpak commits, and Snap revisions.

Run `./scripts/container.sh test instance_scanner`; expect failure.

**Step 3: Implement bounded probing and scanner logic**

`ProcessProbe` returns:

```cpp
struct ProbeResult {
    enum class Status { Success, Failed, TimedOut, OutputTooLarge } status;
    QByteArray standardOutput;
};
```

`QtProcessProbe` uses `QProcess`, a fixed timeout, a fixed maximum output size,
discarded stderr, and no shell. It runs only on the `QCoreApplication` thread
before shutdown; the main event loop must resume afterward so deferred reaping
can complete in the pathological case where a killed child exceeds the bounded
reaping grace period. Build profile environments as follows:

- native: current host XDG homes;
- Flatpak: `~/.var/app/org.remmina.Remmina/config` and `data`;
- Snap: `~/snap/remmina/current/.config` and `.local/share`.

Create launch prefixes that bind Flatpak to the exact installation. Do not run
Remmina itself during discovery.

**Step 4: Verify and commit**

```bash
./scripts/container.sh test instance_scanner
./scripts/container.sh check
./scripts/container.sh sanitize
git add src tests CMakeLists.txt
git commit -m "feat(discovery): find Remmina instances"
```

## Task 5: Persist and repair the selected instance

**Files:**

- Create: `src/core/selection_store.h`
- Create: `src/core/kconfig_selection_store.h`
- Create: `src/core/kconfig_selection_store.cpp`
- Create: `src/core/instance_registry.h`
- Create: `src/core/instance_registry.cpp`
- Create: `tests/test_selection_store.cpp`
- Create: `tests/test_instance_registry.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write failing store tests**

Define:

```cpp
class SelectionStore {
public:
    virtual ~SelectionStore() = default;
    virtual QString selectedId() const = 0;
    virtual bool writeSelectedId(QStringView id) = 0;
};
```

Test `KConfigSelectionStore` against a configuration file in
`QTemporaryDir`. Cover missing config, Unicode/path-like stable IDs, overwrite,
empty selection, sync failure, and preservation of unrelated keys.

**Step 2: Write failing registry lifecycle tests**

Define:

```cpp
struct RegistrySnapshot {
    QList<RemminaInstance> instances;
    QString selectedId;
    QStringList failedBackends;
};

class InstanceRegistry {
public:
    RegistrySnapshot rescanAndRepair();
    RegistrySnapshot snapshot() const;
    bool select(QStringView id);
};
```

Use fake scanner/store boundaries. Test first scan persistence, valid selection
stickiness, fallback persistence, clear-on-empty, rejected unknown manual ID,
and partial scanner errors.

**Step 3: Implement store and registry**

Store `selectedInstance` under `[General]` in `remmina-krunnerrc`. Automatic
repair writes immediately. Manual `select()` accepts only an ID present in the
latest snapshot.

**Step 4: Verify and commit**

```bash
./scripts/container.sh test 'selection_store|instance_registry'
./scripts/container.sh check
git add src/core tests CMakeLists.txt
git commit -m "feat(config): persist instance selection"
```

## Task 6: Parse allowlisted Remmina key-file metadata

**Files:**

- Create: `src/core/key_file_reader.h`
- Create: `src/core/key_file_reader.cpp`
- Create: `src/core/profile_parser.h`
- Create: `src/core/profile_parser.cpp`
- Create: `tests/fixtures/valid.remmina`
- Create: `tests/fixtures/escaped.remmina`
- Create: `tests/fixtures/malformed.remmina`
- Create: `tests/test_key_file_reader.cpp`
- Create: `tests/test_profile_parser.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write failing key-file tests**

The reader API is deliberately allowlisted:

```cpp
using AllowedValues = QHash<QString, QString>;

std::optional<AllowedValues> readAllowedKeyFileValues(
    const QString &path,
    QStringView section,
    const QSet<QString> &allowedKeys);
```

Test sections, comments, blank lines, first `=` splitting, CRLF, UTF-8,
GLib-style `\\s`, `\\n`, `\\t`, `\\r`, and `\\\\` escapes, duplicate keys,
unknown escape rejection, oversized lines, wrong section, unreadable file, and
invalid UTF-8.

Include synthetic `password`, gateway, username, and notes values in fixtures.
Assert they are absent from the returned map and all error strings.

**Step 2: Write failing profile parser tests**

Define:

```cpp
enum class ProfileParseError { Unreadable, Malformed, MissingName };

std::variant<ProfileRecord, ProfileParseError> parseRemminaProfile(
    const QString &sourcePath,
    const QString &launchPath,
    QString opaqueId);
```

Test name/server/labels/protocol extraction, trimmed comma labels, preserved
label display, missing optional values, missing/empty name, wrong group, and
proof that sensitive fixture strings are not retained in the record.

**Step 3: Implement streaming parsing**

Use `QFile::readLine` with explicit size limits. Do not load the entire profile
into memory. Decode only values whose keys are in the allowlist, and wipe the
temporary line buffer after inspecting an unrecognized key/value line.

**Step 4: Verify and commit**

```bash
./scripts/container.sh test 'key_file_reader|profile_parser'
./scripts/container.sh check
./scripts/container.sh sanitize
git add src/core tests CMakeLists.txt
git commit -m "feat(profiles): parse searchable metadata"
```

## Task 7: Resolve profile directories and load snapshots

**Files:**

- Create: `src/core/profile_locator.h`
- Create: `src/core/profile_locator.cpp`
- Create: `src/core/profile_repository.h`
- Create: `src/core/profile_repository.cpp`
- Create: `tests/test_profile_locator.cpp`
- Create: `tests/test_profile_repository.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write failing location-precedence tests**

Define:

```cpp
struct LocatedProfileDirectory {
    QString hostPath;
    QString launchPath;
};

std::optional<LocatedProfileDirectory> locateProfileDirectory(
    const RemminaInstance &instance);
```

Build temporary native, Flatpak, and Snap homes. Test valid custom
`datadir_path`, nonexistent custom fallback, legacy fallback, XDG data
fallback, applicable native system-data fallback, no directory, relative and
escaped preference values, host-equal Flatpak/Snap launch paths, component-safe
sandbox roots, Snap current/active-revision/common custom paths, rejection of
other revisions and external custom sandbox paths, and bounded symlink
resolution.

**Step 2: Write failing repository tests**

Define snapshot data:

```cpp
struct FileFingerprint {
    QString path;
    qint64 size;
    qint64 modifiedMilliseconds;
};

struct ProfileSnapshot {
    QList<ProfileRecord> profiles;
    QList<FileFingerprint> fingerprint;
};

class ProfileRepository {
public:
    std::variant<ProfileSnapshot, ProfileRepositoryError>
    load(const RemminaInstance &instance);
};
```

Test `.remmina` suffix filtering, deterministic file order, valid symlinks,
broken symlinks, directories with the suffix, malformed-profile skipping,
mixed valid/invalid files, unreadable directory, empty directory, and source
file nonmutation. Use a parser spy to verify only candidate files are parsed.

**Step 3: Implement location and repository loading**

Reuse the allowlisted key-file reader for `[remmina_pref] datadir_path`.
Enumerate without changing the directory. Generate opaque IDs from a
collision-resistant hash of stable instance ID plus canonical profile identity;
never expose the hash input over D-Bus.

Cache parsed records only for the current instance-directory scope. Compare an
internal device/inode/size/nanosecond-mtime/ctime signature, do not cache
transient unreadable results, and revalidate identity and signature after
parsing with one bounded retry so stale output cannot poison the cache. The
repository is single-thread confined.

**Step 4: Verify and commit**

```bash
./scripts/container.sh test 'profile_locator|profile_repository'
./scripts/container.sh check
git add src/core tests CMakeLists.txt
git commit -m "feat(profiles): load selected profile set"
```

## Task 8: Add freshness-aware catalog sessions

**Files:**

- Create: `src/platform/profile_watcher.h`
- Create: `src/platform/qt_profile_watcher.h`
- Create: `src/platform/qt_profile_watcher.cpp`
- Create: `src/core/profile_catalog.h`
- Create: `src/core/profile_catalog.cpp`
- Create: `tests/test_profile_catalog.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write failing catalog lifecycle tests**

Inject repository and watcher interfaces. The catalog API should be:

```cpp
class ProfileCatalog {
public:
    CatalogResult records(const RemminaInstance &instance);
    const ProfileRecord *resolve(QStringView opaqueId) const;
    void markDirty();
    void endSession();
    void reset();
};
```

Test first lookup loading, same-session reuse over multiple keystrokes, watcher
dirty reload, new-session fingerprint verification, changed-file-only parse
reuse, selection reset, stale ID removal, repository error mapping, and safe
behavior when watcher setup fails.

**Step 2: Implement watchers and catalog**

Watch the selected directory plus parsed files with `QFileSystemWatcher`.
Watcher callbacks only mark dirty; loading remains synchronous with the next
explicit lookup. On each new session, request a repository fingerprint check
even when no event was delivered. `reset()` removes all watches and records.

**Step 3: Verify and commit**

```bash
./scripts/container.sh test profile_catalog
./scripts/container.sh check
./scripts/container.sh sanitize
git add src tests CMakeLists.txt
git commit -m "feat(profiles): refresh search snapshots"
```

## Task 9: Build shell-free Remmina launch commands

**Files:**

- Create: `src/platform/process_launcher.h`
- Create: `src/platform/qt_process_launcher.h`
- Create: `src/platform/qt_process_launcher.cpp`
- Create: `src/platform/notifier.h`
- Create: `src/platform/freedesktop_notifier.h`
- Create: `src/platform/freedesktop_notifier.cpp`
- Create: `src/core/remmina_launcher.h`
- Create: `src/core/remmina_launcher.cpp`
- Create: `tests/test_remmina_launcher.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write failing launch tests**

Use a fake launcher that records program, argument vector, and environment.
Test:

- native `--connect <launchPath>`;
- exact Flatpak installation selector, `run`, app ID, and connect arguments;
- Snap launcher arguments;
- `--new` without a profile;
- paths and labels containing spaces, quotes, semicolons, `$()`, and newlines
  remaining single inert arguments;
- `XDG_ACTIVATION_TOKEN` propagation and empty-token omission;
- missing instance, missing profile, stale selected ID, process-start failure;
- generic notifications containing no private fixture text.

**Step 2: Implement launcher and notifier**

`QtProcessLauncher` uses `QProcess::startDetached` with an explicit program,
arguments, and `QProcessEnvironment`. `RemminaLauncher` accepts only a current
registry instance and a catalog-resolved profile. It never accepts an
arbitrary command string.

Use `org.freedesktop.Notifications` or KF6 Notifications for generic failure
messages. Notification bodies must not include profile metadata or paths.

**Step 3: Verify and commit**

```bash
./scripts/container.sh test remmina_launcher
./scripts/container.sh check
git add src tests CMakeLists.txt
git commit -m "feat(launch): open Remmina safely"
```

## Task 10: Implement the DBus2 runner service

**Files:**

- Create: `data/org.kde.krunner1.xml`
- Create: `src/dbus/dbus_types.h`
- Create: `src/dbus/runner_service.h`
- Create: `src/dbus/runner_service.cpp`
- Create: `tests/test_runner_service.cpp`
- Create: `tests/test_dbus_contract.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Step 1: Copy and license the upstream interface**

Copy the same KDE `org.kde.krunner1.xml` used by Ente Auth KRunner without
semantic edits. Preserve `LGPL-2.0-or-later` and register the remote match and
action structures with QtDBus.

**Step 2: Write failing service tests**

Construct the service with fake registry, catalog, launcher, notifier, and
clock/timer durations. Test:

- unrelated and empty `rem` queries touch neither registry nor catalog;
- exact `rem new` returns one creation result and no profile read;
- no-instance lookup returns one reserved non-actionable error;
- profile matches expose only name, subtitle, icon, relevance, category, and
  empty actions;
- protocol/server/labels subtitle omits empty components cleanly;
- `Run` opens only current catalog IDs;
- creation with no instance notifies and does not launch;
- error and unknown IDs are no-ops;
- activation token is stored and forwarded, then not leaked in matches;
- `Teardown` ends the catalog session;
- idle timeout ends a missing-teardown session;
- `Config` rescans/reloads selection and resets the catalog;
- all public slots contain exceptions and return generic failure behavior.

Recursively inspect serialized match variants to prove synthetic password,
username, raw path, and unknown profile values are absent.

**Step 3: Implement service behavior**

Expose:

```cpp
RemoteMatches Match(const QString &query) noexcept;
RunnerActions Actions();
void Run(const QString &matchId, const QString &actionId);
void Teardown();
QVariantMap Config();
void SetActivationToken(const QString &token);
```

Use reserved IDs `action:new` and `error:<category>`. `Config()` returns the
case-insensitive standalone-`rem` match regex and trigger words supported by
DBus2.

**Step 4: Verify and commit**

```bash
./scripts/container.sh test 'runner_service|dbus_contract'
./scripts/container.sh check
./scripts/container.sh sanitize
git add data src/dbus tests CMakeLists.txt LICENSES
git commit -m "feat(dbus): expose Remmina runner"
```

## Task 11: Add the Remmina instance KCModule

**Files:**

- Create: `src/kcm/instance_settings_model.h`
- Create: `src/kcm/instance_settings_model.cpp`
- Create: `src/kcm/remmina_runner_config.h`
- Create: `src/kcm/remmina_runner_config.cpp`
- Create: `src/kcm/remmina_runner_config.ui`
- Create: `tests/test_instance_settings_model.cpp`
- Create: `tests/test_kcm.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write failing settings-model tests**

Separate scan/selection behavior from QWidget code. Test that opening the
model rescans, persists automatic repair immediately, lists every instance,
marks the selected item, reports partial backend failure without hiding
results, handles no instances, rejects an unavailable manual choice, and saves
a valid manual choice only on Apply.

**Step 2: Write a failing offscreen KCM test**

Instantiate the module with injected model dependencies under
`QT_QPA_PLATFORM=offscreen`. Verify dropdown labels include packaging type and
executable/ref identity, empty-state text, changed-state signaling, `load()`,
`save()`, and `defaults()`.

**Step 3: Implement and install the module**

Subclass `KCModule`, use a `QComboBox` plus status label, and follow KDE's
current plugin pattern:

```cmake
kcoreaddons_add_plugin(kcm_remmina_krunner
    SOURCES ...
    INSTALL_NAMESPACE "kf6/krunner/kcms")
```

Link Qt Widgets plus KF6 CoreAddons, ConfigCore, KCMUtils, and I18n. Keep all
profile access out of the settings process.

**Step 4: Verify and commit**

```bash
./scripts/container.sh test 'instance_settings_model|kcm'
./scripts/container.sh check
git add src/kcm tests CMakeLists.txt
git commit -m "feat(settings): choose Remmina instance"
```

## Task 12: Add service activation, metadata, and the rescan CLI

**Files:**

- Create: `src/main.cpp`
- Create: `data/org.remminakrunner.KRunner.desktop.in`
- Create: `data/org.remminakrunner.KRunner.service.in`
- Create: `cmake/EscapeDBusExec.cmake`
- Create: `tests/configure_activation_service.cmake`
- Create: `tests/test_metadata.cmake`
- Create: `tests/test_dbus_activation.sh`
- Create: `tests/test_rescan_cli.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write failing metadata and CLI tests**

Test configured metadata for:

- `X-Plasma-API=DBus2`;
- service/path IDs matching the executable registration;
- unique results;
- case-insensitive standalone `rem` regex;
- syntaxes for `rem :q:` and `rem new`;
- `X-KDE-ConfigModule=kf6/krunner/kcms/kcm_remmina_krunner`;
- an absolute, safely escaped D-Bus `Exec=` path.

Test `remmina-krunner --rescan` with fake scan/store factories: it repairs the
selection, prints only safe status/count/type data, returns success for an
empty but valid scan, and never starts D-Bus or reads profiles.

**Step 2: Implement executable startup**

Normal mode:

1. construct scanner/store/registry;
2. rescan and repair at service startup;
3. construct profile and launch components;
4. register `org.remminakrunner.KRunner` and `/runner` on the session bus; and
5. enter `QCoreApplication`.

`--rescan` performs only steps 1-2 and exits. Reject unknown options.

**Step 3: Add private-session activation test**

Start a temporary `dbus-daemon`, stage metadata into a temporary XDG data
home, activate the service, call `Config`, `Match("rem")`, and
`Match("rem new")`, and assert lifecycle output. Use fake PATH/Flatpak/Snap
inputs and no host session/profile mounts.

**Step 4: Verify and commit**

```bash
./scripts/container.sh test 'metadata|activation|rescan_cli'
./scripts/container.sh check
./scripts/container.sh sanitize
git add src/main.cpp data cmake tests CMakeLists.txt
git commit -m "feat: activate Remmina KRunner service"
```

## Task 13: Complete install and uninstall packaging

**Files:**

- Create: `packaging/install.sh`
- Create: `packaging/uninstall.sh`
- Create: `tests/test_package_scripts.sh`
- Create: `tests/test_install_inventory.cmake`
- Modify: `CMakeLists.txt`
- Modify: `scripts/ci.sh`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write failing isolated installation tests**

Use temporary absolute `HOME`, `XDG_DATA_HOME`, and install prefix fixtures.
Test:

- exact runner, service, KRunner metadata, KCM plugin, and license inventory;
- executable and data/plugin modes;
- D-Bus `Exec=` escaping with spaces, quotes, and backslashes;
- initial `--rescan` invocation after files are installed;
- default and custom XDG locations;
- repeat installation and atomic replacement;
- missing dependency/preflight failure before mutation;
- idempotent uninstall;
- preservation of selection config and unrelated files;
- refusal of relative/newline paths and unexpected bundle inventory.

Run `./scripts/container.sh test package_scripts`; expect failure.

**Step 2: Implement safe user-local scripts**

Follow the Ente installer boundaries: no sudo/package manager, strict shell
mode, resolve the script directory, validate all paths and inventory before
mutation, stage in destination filesystems, atomically rename, refresh KDE
service discovery, and stop only the exact installed runner executable during
upgrade/uninstall.

Do not remove `~/.config/remmina-krunnerrc` during normal uninstall.

**Step 3: Extend staged inventory checking**

Make `scripts/ci.sh check` stage `cmake --install` and compare an exact sorted
inventory that includes the KCModule. Add a `BUILD_TESTING=OFF` configure test.

**Step 4: Verify and commit**

```bash
./scripts/container.sh test 'package_scripts|install_inventory|build_testing_off'
./scripts/container.sh check
git add packaging tests scripts/ci.sh CMakeLists.txt
git commit -m "build: add user-local installation"
```

## Task 14: Add container CI, Dev Container, and user/developer docs

**Files:**

- Create: `.github/workflows/ci.yml`
- Create: `.devcontainer/devcontainer.json`
- Create: `scripts/check_repository_diff.sh`
- Create: `scripts/resolve_ci_diff_base.sh`
- Create: `tests/test_ci_git_check.sh`
- Create: `tests/test_ci_diff_base.sh`
- Create: `tests/test_repository_config.cmake`
- Create: `tests/test_workflow_actionlint.sh`
- Create: `tests/test_documentation.cmake`
- Create: `README.md`
- Create: `CONTRIBUTING.md`
- Modify: `containers/Containerfile`
- Modify: `scripts/container.sh`
- Modify: `scripts/ci.sh`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write failing repository and workflow tests**

Test that:

- CI builds and runs the checked-in image with Podman, never host CMake;
- both `check` and `sanitize` execute;
- actions are pinned to full revisions;
- permissions are read-only and concurrency cancellation is enabled;
- only the workspace is mounted;
- CI diff-base resolution handles pull request, push, root, and invalid input;
- the Dev Container uses the same Containerfile and an unprivileged user;
- documented local commands all begin with `./scripts/container.sh`;
- no README/CONTRIBUTING build instruction invokes host `cmake` or `ctest`.

Run the focused tests and observe failure because workflows/docs are absent.

**Step 2: Implement CI and container hardening**

Model the Ente CI but replace Docker invocations with Podman. Use
`ubuntu-24.04`, verify Podman availability explicitly, mount only the checkout,
pass `CI_DIFF_BASE`, run the full check and sanitizer modes, and keep repository
permissions least-privilege.

Add actionlint and Git working-tree/index/committed-range whitespace checks.
Handle linked-worktree metadata that is outside a repository-only container
mount with one explicit safe skip for only unavailable Git checks.

**Step 3: Write README and CONTRIBUTING**

Document:

- verified release and source installation;
- `rem <query>`, empty `rem`, and exact `rem new`;
- title/subtitle/search fields and ranking;
- settings instance list, priority, fallback, and rescan points;
- profile rescan session/watcher timing;
- native, Flatpak, and Snap profile/launch behavior;
- no-instance and failure troubleshooting;
- read-only metadata and non-retention of private fields;
- Podman-only configure/focused/check/sanitize workflows;
- manual Plasma verification without printing profile contents;
- TDD, synthetic fixtures, privacy, formatting, and PR checklist.

**Step 4: Verify and commit**

```bash
./scripts/container.sh test 'repository_config|workflow_actionlint|documentation|ci_'
./scripts/container.sh check
./scripts/container.sh sanitize
git add .github .devcontainer containers scripts tests README.md CONTRIBUTING.md
git commit -m "ci: add Podman verification workflow"
```

## Task 15: Add reproducible draft-release packaging

**Files:**

- Create: `.github/workflows/release.yml`
- Create: `scripts/configure_sanitize.sh`
- Create: `scripts/package_release.sh`
- Create: `scripts/publish_release.sh`
- Create: `scripts/validate_release_tag.sh`
- Create: `tests/test_release_package.sh`
- Create: `tests/test_release_tag.sh`
- Create: `tests/test_publish_release.sh`
- Create: `tests/test_release_workflow.cmake`
- Modify: `README.md`
- Modify: `CONTRIBUTING.md`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write failing release tests**

Cover:

- `vMAJOR.MINOR.PATCH` matching the CMake project version;
- exact archive/checksum names and one top-level archive directory;
- exact bundle inventory including runner, metadata, KCModule, scripts, and
  licenses;
- reproducible sort order, timestamp, ownership, modes, and gzip metadata;
- absence of build paths, developer trees, and host configuration;
- checksum verification;
- release workflow running check/sanitize/build/package through Podman;
- separate read-only build and write-limited publish jobs;
- immutable action revisions and one-day artifact transfer;
- refusal to replace a published release or mismatched tag/commit;
- rerunnable draft update only.

**Step 2: Implement packaging scripts and workflow**

Use `SOURCE_DATE_EPOCH` from the tagged commit. Build the Release artifact with
`BUILD_TESTING=OFF` inside the same image. Transfer only verified archive and
checksum assets to the publication job. Create a draft release with generated
notes and require manual publication.

**Step 3: Document the release process**

Add checksum-first installation, upgrade, uninstall, maintainer dry-run, tag,
draft review, and published-release immutability instructions. Every local
build/test/package command must still enter through `scripts/container.sh`.

**Step 4: Verify and commit**

```bash
./scripts/container.sh test 'release_|publish_release'
./scripts/container.sh check
./scripts/container.sh sanitize
git add .github/workflows/release.yml scripts tests README.md CONTRIBUTING.md
git commit -m "ci(release): package draft releases"
```

## Task 16: Run complete verification and document manual Plasma checks

**Files:**

- Modify if needed: `README.md`
- Modify if needed: `CONTRIBUTING.md`
- Modify if needed: neighboring source/tests found by failures

**Step 1: Run the complete normal suite**

```bash
./scripts/container.sh check
```

Expected: fresh debug configure/build, all CTest cases, exact staged inventory,
actionlint, documentation consistency, and repository checks pass.

**Step 2: Run the complete sanitizer suite**

```bash
./scripts/container.sh sanitize
```

Expected: all supported tests pass under ASan/UBSan with no warnings or leaks.

**Step 3: Build and inspect a release artifact**

```bash
./scripts/container.sh release-build
```

Expected: Release configuration uses `BUILD_TESTING=OFF`; the archive and
checksum pass exact inventory and reproducibility checks.

**Step 4: Run repository checks**

```bash
git status --short
git diff --check
git diff --cached --check
```

Expected: no uncommitted implementation changes and no whitespace errors.

**Step 5: Document but do not automate the host Plasma smoke check**

The README checklist should:

1. install the verified local bundle;
2. open Plasma Search settings and verify the detected instance list;
3. select an instance and Apply;
4. verify empty `rem`, searches by synthetic/non-private name, server, and
   label, and exact `rem new`;
5. create/edit/delete a temporary profile and verify next-session refresh;
6. temporarily choose/remove an instance to verify fallback and no-instance
   messages; and
7. uninstall without removing Remmina profiles or plugin selection config.

The checklist must warn against pasting real profile files or connection
metadata into logs/issues.

**Step 6: Commit any verification-driven documentation fixes**

```bash
git add README.md CONTRIBUTING.md
git commit -m "docs: finalize verification guide"
```

Skip this commit when verification required no tracked changes.
