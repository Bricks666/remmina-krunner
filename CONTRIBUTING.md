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
