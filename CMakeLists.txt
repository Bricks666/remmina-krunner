# SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
# SPDX-License-Identifier: 0BSD

cmake_minimum_required(VERSION 3.25)
project(remmina-krunner VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
option(BUILD_SHARED_LIBS "Build loadable plugins as shared modules" ON)

find_package(ECM REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

set(QT_MAJOR_VERSION 6)
include(KDEInstallDirs6)

include(CTest)

find_package(Qt6 6.7 REQUIRED COMPONENTS Core DBus Widgets)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
find_package(KF6Config REQUIRED)
find_package(KF6CoreAddons REQUIRED)
find_package(KF6I18n REQUIRED)
find_package(KF6KCMUtils REQUIRED)

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
    PUBLIC Qt6::Core Qt6::DBus
    PRIVATE KF6::ConfigCore
)

add_library(remmina_krunner_kcm STATIC
    src/kcm/instance_settings_model.cpp
    src/kcm/remmina_runner_config.cpp
    src/kcm/remmina_runner_config.ui
)
set_target_properties(remmina_krunner_kcm PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(remmina_krunner_kcm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(remmina_krunner_kcm
    PRIVATE TRANSLATION_DOMAIN="remmina-krunner"
)
target_link_libraries(remmina_krunner_kcm
    PUBLIC remmina_krunner_core KF6::KCMUtils Qt6::Widgets
    PRIVATE KF6::ConfigCore KF6::CoreAddons KF6::I18n
)

kcoreaddons_add_plugin(kcm_remmina_krunner
    SOURCES src/kcm/remmina_runner_config_plugin.cpp
    INSTALL_NAMESPACE "kf6/krunner/kcms"
)
set_target_properties(kcm_remmina_krunner PROPERTIES PREFIX "")
target_link_libraries(kcm_remmina_krunner PRIVATE remmina_krunner_kcm)

if(BUILD_TESTING)
    find_package(Qt6 6.7 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
