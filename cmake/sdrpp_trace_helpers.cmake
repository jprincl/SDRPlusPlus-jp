if (COMMAND sdrpp_wrap_traced_command)
    return()
endif ()

set(_SDRPP_TRACE_HELPERS_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(_SDRPP_TRACE_COMMAND_SCRIPT "${_SDRPP_TRACE_HELPERS_DIR}/sdrpp_trace_command.cmake")

function(sdrpp_init_cmake_trace_defaults default_log_dir)
    option(SDRPP_TRACE_CMAKE_INVOCATIONS "Trace nested CMake command entry/exit to a log and console" OFF)
    option(SDRPP_SERIALIZE_CMAKE_INVOCATIONS "Serialize orchestration-level nested CMake commands while leaving leaf compiles parallel" ON)

    if (NOT DEFINED SDRPP_CMAKE_TRACE_LOG OR "${SDRPP_CMAKE_TRACE_LOG}" STREQUAL "")
        set(SDRPP_CMAKE_TRACE_LOG "${default_log_dir}/sdrpp-cmake-invocations.log"
            CACHE FILEPATH "Trace log for nested CMake invocations" FORCE)
    else ()
        set(SDRPP_CMAKE_TRACE_LOG "${SDRPP_CMAKE_TRACE_LOG}"
            CACHE FILEPATH "Trace log for nested CMake invocations")
    endif ()
endfunction()

function(sdrpp_wrap_traced_command out_var label working_directory)
    set(_cmd ${ARGN})
    if (NOT _cmd)
        message(FATAL_ERROR "sdrpp_wrap_traced_command(${label}): no command")
    endif ()

    set(_trace_serialize ${SDRPP_SERIALIZE_CMAKE_INVOCATIONS})
    list(GET _cmd 0 _cmd_first)
    if ("${_cmd_first}" STREQUAL "NO_SERIALIZE")
        set(_trace_serialize OFF)
        list(REMOVE_AT _cmd 0)
    endif ()
    if (NOT _cmd)
        message(FATAL_ERROR "sdrpp_wrap_traced_command(${label}): no command after NO_SERIALIZE")
    endif ()

    if (SDRPP_TRACE_CMAKE_INVOCATIONS OR _trace_serialize)
        get_filename_component(_trace_dir "${SDRPP_CMAKE_TRACE_LOG}" DIRECTORY)
        if ("${_trace_dir}" STREQUAL "")
            set(_trace_dir "${CMAKE_BINARY_DIR}")
        endif ()
        set(_wrapped
            ${CMAKE_COMMAND}
            "-DSDRPP_TRACE_LABEL=${label}"
            "-DSDRPP_TRACE_WORKING_DIRECTORY=${working_directory}"
            "-DSDRPP_TRACE_ENABLED=${SDRPP_TRACE_CMAKE_INVOCATIONS}"
            "-DSDRPP_TRACE_SERIALIZE=${_trace_serialize}"
            "-DSDRPP_TRACE_LOG=${SDRPP_CMAKE_TRACE_LOG}"
            "-DSDRPP_TRACE_LOG_LOCK=${_trace_dir}/sdrpp-cmake-invocations.log.lock"
            "-DSDRPP_TRACE_RUN_LOCK=${_trace_dir}/sdrpp-cmake-invocations.run.lock"
            -P "${_SDRPP_TRACE_COMMAND_SCRIPT}"
            --
            ${_cmd})
    else ()
        set(_wrapped ${_cmd})
    endif ()

    set(${out_var} ${_wrapped} PARENT_SCOPE)
endfunction()
