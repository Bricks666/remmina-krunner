# SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
# SPDX-License-Identifier: 0BSD

foreach(required SOURCE_DIR TEST_ROOT PACKAGE_TEST RUNNER_HELPER)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${TEST_ROOT}")
set(debian_plugin_dir "lib/x86_64-linux-gnu/plugins")
set(debian_plugin_relative
    "${debian_plugin_dir}/kf6/krunner/kcms/kcm_remmina_krunner.so")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}" -B "${TEST_ROOT}/debian"
            -G Ninja -DBUILD_TESTING=OFF
            "-DKDE_INSTALL_PLUGINDIR=${debian_plugin_dir}"
    RESULT_VARIABLE configure_status
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error)
if(NOT configure_status EQUAL 0)
    message(FATAL_ERROR "Debian-like layout configure failed:\n${configure_output}\n${configure_error}")
endif()
execute_process(
    COMMAND bash "${PACKAGE_TEST}"
            "${TEST_ROOT}/debian/packaging/install.sh"
            "${TEST_ROOT}/debian/packaging/uninstall.sh"
            "${RUNNER_HELPER}"
            "${debian_plugin_relative}"
    RESULT_VARIABLE package_status
    OUTPUT_VARIABLE package_output
    ERROR_VARIABLE package_error)
if(NOT package_status EQUAL 0)
    message(FATAL_ERROR "Debian-like package scripts failed:\n${package_output}\n${package_error}")
endif()

foreach(invalid_plugin_dir IN ITEMS "/absolute/plugins" "lib/plugins/../escape" ".")
    string(MAKE_C_IDENTIFIER "${invalid_plugin_dir}" invalid_id)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}"
                -B "${TEST_ROOT}/invalid-${invalid_id}" -G Ninja
                -DBUILD_TESTING=OFF
                "-DKDE_INSTALL_PLUGINDIR=${invalid_plugin_dir}"
        RESULT_VARIABLE invalid_status
        OUTPUT_QUIET ERROR_QUIET)
    if(invalid_status EQUAL 0)
        message(FATAL_ERROR "invalid KDE plugin directory was accepted: ${invalid_plugin_dir}")
    endif()
endforeach()
