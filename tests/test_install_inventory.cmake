# SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
# SPDX-License-Identifier: 0BSD

if(NOT DEFINED MODE OR NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "MODE and SOURCE_DIR are required")
endif()

function(assert_activation root prefix)
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

function(run_inventory_validator root expect_success)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            -DSTAGE_ROOT=${root}
            -DINSTALL_PREFIX_RELATIVE=${relative_prefix}
            -DLAYOUT_FILE=${LAYOUT_FILE}
            -P ${VALIDATOR}
        RESULT_VARIABLE validation_status
        OUTPUT_VARIABLE validation_output
        ERROR_VARIABLE validation_error)
    if(expect_success AND NOT validation_status EQUAL 0)
        message(FATAL_ERROR
            "exact install inventory validation failed:\n${validation_output}\n${validation_error}")
    elseif(NOT expect_success AND validation_status EQUAL 0)
        message(FATAL_ERROR "tampered install inventory was accepted")
    endif()
endfunction()

if(MODE STREQUAL "inventory")
    foreach(required BUILD_DIR INSTALL_PREFIX LAYOUT_FILE VALIDATOR)
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
    run_inventory_validator("${stage}" TRUE)
    assert_activation("${stage}" "${relative_prefix}")

    file(MAKE_DIRECTORY "${stage}/${relative_prefix}/unexpected-empty-directory")
    run_inventory_validator("${stage}" FALSE)
    file(REMOVE_RECURSE "${stage}/${relative_prefix}/unexpected-empty-directory")

    execute_process(COMMAND "${CMAKE_COMMAND}" -E create_symlink
        "${stage}/${relative_prefix}/LICENSE"
        "${stage}/${relative_prefix}/unexpected-symlink")
    run_inventory_validator("${stage}" FALSE)
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
