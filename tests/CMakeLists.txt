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

find_program(TEST_BASH_EXECUTABLE NAMES bash REQUIRED)
add_test(NAME ci_no_tests_error
    COMMAND ${TEST_BASH_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_ci_no_tests_error.sh
        ${PROJECT_SOURCE_DIR}/scripts/ci.sh
)
