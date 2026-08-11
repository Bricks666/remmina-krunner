<!-- SPDX-FileCopyrightText: 2026 Remmina KRunner contributors -->
<!-- SPDX-License-Identifier: 0BSD -->

# Remmina KRunner Integration Design

Date: 2026-08-12

## Goal

Provide a Plasma 6 KRunner integration for saved Remmina connections. A user
types `rem <query>`, sees matching profiles from one configured Remmina
installation, and selects a result to open that connection. The exact command
`rem new` opens Remmina's new-profile window.

The integration must support native, Flatpak, and Snap installations without
guessing installation availability from profile files. When more than one
Remmina instance is installed, a native Plasma Search settings page lets the
user choose which instance owns profile lookup, connection launch, and profile
creation.

## User-visible behavior

The runner recognizes these forms:

- `rem` returns no results and does not read profiles.
- `rem new` returns only **Create a new Remmina connection**.
- `rem <query>` searches the selected instance's saved profiles.

Only exact, case-insensitive `rem new` is reserved. For example, `rem new york`
is a normal profile search.

Each connection result contains:

- the profile `name` as its title;
- the nonempty `protocol`, `server`, and `labels` values as a
  `protocol · server · labels` subtitle;
- Remmina's application icon; and
- an internal identifier bound to the selected instance and profile.

The search covers:

- profile name;
- the raw server value, including IP addresses, hostnames, domain names, and
  ports; and
- individual comma-separated labels.

Search is Unicode case-insensitive and splits the query on whitespace. Every
token must match at least one searchable field, and different tokens may match
different fields. Exact field or label matches rank above prefixes, which rank
above substrings. Ties use folded name, server, and profile path ordering so
results are deterministic.

Group, protocol, username, notes, and filename are not searchable in the first
release. Protocol remains visible in the subtitle.

## Chosen architecture

Use an out-of-process C++20/Qt 6 D-Bus runner and a native KDE configuration
module. This follows the proven structure of the Ente Auth KRunner project
while adding the configuration UI that this integration requires.

The installed components are:

1. **Runner service** implements KDE's `org.kde.krunner1` D-Bus contract using
   the DBus2 lifecycle.
2. **KRunner metadata** registers the `rem` syntax, activation service, unique
   result behavior, and configuration module.
3. **Configuration module** presents detected Remmina instances in Plasma's
   normal Search settings.
4. **Shared static core** contains instance discovery, selection validation,
   profile discovery and parsing, matching, catalog lifecycle, and process
   launch policy. It is linked into the service and configuration module, not
   installed as an extra runtime library.
5. **Notifier** reports action-time failures through the desktop notification
   service without including private profile data.

The runner remains independent of Remmina's libraries. It reads the stable
key-file metadata needed for search and invokes Remmina's documented command
line instead of linking to Remmina internals.

## Remmina instance model

An instance descriptor contains:

- packaging type: native, Flatpak, or Snap;
- stable identifier;
- user-facing description;
- exact launcher and fixed launcher prefix arguments;
- configuration and data environment;
- profile-path mapping needed by its sandbox; and
- deterministic priority within its packaging type.

The scanner discovers applications, never profiles:

### Native

Enumerate every executable named `remmina` in every `PATH` directory. Resolve
canonical paths only to validate executables, remove duplicates, and classify
Snap targets so the same Snap is not also reported as native. Native instances
retain PATH order and use the first clean absolute lexical launcher path for
launch and stable identity, so retargeting a versioned symlink during an update
does not discard the selection.

### Flatpak

When a Flatpak launcher is executable, enumerate the exact
`org.remmina.Remmina` application ref in the user installation and every named
system installation. User and system installations are distinct selectable
instances even when they share the same per-application user data. The stable
identity includes the Flatpak installation identity and stable ref components,
but excludes the deployed commit so application updates do not discard the
selection.

### Snap

Detect the installed `remmina` Snap through its executable application
launcher and package identity. The stable identity excludes the current Snap
revision so refreshes do not discard the selection.

External discovery tools are executed without a shell, with fixed arguments,
bounded output, and bounded timeouts. A failure in one packaging scanner does
not hide instances found by another scanner.

