# Release Draft Lookup Fix Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make release publication discover GitHub drafts reliably and prevent the production-only 404 that failed the v0.1.0 workflow.

**Architecture:** Replace tag-endpoint discovery with a paginated authenticated releases-collection lookup that returns zero or one validated exact-tag record. Keep all mutation and byte-verification operations bound to the captured numeric release ID.

**Tech Stack:** Bash, GitHub REST API through `gh api`, CMake/CTest shell tests, rootless Podman.

---

### Task 1: Reproduce GitHub's draft visibility

**Files:**
- Modify: `tests/test_publish_release.sh`

**Step 1: Correct the fake API boundary**

Make `repos/.../releases/tags/<tag>` return 404 for both absent and draft
releases. Add the paginated `repos/.../releases?per_page=100` collection,
including draft records, to the fake.

**Step 2: Add bounded lookup regressions**

Require first draft creation and existing interrupted-draft recovery to use the
collection. Add cases for a draft found on a later page, duplicate exact-tag
records, invalid IDs/booleans, and published-release refusal.

**Step 3: Run the focused test to verify RED**

Run: `./scripts/container.sh test '^publish_release$'`

Expected: FAIL because the publisher sees the draft tag endpoint as 404 and
attempts to create or cannot recapture the existing draft.

### Task 2: Discover releases through the authenticated collection

**Files:**
- Modify: `scripts/publish_release.sh`

**Step 1: Add exact-tag collection lookup**

Page through `repos/${repository}/releases?per_page=100`, validate each record,
select only `${release_tag}`, and return a typed zero/one/ambiguous result with
the numeric release ID and draft state.

**Step 2: Replace tag-endpoint discovery**

Use the collection before creation, after creation, and when resuming a draft.
Retain the existing ID-based identity checks, immutable published-release
refusal, tag-target resolution, and asset byte verification.

**Step 3: Run the focused test to verify GREEN**

Run: `./scripts/container.sh test '^publish_release$'`

Expected: PASS with every release publication scenario green.

**Step 4: Check shell syntax and whitespace**

Run: `bash -n scripts/publish_release.sh tests/test_publish_release.sh`

Run: `git diff --check`

Expected: both commands exit 0.

### Task 3: Verify and publish the follow-up PR

**Files:**
- Verify only: repository tree

**Step 1: Run all Podman gates**

Run: `./scripts/container.sh check`

Expected: all tests and the exact install inventory pass.

Run: `./scripts/container.sh sanitize`

Expected: all tests pass under ASan/UBSan with no diagnostics.

Run: `./scripts/container.sh release-build`

Expected: the production Release build succeeds with tests disabled.

**Step 2: Review the complete branch diff**

Compare against `origin/main`; confirm the scope is limited to the design,
plan, publisher, and publisher tests, and that no release tag or published
asset is changed.

**Step 3: Commit and open a PR**

Commit the tested implementation, push `fix/release-draft-lookup`, and open a
focused pull request explaining that the historical v0.1.0 run remains red
because tag workflows are immutable while future releases use the corrected
publisher.
