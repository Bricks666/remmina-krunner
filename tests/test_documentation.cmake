# SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
# SPDX-License-Identifier: 0BSD
cmake_minimum_required(VERSION 3.25)

set(README "${REPO_ROOT}/README.md")
set(CONTRIBUTING "${REPO_ROOT}/CONTRIBUTING.md")
foreach(DOCUMENT IN ITEMS "${README}" "${CONTRIBUTING}")
    if(NOT EXISTS "${DOCUMENT}")
        message(FATAL_ERROR "Required documentation is missing: ${DOCUMENT}")
    endif()
endforeach()

file(READ "${README}" README_TEXT)
file(READ "${CONTRIBUTING}" CONTRIBUTING_TEXT)
set(ALL_TEXT "${README_TEXT}\n${CONTRIBUTING_TEXT}")

function(assert_contains CONTENT DOCUMENT NEEDLE)
    string(FIND "${CONTENT}" "${NEEDLE}" OFFSET)
    if(OFFSET EQUAL -1)
        message(FATAL_ERROR "${DOCUMENT} must contain: ${NEEDLE}")
    endif()
endfunction()

foreach(NEEDLE IN ITEMS
    "## Installation"
    "sha256sum --check"
    "Once a release is published"
    "install.sh"
    "uninstall.sh"
    "rem <query>"
    "rem new"
    "name"
    "server"
    "IP address"
    "domain"
    "labels"
    "exact"
    "prefix"
    "substring"
    "native"
    "Flatpak"
    "Snap"
    "Apply"
    "watcher"
    "session"
    "No Remmina installations found"
    "read-only"
    "password"
    "profile path"
    "manual Plasma"
    "./scripts/container.sh source-bundle"
    "./build-source-bundle/remmina-krunner/install.sh"
)
    assert_contains("${README_TEXT}" "README.md" "${NEEDLE}")
endforeach()
assert_contains("${README_TEXT}" "README.md" "`rem` returns no results")
assert_contains("${README_TEXT}" "README.md" "case-insensitive")

foreach(NEEDLE IN ITEMS
    "Podman"
    "./scripts/container.sh configure"
    "./scripts/container.sh build"
    "./scripts/container.sh test"
    "./scripts/container.sh check"
    "./scripts/container.sh sanitize"
    "./scripts/container.sh release-build"
    "RED"
    "GREEN"
    "synthetic"
    "real Remmina profiles"
    "privacy"
    "SPDX"
    "0BSD"
    "git diff --check"
    "documentation"
)
    assert_contains("${CONTRIBUTING_TEXT}" "CONTRIBUTING.md" "${NEEDLE}")
endforeach()

string(REGEX MATCHALL "(^|\n)[ \t]*(cmake|ctest)([ \t]|$)[^\n]*" HOST_COMMANDS "${ALL_TEXT}")
if(HOST_COMMANDS)
    message(FATAL_ERROR
        "Documentation must not recommend host CMake/CTest commands: ${HOST_COMMANDS}"
    )
endif()
string(REGEX MATCHALL "(^|\n)[ \t]*\./scripts/ci\.sh([ \t]|$)[^\n]*" INTERNAL_COMMANDS "${ALL_TEXT}")
if(INTERNAL_COMMANDS)
    message(FATAL_ERROR
        "Documentation must route local work through scripts/container.sh: ${INTERNAL_COMMANDS}"
    )
endif()
