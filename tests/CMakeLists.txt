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

add_executable(test_key_file_reader test_key_file_reader.cpp)
target_link_libraries(test_key_file_reader PRIVATE remmina_krunner_core Qt6::Test)

add_test(NAME key_file_reader COMMAND test_key_file_reader)

add_executable(test_profile_parser test_profile_parser.cpp)
target_link_libraries(test_profile_parser PRIVATE remmina_krunner_core Qt6::Test)

add_test(NAME profile_parser COMMAND test_profile_parser)

add_executable(test_profile_locator test_profile_locator.cpp)
target_link_libraries(test_profile_locator PRIVATE remmina_krunner_core Qt6::Test)

add_test(NAME profile_locator COMMAND test_profile_locator)

add_executable(test_profile_repository test_profile_repository.cpp)
target_link_libraries(test_profile_repository PRIVATE remmina_krunner_core Qt6::Test)

add_test(NAME profile_repository COMMAND test_profile_repository)

add_executable(test_profile_catalog test_profile_catalog.cpp)
target_link_libraries(test_profile_catalog PRIVATE remmina_krunner_core Qt6::Test)

add_test(NAME profile_catalog COMMAND test_profile_catalog)

add_executable(test_qt_profile_watcher test_qt_profile_watcher.cpp)
target_link_libraries(test_qt_profile_watcher PRIVATE remmina_krunner_core Qt6::Test)

add_test(NAME qt_profile_watcher COMMAND test_qt_profile_watcher)

add_executable(test_selection_policy test_selection_policy.cpp)
target_link_libraries(test_selection_policy PRIVATE remmina_krunner_core Qt6::Test)

add_test(NAME selection_policy COMMAND test_selection_policy)

add_executable(test_selection_store test_selection_store.cpp)
target_link_libraries(test_selection_store
    PRIVATE remmina_krunner_core KF6::ConfigCore Qt6::Test
)

add_test(NAME selection_store COMMAND test_selection_store)

add_executable(test_instance_scanner test_instance_scanner.cpp)
target_link_libraries(test_instance_scanner PRIVATE remmina_krunner_core Qt6::Test)

add_test(NAME instance_scanner COMMAND test_instance_scanner)

add_executable(test_instance_registry test_instance_registry.cpp)
target_link_libraries(test_instance_registry PRIVATE remmina_krunner_core Qt6::Test)

add_test(NAME instance_registry COMMAND test_instance_registry)

add_executable(process_probe_helper process_probe_helper.cpp)

add_executable(test_qt_process_probe test_qt_process_probe.cpp)
target_compile_definitions(test_qt_process_probe
    PRIVATE PROCESS_PROBE_HELPER_PATH="$<TARGET_FILE:process_probe_helper>"
)
target_link_libraries(test_qt_process_probe PRIVATE remmina_krunner_core Qt6::Test)
add_dependencies(test_qt_process_probe process_probe_helper)

add_test(NAME qt_process_probe COMMAND test_qt_process_probe)

add_executable(test_remmina_launcher test_remmina_launcher.cpp)
target_link_libraries(test_remmina_launcher
    PRIVATE remmina_krunner_core Qt6::DBus Qt6::Test
)

add_test(NAME remmina_launcher COMMAND test_remmina_launcher)

find_package(KF6Runner REQUIRED)
add_executable(test_dbus_types_kf6 test_dbus_types_kf6.cpp)
target_link_libraries(test_dbus_types_kf6
    PRIVATE remmina_krunner_core KF6::Runner
)

add_test(NAME dbus_types_kf6 COMMAND test_dbus_types_kf6)

add_executable(test_dbus_contract test_dbus_contract.cpp)
target_compile_definitions(test_dbus_contract
    PRIVATE KRUNNER_INTERFACE_XML="${PROJECT_SOURCE_DIR}/data/org.kde.krunner1.xml"
)
target_link_libraries(test_dbus_contract
    PRIVATE remmina_krunner_core Qt6::DBus Qt6::Test
)

find_program(DBUS_RUN_SESSION_EXECUTABLE NAMES dbus-run-session REQUIRED)
add_test(NAME dbus_contract
    COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- $<TARGET_FILE:test_dbus_contract>
)

add_executable(test_runner_service test_runner_service.cpp)
target_link_libraries(test_runner_service
    PRIVATE remmina_krunner_core Qt6::DBus Qt6::Test
)

add_test(NAME runner_service
    COMMAND ${DBUS_RUN_SESSION_EXECUTABLE} -- $<TARGET_FILE:test_runner_service>
)
set_tests_properties(dbus_contract runner_service PROPERTIES TIMEOUT 15)

add_executable(process_launcher_helper process_launcher_helper.cpp)
target_link_libraries(process_launcher_helper PRIVATE Qt6::Core)

add_executable(test_qt_process_launcher test_qt_process_launcher.cpp)
target_compile_definitions(test_qt_process_launcher
    PRIVATE PROCESS_LAUNCHER_HELPER_PATH="$<TARGET_FILE:process_launcher_helper>"
)
target_link_libraries(test_qt_process_launcher
    PRIVATE remmina_krunner_core Qt6::Test
)
add_dependencies(test_qt_process_launcher process_launcher_helper)

add_test(NAME qt_process_launcher COMMAND test_qt_process_launcher)

find_program(TEST_BASH_EXECUTABLE NAMES bash REQUIRED)
add_test(NAME ci_no_tests_error
    COMMAND ${TEST_BASH_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_ci_no_tests_error.sh
        ${PROJECT_SOURCE_DIR}/scripts/ci.sh
)
