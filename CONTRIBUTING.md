<!-- SPDX-FileCopyrightText: 2026 Remmina KRunner contributors -->
<!-- SPDX-License-Identifier: 0BSD -->

# Contributing

This project is container-first and Podman-only. Every local configure, build,
or test command starts with `./scripts/container.sh`; never invoke CMake, CTest,
a host compiler, or a second ad-hoc toolchain on the host. The wrapper builds
`containers/Containerfile`, mounts only the repository workspace, uses the
caller UID/GID, and isolates `HOME` at `/tmp`.

## Local commands

```bash
./scripts/container.sh configure
./scripts/container.sh build
./scripts/container.sh test 'matcher|profile_parser'
./scripts/container.sh check
./scripts/container.sh sanitize
./scripts/container.sh release-build
```

Use `test '<ctest-regex>'` for a focused test. `check` configures a fresh debug
tree, builds it, runs every test, validates the staged install inventory, checks
clang-format, and checks Git whitespace in the working tree, index, and CI
commit range. `sanitize` repeats the suite with ASan and UBSan. `release-build`
configures with tests disabled and proves the production graph builds.

## Release process

Prepare a local release package through the same Podman wrapper. The timestamp
must be the release commit time, and the output directory must be a new empty
directory inside the workspace:

```bash
release_tag=v0.1.0
release_commit=$(git rev-parse HEAD)
export SOURCE_DATE_EPOCH=$(git show -s --format=%ct "${release_commit}")
mkdir -m 0755 build-release-assets
./scripts/container.sh release-package "${release_tag}" "${PWD}/build-release-assets"
```

The v0.1.0 binary runtime target is Fedora Linux 44 x86_64. Both the published
archive and `source-bundle` output use the pinned Fedora container's dynamic
Qt/KDE ABI and `lib64` plugin layout. A distro-native build, package, and layout
is required for every other distribution; none is provided by the current
release workflow. aarch64 is outside the current binary release contract.

Package tests may set `REMMINA_KRUNNER_OS_RELEASE_FILE` to a bounded absolute
regular fixture in order to exercise synthetic platform records. This is a
test-only input, not an end-user compatibility override, and the installer
rejects symlinks, malformed records, duplicate keys, and unsupported targets.

The wrapper performs a Release build with `BUILD_TESTING=OFF`, creates the
archive and checksum in the checked-in container, and verifies the exact
nine-file bundle. Verify and review both assets before tagging. A release tag
may be lightweight (`git tag v0.1.0`) or annotated
(`git tag -a v0.1.0 -m 'v0.1.0'`), but it must use canonical
`vMAJOR.MINOR.PATCH`, match the CMake project version, and point at the tested
commit. Push the tag only after the commit is final:

```bash
git push origin v0.1.0
```

The release workflow repeats check, sanitizer, and reproducible packaging with
rootless Podman. It transfers only the archive and checksum to a separately
authorized publication job, which creates or safely updates a **draft** GitHub
Release. Review its generated notes, asset names, and checksum, then publish it
manually in GitHub. The automation never publishes a release.

Treat a published release as immutable: never retag a published version,
replace its assets, or move its tag. If something is wrong, increment the
project version, repeat verification, and publish a new release. Reruns may
repair only an unpublished draft whose tag still resolves to the verified
commit.

The Dev Container uses the same Containerfile and unprivileged `developer`
account. Configure your editor's container provider to use Podman. The checked-in
configuration has no project-declared credential or engine-socket mounts and
mounts only the repository workspace; it does not declare the host home or
desktop session either. Some editor providers can independently forward Git
credentials or an SSH agent. For strict isolation, inspect and disable
provider-added credential forwarding in the editor provider.

## Test-driven changes

Use strict RED/GREEN/refactor development:

1. Add the smallest test that expresses the missing behavior.
2. Run the focused container test and record the expected RED failure.
3. Add the minimum production change, rerun for GREEN, then refactor.
4. Run the complete container checks before requesting review.

Tests use synthetic fixtures and isolated temporary XDG/HOME/session paths.
Never mount or read real Remmina profiles, a real HOME, desktop session state,
or private data. Synthetic profile values must be obviously invented and must
not be copied from or derived from user data.

## Privacy and style

Preserve the read-only metadata boundary: production parsing may retain only
the explicitly allowed display/search fields and internal launch identity.
Runtime logging, assertions, test output, issue reports, and screenshots must
not expose profile contents, paths, servers, labels, usernames, passwords, or
other connection metadata.

C++20 code follows the repository `.clang-format`. Shell is Bash with strict
mode, quoted expansions, bounded temporary paths, and explicit cleanup.
Every new source, script, workflow, test, and document needs SPDX headers.
Original project work uses 0BSD; preserve the LGPL-2.0-or-later notice on
`data/org.kde.krunner1.xml`.

## Pull request checklist

- A focused test demonstrated RED before the implementation and GREEN after it.
- `./scripts/container.sh check` passes.
- `./scripts/container.sh sanitize` passes.
- `./scripts/container.sh release-build` passes when build/install inputs changed.
- `git diff --check` and the repository diff/range check are clean.
- Tests use only synthetic data and preserve privacy boundaries.
- README, contributing guidance, design/plan, and automated documentation
  checks reflect user-visible or workflow changes.
- New files have correct SPDX notices and no unrelated changes are included.