## Selection and settings lifecycle

Store the selected stable instance identifier in a dedicated KDE configuration
file. Instance ordering is:

1. native instances in PATH order;
2. Flatpak instances, user before system and then deterministic installation
   order; and
3. Snap instances.

Every validation applies these rules:

1. Preserve the saved selection if the same instance remains available.
2. Otherwise choose the first available instance in the ordering above.
3. Clear the selection when no instance is available.
4. Persist an automatic repair immediately.

An instance scan and selection validation occur:

- after installation through `remmina-krunner --rescan`;
- whenever the configuration module opens; and
- whenever the D-Bus runner service starts.

The configuration module shows every discovered instance in a dropdown with
its packaging type and executable or ref identity. Automatic validation is
already persisted; manual selection changes follow normal Apply/OK behavior.

The metadata uses the DBus2 API. When settings are applied, Plasma calls the
runner's `Config()` method. The service reloads and revalidates the selection,
clears the profile catalog and watchers, and applies the new instance to the
next query.

## Profile location

Only the selected instance's profile environment is used.

- Native instances use the host XDG configuration and data locations.
- Flatpak instances use the `org.remmina.Remmina` per-application XDG
  locations below `~/.var/app`.
- Snap uses the Remmina Snap's current user-data environment below
  `~/snap/remmina`.

Within that environment, profile location mirrors Remmina's precedence:

1. an existing custom `datadir_path` from the instance's `remmina.pref`;
2. its legacy `.remmina` directory;
3. its user data `remmina` directory; and
4. applicable native system data directories.

Only regular or Remmina-compatible linked files ending in `.remmina` are
offered to the parser. Paths passed during activation are translated to the
selected sandbox's view when needed. Profile discovery is read-only and never
creates a missing Remmina directory.

## Profile parsing and privacy

Parse each profile as a GLib-compatible key file. The parser streams the file
and retains only these keys from the `[remmina]` group:

- `name`;
- `server`;
- `labels`; and
- `protocol`.

A valid result requires the same basic identity Remmina requires, including a
nonempty name and a usable profile path. Labels are split on commas and
trimmed for matching while their readable value is preserved for display.

The parser does not retain passwords, usernames, gateways, SSH settings,
notes, or unknown fields. No raw profile content, connection metadata, or
profile path is written to logs. D-Bus results contain only the metadata that
the UI intentionally displays.

## Profile rescan lifecycle

Profile discovery is lazy and freshness-aware:

1. Runner startup clears profile state but does not read profiles.
2. The first nonempty lookup query in each KRunner match session enumerates the
   selected profile directory and builds a snapshot.
3. Later keystrokes in that session reuse the snapshot.
4. Directory and file watchers mark the snapshot dirty when profiles are
   created, edited, renamed, or removed. The next lookup query rescans.
5. A new match session verifies the directory and file fingerprint even if no
   watcher event arrived, then reparses only changed files.
6. Changing or automatically replacing the selected instance clears the
   snapshot and watchers.
7. DBus2 teardown ends the session. A bounded idle timer provides a fallback
   if a client fails to deliver teardown.

`rem` and `rem new` never scan profiles. After a new profile is saved, its
filesystem event invalidates the snapshot and makes it available to the next
lookup.

## Query and result data flow

For a query such as `rem office east`:

1. KRunner activates or calls the D-Bus service because the query matches the
   registered `rem` syntax.
2. The service parses the trigger and rejects empty or unrelated input without
   touching the profile catalog.
3. The catalog refreshes the selected instance's profile snapshot when the
   lifecycle rules require it.
4. The matcher requires `office` and `east` to occur across name, server, or
   labels and ranks all candidates.
5. The service returns visible metadata and an opaque result identifier. It
   does not return raw file contents or the launcher command.
6. On activation, the service verifies that the result belongs to the current
   selected instance, the instance remains available, and the profile still
   exists.
7. The launcher starts the selected Remmina instance with its documented
   connect arguments.

## Launch behavior

Launches use `QProcess` with an exact executable, argument list, and process
environment. No shell or command-string evaluation is allowed.

The logical commands are:

