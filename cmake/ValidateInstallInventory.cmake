# SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
# SPDX-License-Identifier: 0BSD

foreach(required STAGE_ROOT INSTALL_PREFIX_RELATIVE LAYOUT_FILE)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
if(NOT IS_DIRECTORY "${STAGE_ROOT}" OR IS_SYMLINK "${STAGE_ROOT}")
    message(FATAL_ERROR "STAGE_ROOT must be a real directory")
endif()
if(IS_ABSOLUTE "${INSTALL_PREFIX_RELATIVE}")
    message(FATAL_ERROR "INSTALL_PREFIX_RELATIVE must be relative")
endif()
cmake_path(NORMAL_PATH INSTALL_PREFIX_RELATIVE OUTPUT_VARIABLE normalized_prefix)
if(NOT normalized_prefix STREQUAL INSTALL_PREFIX_RELATIVE
   OR normalized_prefix MATCHES "(^|/)\\.\\.($|/)")
    message(FATAL_ERROR "INSTALL_PREFIX_RELATIVE must be normalized")
endif()

include("${LAYOUT_FILE}")
if(NOT DEFINED REMMINA_KRUNNER_PLUGIN_RELATIVE
   OR REMMINA_KRUNNER_PLUGIN_RELATIVE STREQUAL ""
   OR IS_ABSOLUTE "${REMMINA_KRUNNER_PLUGIN_RELATIVE}")
    message(FATAL_ERROR "layout contains an invalid plugin path")
endif()
cmake_path(NORMAL_PATH REMMINA_KRUNNER_PLUGIN_RELATIVE
    OUTPUT_VARIABLE normalized_plugin)
if(NOT normalized_plugin STREQUAL REMMINA_KRUNNER_PLUGIN_RELATIVE
   OR normalized_plugin MATCHES "(^|/)\\.\\.($|/)")
    message(FATAL_ERROR "layout plugin path must be normalized")
endif()

set(expected_files
    "LICENSE|f|644"
    "LICENSES/0BSD.txt|f|644"
    "LICENSES/LGPL-2.0-or-later.txt|f|644"
    "bin/remmina-krunner|f|755"
    "install.sh|f|755"
    "${REMMINA_KRUNNER_PLUGIN_RELATIVE}|f|755"
    "share/dbus-1/services/org.remminakrunner.KRunner.service|f|644"
    "share/krunner/dbusplugins/org.remminakrunner.KRunner.desktop|f|644"
    "uninstall.sh|f|755"
)
set(expected_directories)
set(expected_inventory)
foreach(file_record IN LISTS expected_files)
    string(REPLACE "|" ";" file_fields "${file_record}")
    list(GET file_fields 0 relative_file)
    list(GET file_fields 1 file_type)
    list(GET file_fields 2 file_mode)
    set(installed_file "${INSTALL_PREFIX_RELATIVE}/${relative_file}")
    list(APPEND expected_inventory "${installed_file}|${file_type}|${file_mode}")
    set(parent "${installed_file}")
    while("${parent}" MATCHES "/")
        string(REGEX REPLACE "/[^/]+$" "" parent "${parent}")
        list(APPEND expected_directories "${parent}|d|755")
    endwhile()
endforeach()
list(REMOVE_DUPLICATES expected_directories)
list(APPEND expected_inventory ${expected_directories})
list(SORT expected_inventory)

find_program(INVENTORY_FIND_EXECUTABLE NAMES find REQUIRED)
find_program(INVENTORY_SORT_EXECUTABLE NAMES sort REQUIRED)
execute_process(
    COMMAND "${INVENTORY_FIND_EXECUTABLE}" "${STAGE_ROOT}" -mindepth 1
            -printf "%P|%y|%m\\n"
    COMMAND "${INVENTORY_SORT_EXECUTABLE}"
    RESULT_VARIABLE inventory_status
    OUTPUT_VARIABLE inventory_output
    ERROR_VARIABLE inventory_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT inventory_status EQUAL 0)
    message(FATAL_ERROR "unable to inspect install inventory: ${inventory_error}")
endif()
if(inventory_output STREQUAL "")
    set(actual_inventory)
else()
    string(REPLACE "\n" ";" actual_inventory "${inventory_output}")
endif()
if(NOT actual_inventory STREQUAL expected_inventory)
    message(FATAL_ERROR
        "installed path/type/mode inventory differs\nexpected=${expected_inventory}\nactual=${actual_inventory}")
endif()
