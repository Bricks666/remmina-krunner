# SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
# SPDX-License-Identifier: 0BSD
cmake_minimum_required(VERSION 3.25)

if(NOT IS_ABSOLUTE "${REPO_ROOT}" OR NOT IS_ABSOLUTE "${BASH_EXECUTABLE}")
    message(FATAL_ERROR "Repository and Bash paths must be absolute")
endif()

set(WORKFLOW "${REPO_ROOT}/.github/workflows/ci.yml")
set(DEVCONTAINER "${REPO_ROOT}/.devcontainer/devcontainer.json")
set(CONTAINERFILE "${REPO_ROOT}/containers/Containerfile")
set(CONTAINER_WRAPPER "${REPO_ROOT}/scripts/container.sh")
set(CI_SCRIPT "${REPO_ROOT}/scripts/ci.sh")
set(GIT_CHECK "${REPO_ROOT}/scripts/check_repository_diff.sh")
set(DIFF_RESOLVER "${REPO_ROOT}/scripts/resolve_ci_diff_base.sh")

foreach(REQUIRED IN ITEMS
    "${WORKFLOW}" "${DEVCONTAINER}" "${CONTAINERFILE}"
    "${CONTAINER_WRAPPER}" "${CI_SCRIPT}" "${GIT_CHECK}" "${DIFF_RESOLVER}"
)
    if(NOT EXISTS "${REQUIRED}")
        message(FATAL_ERROR "Missing repository configuration: ${REQUIRED}")
    endif()
endforeach()

function(assert_contains CONTENT DESCRIPTION NEEDLE)
    string(FIND "${CONTENT}" "${NEEDLE}" OFFSET)
    if(OFFSET EQUAL -1)
        message(FATAL_ERROR "${DESCRIPTION} is missing: ${NEEDLE}")
    endif()
endfunction()

function(assert_not_contains CONTENT DESCRIPTION NEEDLE)
    string(FIND "${CONTENT}" "${NEEDLE}" OFFSET)
    if(NOT OFFSET EQUAL -1)
        message(FATAL_ERROR "${DESCRIPTION} contains prohibited text: ${NEEDLE}")
    endif()
endfunction()

function(assert_count CONTENT DESCRIPTION NEEDLE EXPECTED)
    set(REMAINDER "${CONTENT}")
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
        message(FATAL_ERROR
            "${DESCRIPTION}: expected ${EXPECTED} '${NEEDLE}', found ${ACTUAL}"
        )
    endif()
endfunction()

file(READ "${WORKFLOW}" WORKFLOW_TEXT)
foreach(NEEDLE IN ITEMS
    "pull_request:"
    "branches:"
    "- main"
    "workflow_dispatch:"
    "permissions:"
    "contents: read"
    "cancel-in-progress: true"
    "runs-on: ubuntu-24.04"
    "timeout-minutes:"
    "fetch-depth: 0"
    "persist-credentials: false"
    "podman --version"
    "./scripts/resolve_ci_diff_base.sh"
    "./scripts/container.sh check"
    "./scripts/container.sh sanitize"
)
    assert_contains("${WORKFLOW_TEXT}" "CI workflow" "${NEEDLE}")
endforeach()
assert_count("${WORKFLOW_TEXT}" "CI permissions" "permissions:" 1)
assert_count("${WORKFLOW_TEXT}" "CI jobs" "    runs-on:" 1)
assert_not_contains("${WORKFLOW_TEXT}" "CI workflow" "docker ")
assert_not_contains("${WORKFLOW_TEXT}" "CI workflow" "podman run")
assert_not_contains("${WORKFLOW_TEXT}" "CI workflow" "podman build")
assert_not_contains("${WORKFLOW_TEXT}" "CI workflow" "cmake ")
assert_not_contains("${WORKFLOW_TEXT}" "CI workflow" "ctest ")
foreach(PRIVATE_MOUNT IN ITEMS
    "/var/run/docker.sock" "/run/podman/podman.sock" "/run/user/"
    "SSH_AUTH_SOCK" "DBUS_SESSION_BUS_ADDRESS" "/home/runner"
)
    assert_not_contains("${WORKFLOW_TEXT}" "CI workflow" "${PRIVATE_MOUNT}")
endforeach()
file(STRINGS "${WORKFLOW}" ACTION_USES REGEX "^[ \t]*uses:")
if(NOT ACTION_USES)
    message(FATAL_ERROR "CI workflow must use a pinned checkout action")
endif()
foreach(ACTION IN LISTS ACTION_USES)
    string(STRIP "${ACTION}" ACTION)
    string(REGEX REPLACE "[ \t]+#.*$" "" ACTION_WITHOUT_COMMENT "${ACTION}")
    string(FIND "${ACTION_WITHOUT_COMMENT}" "@" AT_OFFSET REVERSE)
    if(AT_OFFSET EQUAL -1)
        message(FATAL_ERROR "Workflow action is not pinned to a full revision: ${ACTION}")
    endif()
    math(EXPR REVISION_OFFSET "${AT_OFFSET} + 1")
    string(SUBSTRING "${ACTION_WITHOUT_COMMENT}" ${REVISION_OFFSET} -1 REVISION)
    string(LENGTH "${REVISION}" REVISION_LENGTH)
    if(NOT REVISION_LENGTH EQUAL 40 OR NOT REVISION MATCHES "^[0-9a-f]+$")
        message(FATAL_ERROR "Workflow action revision is not 40 hexadecimal characters")
    endif()
