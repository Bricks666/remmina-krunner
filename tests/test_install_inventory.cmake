# SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
# SPDX-License-Identifier: 0BSD

if(NOT DEFINED MODE OR NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "MODE and SOURCE_DIR are required")
endif()

function(assert_inventory root prefix libdir plugin_dir)
    file(GLOB_RECURSE actual RELATIVE "${root}" LIST_DIRECTORIES false "${root}/*")
    list(SORT actual)
    set(expected
        "${prefix}/LICENSE"
        "${prefix}/LICENSES/0BSD.txt"
        "${prefix}/LICENSES/LGPL-2.0-or-later.txt"
        "${prefix}/bin/remmina-krunner"
        "${prefix}/install.sh"
        "${prefix}/${plugin_dir}/kcm_remmina_krunner.so"
        "${prefix}/share/dbus-1/services/org.remminakrunner.KRunner.service"
        "${prefix}/share/krunner/dbusplugins/org.remminakrunner.KRunner.desktop"
        "${prefix}/uninstall.sh"
    )
    list(SORT expected)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "installed inventory differs\nexpected=${expected}\nactual=${actual}")
    endif()

    foreach(executable
            "${root}/${prefix}/bin/remmina-krunner"
            "${root}/${prefix}/install.sh"
            "${root}/${prefix}/${plugin_dir}/kcm_remmina_krunner.so"
            "${root}/${prefix}/uninstall.sh")
        execute_process(COMMAND stat -c %a "${executable}"
            OUTPUT_VARIABLE permissions OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE stat_status)
        if(NOT stat_status EQUAL 0 OR NOT permissions STREQUAL "755")
            message(FATAL_ERROR "expected mode 0755: ${executable} (${permissions})")
        endif()
    endforeach()
    foreach(data_file
            "${root}/${prefix}/LICENSE"
            "${root}/${prefix}/LICENSES/0BSD.txt"
            "${root}/${prefix}/LICENSES/LGPL-2.0-or-later.txt"
            "${root}/${prefix}/share/dbus-1/services/org.remminakrunner.KRunner.service"
            "${root}/${prefix}/share/krunner/dbusplugins/org.remminakrunner.KRunner.desktop")
        execute_process(COMMAND stat -c %a "${data_file}"
            OUTPUT_VARIABLE permissions OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE stat_status)
        if(NOT stat_status EQUAL 0 OR NOT permissions STREQUAL "644")
            message(FATAL_ERROR "expected mode 0644: ${data_file} (${permissions})")
        endif()
    endforeach()

    file(STRINGS
        "${root}/${prefix}/share/dbus-1/services/org.remminakrunner.KRunner.service"
        exec_lines REGEX "^Exec=")
    list(LENGTH exec_lines exec_count)
    if(NOT exec_count EQUAL 1)
        message(FATAL_ERROR "installed D-Bus service must have one Exec line")
    endif()
    list(GET exec_lines 0 exec_line)
    set(expected_exec "Exec=\"/${prefix}/bin/remmina-krunner\"")
    if(NOT exec_line STREQUAL expected_exec)
        message(FATAL_ERROR "installed activation path differs: ${exec_line}")
    endif()
endfunction()

if(MODE STREQUAL "inventory")
    foreach(required BUILD_DIR INSTALL_PREFIX KDE_PLUGIN_DIR)
        if(NOT DEFINED ${required})
            message(FATAL_ERROR "${required} is required")
        endif()
    endforeach()
    string(REGEX REPLACE "^/" "" relative_prefix "${INSTALL_PREFIX}")
    set(stage "${BUILD_DIR}/install-inventory-stage")
    file(REMOVE_RECURSE "${stage}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "DESTDIR=${stage}"
                "${CMAKE_COMMAND}" --install "${BUILD_DIR}"
        RESULT_VARIABLE install_status
        OUTPUT_VARIABLE install_output
        ERROR_VARIABLE install_error)
    if(NOT install_status EQUAL 0)
        message(FATAL_ERROR "cmake --install failed:\n${install_output}\n${install_error}")
    endif()
    assert_inventory("${stage}" "${relative_prefix}" "${INSTALL_LIBDIR}" "${KDE_PLUGIN_DIR}")
elseif(MODE STREQUAL "build_testing_off")
    if(NOT DEFINED TEST_ROOT)
        message(FATAL_ERROR "TEST_ROOT is required")
    endif()
    file(REMOVE_RECURSE "${TEST_ROOT}")
    set(prefix "${TEST_ROOT}/prefix with spaces")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}" -B "${TEST_ROOT}/build"
                -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
                "-DCMAKE_INSTALL_PREFIX=${prefix}"
        RESULT_VARIABLE configure_status
        OUTPUT_VARIABLE configure_output
        ERROR_VARIABLE configure_error)
    if(NOT configure_status EQUAL 0)
        message(FATAL_ERROR "BUILD_TESTING=OFF configure failed:\n${configure_output}\n${configure_error}")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${TEST_ROOT}/build" --parallel 2
        RESULT_VARIABLE build_status
        OUTPUT_VARIABLE build_output
        ERROR_VARIABLE build_error)
    if(NOT build_status EQUAL 0)
        message(FATAL_ERROR "BUILD_TESTING=OFF build failed:\n${build_output}\n${build_error}")
    endif()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --install "${TEST_ROOT}/build"
        RESULT_VARIABLE install_status
        OUTPUT_VARIABLE install_output
        ERROR_VARIABLE install_error)
    if(NOT install_status EQUAL 0)
        message(FATAL_ERROR "BUILD_TESTING=OFF install failed:\n${install_output}\n${install_error}")
    endif()
    file(STRINGS "${prefix}/share/dbus-1/services/org.remminakrunner.KRunner.service"
        exec_lines REGEX "^Exec=")
    list(GET exec_lines 0 exec_line)
    string(FIND "${exec_line}" "prefix with spaces/bin/remmina-krunner" path_position)
    if(path_position EQUAL -1)
        message(FATAL_ERROR "custom-prefix activation path missing: ${exec_line}")
    endif()
    if(EXISTS "${TEST_ROOT}/build/tests")
        message(FATAL_ERROR "BUILD_TESTING=OFF unexpectedly configured the test tree")
    endif()
else()
    message(FATAL_ERROR "unknown MODE: ${MODE}")
endif()
