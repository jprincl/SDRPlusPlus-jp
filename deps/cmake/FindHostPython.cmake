#
# FindHostPython.cmake — locate the *host* Python 3 interpreter.
#

if(HOST_PYTHON_EXECUTABLE)
    return()
endif()

function(_hpy_accept_if_valid _exe)
    if(NOT _exe OR NOT EXISTS "${_exe}")
        return()
    endif()

    execute_process(
        COMMAND "${_exe}" -c "import sys; print(sys.version_info[0]); print(sys.executable)"
        OUTPUT_VARIABLE _hpy_out
        ERROR_QUIET
        RESULT_VARIABLE _hpy_rc
        OUTPUT_STRIP_TRAILING_WHITESPACE)

    if(_hpy_rc EQUAL 0)
        string(REGEX MATCH "^3" _hpy_is_py3 "${_hpy_out}")
        if(_hpy_is_py3)
            set(HOST_PYTHON_EXECUTABLE "${_exe}" CACHE FILEPATH
                "Host Python 3 interpreter" FORCE)
        endif()
    endif()
endfunction()

if(CMAKE_HOST_WIN32)
    # 1. Glob real install locations.
    # Deliberately exclude:
    #   %LOCALAPPDATA%/Microsoft/WindowsApps/python*.exe
    # because those are often App Execution Alias stubs, not real Python.
    file(GLOB _hpy_candidates
        "$ENV{APPDATA}/Python/Python3*/python.exe"
        "$ENV{LOCALAPPDATA}/Programs/Python/Python3*/python.exe"
        "$ENV{ProgramFiles}/Python3*/python.exe"
        "$ENV{ProgramW6432}/Python3*/python.exe")

    list(SORT _hpy_candidates ORDER DESCENDING)

    foreach(_hpy_candidate IN LISTS _hpy_candidates)
        if(NOT HOST_PYTHON_EXECUTABLE)
            _hpy_accept_if_valid("${_hpy_candidate}")
        endif()
    endforeach()

    unset(_hpy_candidates)
    unset(_hpy_candidate)
endif()

if(CMAKE_HOST_WIN32 AND NOT HOST_PYTHON_EXECUTABLE)
    # 2. py.exe launcher — registry-based, works without PATH.
    find_program(_hpy_launcher NAMES py NO_CMAKE_FIND_ROOT_PATH)

    if(_hpy_launcher)
        execute_process(
            COMMAND "${_hpy_launcher}" -3 -c "import sys; print(sys.executable)"
            OUTPUT_VARIABLE _hpy_from_launcher
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE _hpy_launcher_rc)

        if(_hpy_launcher_rc EQUAL 0 AND _hpy_from_launcher)
            _hpy_accept_if_valid("${_hpy_from_launcher}")
        endif()
    endif()

    unset(_hpy_launcher)
    unset(_hpy_from_launcher)
    unset(_hpy_launcher_rc)
endif()

if(CMAKE_HOST_WIN32 AND NOT HOST_PYTHON_EXECUTABLE)
    # 3. where.exe fallback. Skip WindowsApps aliases.
    execute_process(
        COMMAND "C:/Windows/System32/where.exe" python
        OUTPUT_VARIABLE _hpy_where_out
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _hpy_where_rc)

    if(_hpy_where_rc EQUAL 0 AND _hpy_where_out)
        string(REPLACE "\r\n" "\n" _hpy_where_out "${_hpy_where_out}")
        string(REPLACE "\r" "\n" _hpy_where_out "${_hpy_where_out}")
        string(REPLACE "\n" ";" _hpy_where_list "${_hpy_where_out}")

        foreach(_hpy_where_candidate IN LISTS _hpy_where_list)
            string(STRIP "${_hpy_where_candidate}" _hpy_where_candidate)

            if(_hpy_where_candidate
               AND NOT _hpy_where_candidate MATCHES "/Microsoft/WindowsApps/python[0-9.]*\\.exe$")
                if(NOT HOST_PYTHON_EXECUTABLE)
                    _hpy_accept_if_valid("${_hpy_where_candidate}")
                endif()
            endif()
        endforeach()
    endif()

    unset(_hpy_where_out)
    unset(_hpy_where_rc)
    unset(_hpy_where_list)
    unset(_hpy_where_candidate)
endif()

# 4. Generic fallback — primary path on Linux/macOS.
if(NOT HOST_PYTHON_EXECUTABLE)
    find_program(_hpy_generic
        NAMES python3 python
        NO_CMAKE_FIND_ROOT_PATH)

    if(_hpy_generic)
        _hpy_accept_if_valid("${_hpy_generic}")
    endif()

    unset(_hpy_generic)
endif()

if(NOT HOST_PYTHON_EXECUTABLE)
    message(FATAL_ERROR
        "FindHostPython: could not find a real host Python 3 interpreter.\n"
        "Install Python 3 and re-run CMake, or pass "
        "-DHOST_PYTHON_EXECUTABLE=<path/to/python>.")
endif()

message(STATUS "Host Python: ${HOST_PYTHON_EXECUTABLE}")