endforeach()

file(READ "${DEVCONTAINER}" DEVCONTAINER_TEXT)
string(JSON DEV_NAME ERROR_VARIABLE JSON_ERROR GET "${DEVCONTAINER_TEXT}" name)
if(JSON_ERROR)
    message(FATAL_ERROR "devcontainer.json is invalid: ${JSON_ERROR}")
endif()
string(JSON DEV_DOCKERFILE GET "${DEVCONTAINER_TEXT}" build dockerfile)
string(JSON DEV_USER GET "${DEVCONTAINER_TEXT}" remoteUser)
string(JSON DEV_MOUNT GET "${DEVCONTAINER_TEXT}" workspaceMount)
string(JSON DEV_POST_CREATE GET "${DEVCONTAINER_TEXT}" postCreateCommand)
string(JSON DEV_CONTAINER_MARKER GET "${DEVCONTAINER_TEXT}"
    containerEnv REMMINA_KRUNNER_CONTAINER)
string(JSON DEV_HOME GET "${DEVCONTAINER_TEXT}" containerEnv HOME)
if(NOT DEV_DOCKERFILE STREQUAL "../containers/Containerfile")
    message(FATAL_ERROR "Dev Container must use containers/Containerfile")
endif()
if(NOT DEV_USER STREQUAL "developer")
    message(FATAL_ERROR "Dev Container must use the unprivileged developer user")
endif()
if(NOT DEV_POST_CREATE STREQUAL "./scripts/ci.sh configure")
    message(FATAL_ERROR "Dev Container must configure through the in-container CI script")
endif()
if(NOT DEV_CONTAINER_MARKER STREQUAL "1")
    message(FATAL_ERROR "Dev Container must set the CI script container marker")
endif()
if(NOT DEV_HOME STREQUAL "/tmp")
    message(FATAL_ERROR "Dev Container must isolate HOME at /tmp")
endif()
if(NOT DEV_MOUNT STREQUAL
   [=[source=${localWorkspaceFolder},target=/workspace,type=bind]=])
    message(FATAL_ERROR "Dev Container must mount only the repository workspace")
endif()
foreach(PRIVATE_MOUNT IN ITEMS
    "docker.sock" "podman.sock" "SSH_AUTH_SOCK" "DBUS_SESSION_BUS_ADDRESS"
    "/run/user/" [=[source=${localEnv:HOME}]=]
)
    assert_not_contains("${DEVCONTAINER_TEXT}" "Dev Container" "${PRIVATE_MOUNT}")
endforeach()

file(READ "${CONTAINERFILE}" CONTAINER_TEXT)
foreach(NEEDLE IN ITEMS
    "clang-format"
    "ARG ACTIONLINT_VERSION=1.7.12"
    "8aca8db96f1b94770f1b0d72b6dddcb1ebb8123cb3712530b08cc387b349a3d8"
    "325e971b6ba9bfa504672e29be93c24981eeb1c07576d730e9f7c8805afff0c6"
    "sha256sum --check"
    "useradd --create-home --shell /bin/bash developer"
)
    assert_contains("${CONTAINER_TEXT}" "Containerfile" "${NEEDLE}")
endforeach()

file(READ "${CONTAINER_WRAPPER}" WRAPPER_TEXT)
foreach(NEEDLE IN ITEMS
    "containers/Containerfile"
    "--env HOME=/tmp"
    [=[--user "${host_user_id}:${host_group_id}"]=]
    "CI_DIFF_BASE"
    [=[--volume "${repository_root}:/workspace:Z"]=]
    "source-bundle"
)
    assert_contains("${WRAPPER_TEXT}" "container wrapper" "${NEEDLE}")
endforeach()
assert_count("${WRAPPER_TEXT}" "container wrapper mounts" "--volume " 1)
foreach(PRIVATE_MOUNT IN ITEMS
    "docker.sock" "podman.sock" "SSH_AUTH_SOCK" "DBUS_SESSION_BUS_ADDRESS"
    "/run/user/" [=[${HOME}:]=]
)
    assert_not_contains("${WRAPPER_TEXT}" "container wrapper" "${PRIVATE_MOUNT}")
endforeach()

file(READ "${CI_SCRIPT}" CI_TEXT)
assert_contains("${CI_TEXT}" "CI script" [=["${script_directory}/check_repository_diff.sh" "${repository_root}"]=])
assert_contains("${CI_TEXT}" "CI script" "source_bundle")
assert_contains("${CI_TEXT}" "CI script" [=[-DCMAKE_INSTALL_PREFIX="/${bundle_prefix}"]=])
assert_contains("${CI_TEXT}" "CI script" "ValidateInstallInventory.cmake")
file(READ "${GIT_CHECK}" GIT_CHECK_TEXT)
assert_contains("${GIT_CHECK_TEXT}" "repository checker" "clang-format")

foreach(SCRIPT IN ITEMS
    "${CONTAINER_WRAPPER}" "${CI_SCRIPT}" "${GIT_CHECK}" "${DIFF_RESOLVER}"
)
    execute_process(
        COMMAND "${BASH_EXECUTABLE}" -n "${SCRIPT}"
        RESULT_VARIABLE SYNTAX_RESULT
        ERROR_VARIABLE SYNTAX_ERROR
    )
    if(NOT SYNTAX_RESULT EQUAL 0)
        message(FATAL_ERROR "${SCRIPT} failed bash -n: ${SYNTAX_ERROR}")
    endif()
endforeach()