- native: `<selected-binary> --connect <profile>`;
- Flatpak: `flatpak <installation-selector> run org.remmina.Remmina --connect
  <profile>`;
- Snap: `<selected-snap-launcher> --connect <profile>`; and
- creation: the same selected launcher with `--new` and no profile argument.

The runner records KRunner's activation token and passes it as
`XDG_ACTIVATION_TOKEN` when starting Remmina so the connection or editor can be
presented correctly on Wayland.

Result identifiers include the selected stable instance identity and an opaque
profile identity. A result from an earlier selection is never silently opened
with a different Remmina instance.

## Error handling

Expected lookup failures return harmless, non-actionable informational rows:

- With no selected installation, `rem <query>` returns **Remmina unavailable**
  with a message that no installation is selected.
- An unreadable profile directory returns a generic profile-unavailable row.
- Reserved error identifiers make activation a no-op.

A missing or empty profile directory and a query with no matches return no
results. Malformed individual profiles are skipped without hiding valid
profiles.

`rem new` always returns the creation result. If no instance is selected when
it is activated, the service sends a desktop notification explaining that no
Remmina installation is available. It does not start a process.

Action-time failures use generic notifications:

- the selected instance disappeared;
- the profile disappeared after matching; or
- the Remmina process could not be started.

Notifications and logs do not include profile names, servers, labels, raw
paths, or profile contents.

## Container-only development

All compilation and automated testing run in Podman. Host-side CMake builds
and host-side automated tests are unsupported and undocumented.

`containers/Containerfile` is the single digest-pinned Fedora 44 toolchain
definition. It contains CMake, a C++20 compiler, Qt 6, the required KDE
Frameworks development packages, test tools, sanitizers, actionlint, and
packaging utilities.

The entry points are:

- `scripts/container.sh`, which builds the image and owns every host-side
  `podman build` and `podman run` invocation; and
- `scripts/ci.sh`, which runs only inside the image and provides configure,
  focused-test, check, sanitizer, install-inventory, and release-build modes.

Only the repository is mounted into the container. Real Remmina configuration,
profiles, session D-Bus, desktop sockets, the host home, and container-engine
sockets are not mounted. Automated tests use synthetic fixtures and fake
process discovery.

A Dev Container configuration builds the same Containerfile and documents
Podman as its runtime. It runs as an unprivileged developer user and mounts
only the workspace.

## Continuous integration and releases

GitHub Actions uses Podman to build and execute the checked-in image. The CI
workflow runs on pull requests, pushes to `main`, and manual dispatch. It:

1. checks out full-enough history for repository diff validation;
2. builds the toolchain image;
3. runs the debug build, complete offscreen CTest suite, staged-install
   inventory, metadata checks, formatting checks, and repository diff checks;
4. runs the complete ASan/UBSan suite; and
5. uses pinned actions, read-only permissions, timeouts, and concurrency
   cancellation.

A tag-driven workflow repeats the complete checks, produces a Release build
inside Podman, assembles a reproducible user-local bundle, writes a SHA-256
checksum, and creates or updates only a draft GitHub release. Publication
remains a manual review step.

The bundle contains the runner, KRunner and D-Bus metadata, configuration
module, install and uninstall scripts, and license material. Installation:

- validates the platform, dependencies, paths, file inventory, and modes
  before mutation;
- atomically installs only project-owned files below the user's local prefix;
- refreshes KDE service discovery;
- performs the initial `--rescan`; and
- never invokes sudo or a package manager.

Uninstallation is idempotent and removes only project-owned installed files.
It preserves the selected-instance configuration unless the user explicitly
chooses to remove it.

## Documentation and licensing

The repository includes:

- `README.md` covering installation, configuration, syntax, result behavior,
  instance fallback, profile rescan timing, native/Flatpak/Snap behavior,
  troubleshooting, privacy, and release verification;
- `CONTRIBUTING.md` covering the Podman-only workflow, TDD, focused and full
  checks, synthetic fixtures, code style, privacy rules, and the pull-request
  checklist;
- the approved design and detailed implementation plan in `docs/plans`;
- `.gitignore`, formatting configuration, Dev Container metadata, workflows,
  repository checks, packaging scripts, and install/uninstall tests; and
