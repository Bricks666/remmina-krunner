# SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
# SPDX-License-Identifier: 0BSD
cmake_minimum_required(VERSION 3.25)
set(WORKFLOW_FILE "${REPO_ROOT}/.github/workflows/release.yml")
if(NOT IS_ABSOLUTE "${REPO_ROOT}" OR NOT EXISTS "${WORKFLOW_FILE}")
    message(FATAL_ERROR "Missing release workflow")
endif()
file(READ "${WORKFLOW_FILE}" WORKFLOW)
function(assert_contains DESCRIPTION NEEDLE)
    string(FIND "${WORKFLOW}" "${NEEDLE}" OFFSET)
    if(OFFSET EQUAL -1)
        message(FATAL_ERROR "${DESCRIPTION} is missing: ${NEEDLE}")
    endif()
endfunction()
function(assert_not_contains DESCRIPTION NEEDLE)
    string(FIND "${WORKFLOW}" "${NEEDLE}" OFFSET)
    if(NOT OFFSET EQUAL -1)
        message(FATAL_ERROR "${DESCRIPTION} contains prohibited text: ${NEEDLE}")
    endif()
endfunction()
function(assert_count DESCRIPTION NEEDLE EXPECTED)
    set(REMAINDER "${WORKFLOW}")
    set(ACTUAL 0)
    while(TRUE)
        string(FIND "${REMAINDER}" "${NEEDLE}" OFFSET)
        if(OFFSET EQUAL -1)
            break()
        endif()
        math(EXPR ACTUAL "${ACTUAL} + 1")
        string(LENGTH "${NEEDLE}" LENGTH)
        math(EXPR NEXT "${OFFSET} + ${LENGTH}")
        string(SUBSTRING "${REMAINDER}" ${NEXT} -1 REMAINDER)
    endwhile()
    if(NOT ACTUAL EQUAL EXPECTED)
        message(FATAL_ERROR "${DESCRIPTION}: expected ${EXPECTED}, found ${ACTUAL}")
    endif()
endfunction()
function(assert_before DESCRIPTION FIRST SECOND)
    string(FIND "${WORKFLOW}" "${FIRST}" FIRST_OFFSET)
    string(FIND "${WORKFLOW}" "${SECOND}" SECOND_OFFSET)
    if(FIRST_OFFSET EQUAL -1 OR SECOND_OFFSET EQUAL -1 OR NOT FIRST_OFFSET LESS SECOND_OFFSET)
        message(FATAL_ERROR "${DESCRIPTION}: ${FIRST} must precede ${SECOND}")
    endif()
endfunction()

# Parse two explicit job blocks so authority assertions bind to the correct job.
string(FIND "${WORKFLOW}" "\n  build:\n" BUILD_START)
string(FIND "${WORKFLOW}" "\n  publish:\n" PUBLISH_START)
if(BUILD_START EQUAL -1 OR PUBLISH_START EQUAL -1 OR NOT BUILD_START LESS PUBLISH_START)
    message(FATAL_ERROR "Workflow must contain build then publish jobs")
endif()
math(EXPR BUILD_LENGTH "${PUBLISH_START} - ${BUILD_START}")
string(SUBSTRING "${WORKFLOW}" ${BUILD_START} ${BUILD_LENGTH} BUILD_JOB)
string(SUBSTRING "${WORKFLOW}" ${PUBLISH_START} -1 PUBLISH_JOB)
if(NOT BUILD_JOB MATCHES "permissions:\n      contents: read" OR BUILD_JOB MATCHES "contents: write|GH_TOKEN")
    message(FATAL_ERROR "Build job must remain read-only and token-free")
endif()
if(NOT PUBLISH_JOB MATCHES "needs: build" OR NOT PUBLISH_JOB MATCHES "permissions:\n      contents: write")
    message(FATAL_ERROR "Publish job must depend on build with contents:write")
endif()

foreach(NEEDLE IN ITEMS
    "name: Release" "      - 'v*.*.*'" "  contents: read" "cancel-in-progress: false"
    "runs-on: ubuntu-24.04" "timeout-minutes: 90" "timeout-minutes: 20"
    "persist-credentials: false" "fetch-depth: 0"
    [=[rootless=$(podman info --format '{{.Host.Security.Rootless}}')]=]
    [=[[[ "${rootless}" == true ]]]=]
    [=[./scripts/validate_release_tag.sh "${GITHUB_REF_NAME}" CMakeLists.txt]=]
    [=[tag_commit=$(git rev-parse "${GITHUB_REF}^{commit}")]=]
    [=[checkout_commit=$(git rev-parse HEAD)]=]
    [=[[[ "${tag_commit}" == "${checkout_commit}" ]]]=]
    [=[git show -s --format=%ct "${tag_commit}"]=]
    [=[${source_date_epoch} =~ ^[0-9]+$]=]
    [=[printf 'SOURCE_DATE_EPOCH=%s\n' "${source_date_epoch}" >>"${GITHUB_ENV}"]=]
    "./scripts/container.sh check" "./scripts/container.sh sanitize"
    [=[./scripts/container.sh release-package "${GITHUB_REF_NAME}" "${RELEASE_OUTPUT}"]=]
    [=[archive="remmina-krunner-${GITHUB_REF_NAME}-linux-x86_64.tar.gz"]=]
    [=[sha256sum --check "${checksum}"]=] "retention-days: 1"
    [=[name: release-assets-${{ github.run_id }}-${{ github.run_attempt }}]=]
    [=[ref: ${{ needs.build.outputs.expected_commit }}]=]
    [=[GH_TOKEN: ${{ github.token }}]=]
    [=[EXPECTED_COMMIT: ${{ needs.build.outputs.expected_commit }}]=]
    "./scripts/publish_release.sh"
)
    assert_contains("release workflow" "${NEEDLE}")
endforeach()
assert_count("checkout actions" "uses: actions/checkout@" 2)
assert_count("artifact upload" "uses: actions/upload-artifact@" 1)
assert_count("artifact download" "uses: actions/download-artifact@" 1)
assert_count("token exposure" "GH_TOKEN:" 1)
assert_count("job runners" "\n    runs-on:" 2)
file(STRINGS "${WORKFLOW_FILE}" ACTION_LINES REGEX "^[ \t]*uses:")
foreach(ACTION IN LISTS ACTION_LINES)
    string(REGEX REPLACE "[ \t]+#.*$" "" ACTION "${ACTION}")
    if(NOT ACTION MATCHES "@[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]$")
        message(FATAL_ERROR "Action is not pinned to a full commit: ${ACTION}")
    endif()
endforeach()
assert_before("tag validation" "validate_release_tag.sh" "container.sh check")
assert_before("verification before package" "container.sh sanitize" "container.sh release-package")
assert_before("checksum before transfer" "sha256sum --check" "actions/upload-artifact@")
assert_before("download before publish" "actions/download-artifact@" "publish_release.sh")
foreach(PROHIBITED IN ITEMS "pull_request:" "workflow_run:" "sudo " "docker " "podman run" "podman build" "cmake " "ctest " "secrets." "--privileged" "/var/run/docker.sock" "/run/podman/podman.sock" "gh release" "gh api" "--clobber" "draft: false")
    assert_not_contains("release workflow" "${PROHIBITED}")
endforeach()
