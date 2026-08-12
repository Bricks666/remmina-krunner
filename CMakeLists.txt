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

if(KDE_INSTALL_PLUGINDIR STREQUAL "" OR IS_ABSOLUTE "${KDE_INSTALL_PLUGINDIR}")
    message(FATAL_ERROR "KDE_INSTALL_PLUGINDIR must be a nonempty relative path")
endif()
cmake_path(NORMAL_PATH KDE_INSTALL_PLUGINDIR
    OUTPUT_VARIABLE REMMINA_KRUNNER_NORMALIZED_PLUGINDIR)
if(NOT REMMINA_KRUNNER_NORMALIZED_PLUGINDIR STREQUAL KDE_INSTALL_PLUGINDIR)
    message(FATAL_ERROR "KDE_INSTALL_PLUGINDIR must be normalized")
endif()
set(REMMINA_KRUNNER_PLUGIN_RELATIVE
    "${KDE_INSTALL_PLUGINDIR}/kf6/krunner/kcms/kcm_remmina_krunner.so")
cmake_path(NORMAL_PATH REMMINA_KRUNNER_PLUGIN_RELATIVE
    OUTPUT_VARIABLE REMMINA_KRUNNER_NORMALIZED_PLUGIN_RELATIVE)
if(NOT REMMINA_KRUNNER_NORMALIZED_PLUGIN_RELATIVE STREQUAL
       REMMINA_KRUNNER_PLUGIN_RELATIVE)
    message(FATAL_ERROR "configured KRunner plugin path must be normalized")
endif()
configure_file(cmake/RemminaKRunnerInstallLayout.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/RemminaKRunnerInstallLayout.cmake @ONLY)

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

add_library(remmina_krunner_application STATIC src/application.cpp)
set_target_properties(remmina_krunner_application PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
target_include_directories(remmina_krunner_application
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(remmina_krunner_application PUBLIC remmina_krunner_core)

add_executable(remmina-krunner src/main.cpp)
target_link_libraries(remmina-krunner
    PRIVATE remmina_krunner_application remmina_krunner_core Qt6::DBus
)

include(cmake/EscapeDBusExec.cmake)
set(REMMINA_KRUNNER_EXECUTABLE
    "${CMAKE_INSTALL_FULL_BINDIR}/remmina-krunner"
)
remmina_escape_dbus_exec_argument(
    REMMINA_KRUNNER_DBUS_EXEC "${REMMINA_KRUNNER_EXECUTABLE}")
configure_file(
    data/org.remminakrunner.KRunner.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/org.remminakrunner.KRunner.desktop
    @ONLY
)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/packaging)
configure_file(packaging/install.sh
    ${CMAKE_CURRENT_BINARY_DIR}/packaging/install.sh @ONLY)
configure_file(packaging/uninstall.sh
    ${CMAKE_CURRENT_BINARY_DIR}/packaging/uninstall.sh @ONLY)
configure_file(
    data/org.remminakrunner.KRunner.service.in
    ${CMAKE_CURRENT_BINARY_DIR}/org.remminakrunner.KRunner.service
    @ONLY
)

install(TARGETS remmina-krunner RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.remminakrunner.KRunner.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/krunner/dbusplugins
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.remminakrunner.KRunner.service
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/dbus-1/services
)
install(PROGRAMS
    ${CMAKE_CURRENT_BINARY_DIR}/packaging/install.sh
    ${CMAKE_CURRENT_BINARY_DIR}/packaging/uninstall.sh
    DESTINATION .
)
install(FILES LICENSE DESTINATION .)
install(FILES LICENSES/0BSD.txt LICENSES/LGPL-2.0-or-later.txt
    DESTINATION LICENSES
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
