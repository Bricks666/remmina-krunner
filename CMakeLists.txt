# SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
# SPDX-License-Identifier: 0BSD

cmake_minimum_required(VERSION 3.25)
project(remmina-krunner VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

include(CTest)

find_package(Qt6 6.7 REQUIRED COMPONENTS Core DBus)
find_package(KF6Config REQUIRED)

add_library(remmina_krunner_core STATIC
    src/core/instance_registry.cpp
    src/core/instance_scanner.cpp
    src/core/key_file_reader.cpp
    src/core/kconfig_selection_store.cpp
    src/core/matcher.cpp
    src/core/profile_catalog.cpp
    src/core/profile_locator.cpp
    src/core/profile_parser.cpp
    src/core/profile_repository.cpp
    src/core/remmina_launcher.cpp
    src/core/selection_policy.cpp
    src/dbus/runner_service.cpp
    src/platform/freedesktop_notifier.cpp
    src/platform/qt_process_launcher.cpp
    src/platform/qt_process_probe.cpp
    src/platform/qt_profile_watcher.cpp
)
set_target_properties(remmina_krunner_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(remmina_krunner_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(remmina_krunner_core
    PUBLIC Qt6::Core
    PRIVATE KF6::ConfigCore Qt6::DBus
)

if(BUILD_TESTING)
    find_package(Qt6 6.7 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
