# SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
# SPDX-License-Identifier: 0BSD

foreach(required ESCAPE_HELPER SERVICE_TEMPLATE OUTPUT_FILE)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "missing required argument: ${required}")
    endif()
endforeach()
if(NOT DEFINED ENV{ACTIVATION_EXECUTABLE_PATH}
   OR "$ENV{ACTIVATION_EXECUTABLE_PATH}" STREQUAL "")
    message(FATAL_ERROR "ACTIVATION_EXECUTABLE_PATH is required")
endif()

include("${ESCAPE_HELPER}")
set(REMMINA_KRUNNER_EXECUTABLE "$ENV{ACTIVATION_EXECUTABLE_PATH}")
remmina_escape_dbus_exec_argument(
    REMMINA_KRUNNER_DBUS_EXEC "${REMMINA_KRUNNER_EXECUTABLE}")
configure_file("${SERVICE_TEMPLATE}" "${OUTPUT_FILE}" @ONLY)
