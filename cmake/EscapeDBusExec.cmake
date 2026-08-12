# SPDX-FileCopyrightText: 2026 Remmina KRunner contributors
# SPDX-License-Identifier: 0BSD

function(remmina_escape_dbus_exec_argument output value)
    if(NOT IS_ABSOLUTE "${value}")
        message(FATAL_ERROR "D-Bus activation executable must be absolute: ${value}")
    endif()
    set(escaped "${value}")
    # The service keyfile parser consumes one escaping layer before the D-Bus
    # Exec parser consumes the second. Dollar signs and backticks are literals.
    string(REPLACE [[\]] [[\\\\]] escaped "${escaped}")
    string(REPLACE [["]] [[\\"]] escaped "${escaped}")
    set(${output} "\"${escaped}\"" PARENT_SCOPE)
endfunction()
