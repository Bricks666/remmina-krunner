# SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
# SPDX-License-Identifier: 0BSD

get_target_property(remmina_krunner_core_pic remmina_krunner_core POSITION_INDEPENDENT_CODE)
if(NOT remmina_krunner_core_pic)
    message(FATAL_ERROR "remmina_krunner_core must be position-independent")
endif()

add_executable(test_smoke test_smoke.cpp)
target_link_libraries(test_smoke PRIVATE Qt6::Test)

add_test(NAME smoke COMMAND test_smoke)

add_executable(test_matcher test_matcher.cpp)
target_link_libraries(test_matcher PRIVATE remmina_krunner_core Qt6::Test)

add_test(NAME matcher COMMAND test_matcher)

add_executable(test_selection_policy test_selection_policy.cpp)
target_link_libraries(test_selection_policy PRIVATE remmina_krunner_core Qt6::Test)

add_test(NAME selection_policy COMMAND test_selection_policy)

add_executable(test_instance_scanner test_instance_scanner.cpp)
target_link_libraries(test_instance_scanner PRIVATE remmina_krunner_core Qt6::Test)

add_test(NAME instance_scanner COMMAND test_instance_scanner)

add_executable(process_probe_helper process_probe_helper.cpp)

add_executable(test_qt_process_probe test_qt_process_probe.cpp)
target_compile_definitions(test_qt_process_probe
    PRIVATE PROCESS_PROBE_HELPER_PATH="$<TARGET_FILE:process_probe_helper>"
)
target_link_libraries(test_qt_process_probe PRIVATE remmina_krunner_core Qt6::Test)
add_dependencies(test_qt_process_probe process_probe_helper)

add_test(NAME qt_process_probe COMMAND test_qt_process_probe)

find_program(TEST_BASH_EXECUTABLE NAMES bash REQUIRED)
add_test(NAME ci_no_tests_error
    COMMAND ${TEST_BASH_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_ci_no_tests_error.sh
        ${PROJECT_SOURCE_DIR}/scripts/ci.sh
)