- SPDX headers and complete license texts.

Original project work is licensed under `0BSD`, matching Ente Auth KRunner.
The copied KDE D-Bus interface remains under `LGPL-2.0-or-later` with its
upstream notice and corresponding license text.

## Automated testing

QtTest and CTest cover the following with injected filesystem, process,
configuration, clock, watcher, notifier, and launcher boundaries:

### Instance discovery

- multiple native executables, PATH ordering, stable lexical launchers across
  symlink retargets, canonical deduplication, and Snap exclusion;
- user and named-system Flatpak installations;
- Snap discovery;
- stable identities across application updates;
- native, Flatpak, Snap default priority;
- partial scanner failure, malformed output, timeout, and no-instance cases.

### Selection and settings

- initial default selection;
- preservation of a valid manual selection;
- fallback after removal;
- clearing when no instance exists;
- installation, settings-open, service-start, and settings-apply lifecycle;
- configuration-module list, selection, Apply, and empty-state behavior.

### Profile discovery and parsing

- native, Flatpak, and Snap configuration/data roots;
- custom data-directory and legacy precedence;
- sandbox-visible activation path mapping;
- valid, incomplete, malformed, linked, removed, and unreadable fixtures;
- label splitting and display preservation;
- proof that passwords and unrelated settings are not retained or returned.

### Search and catalog lifecycle

- name, IPv4, IPv6, hostname/domain, port-bearing server, and label searches;
- Unicode case folding and multi-token matching across fields;
- exact, prefix, substring ranking and deterministic ties;
- `rem`, `rem new`, `rem new york`, unrelated input, and malformed whitespace;
- first-session scan, same-session reuse, watcher invalidation, fingerprint
  fallback, selection invalidation, teardown, and idle timeout;
- proof that `rem` and `rem new` perform no profile access.

### D-Bus and launch behavior

- visible-only D-Bus match payloads;
- exact executable, arguments, environment, and Flatpak installation target;
- activation-token propagation;
- stale selection/profile rejection;
- non-actionable errors and generic notifications;
- no shell invocation or command interpolation;
- DBus2 configuration and teardown lifecycle;
- service activation and metadata registration.

### Repository and distribution

- `BUILD_TESTING=OFF` configuration;
- normal and ASan/UBSan builds;
- exact staged-install and release inventories;
- user-local installation, upgrade, initial scan, and idempotent uninstall;
- workflow syntax, pinned actions, permissions, release tag validation, draft
  protection, reproducible archive metadata, and checksum verification;
- documentation command consistency and clean repository diffs.

No automated test reads a real Remmina profile or requires an installed
Remmina, Flatpak, Snap, KDE session, or network connection.

## Acceptance criteria

- Every distinct native, Flatpak, and Snap Remmina installation is detected
  without using profile presence as an installation signal.
- The configuration module lists all detected instances and the saved
  selection follows the approved preserve/fallback/clear rules.
- Installation, settings opening, and runner startup rescan instances.
- Only the selected instance supplies profiles and receives connect/create
  launches.
- `rem` returns nothing; exact `rem new` opens creation; nonempty searches find
  profiles by name, IP/domain/server, and labels with deterministic ranking.
- Results display name and `protocol · server · labels` only.
- Profiles are fresh at every new KRunner search session and filesystem changes
  invalidate an active snapshot.
- No-instance lookup and creation behavior matches the approved informational
  result and notification behavior.
- Profile access remains read-only, private fields never leave the parser, and
  no shell is involved in discovery or launch.
- Every build and automated test, locally and in CI/release workflows, runs in
  the checked-in Podman toolchain image.
- The complete normal and sanitizer suites, staged installation, packaging,
  documentation, and repository checks pass.

## Upstream references

- [KRunner C++ plugin and configuration documentation](https://develop.kde.org/docs/plasma/krunner/)
- [KRunner metadata and DBus2 lifecycle](https://develop.kde.org/docs/plasma/krunner/metadata/)
- [Remmina source](https://github.com/FreeRDP/Remmina)
- [Remmina command-line usage](https://remmina.org/faq/)
- [Ente Auth KRunner reference project](https://github.com/Bricks666/ente-auth-krunner)
