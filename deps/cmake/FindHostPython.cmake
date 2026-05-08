#
# FindHostPython.cmake — locate the *host* Python 3 interpreter.
#
# Needed when cross-compiling (e.g. for Android): the toolchain redirects
# find_package(Python3) into the sysroot, and the SDK's CMake subprocess
# may not inherit the full user PATH.  This module searches in host-only
# locations and sets HOST_PYTHON_EXECUTABLE as a cache variable.
#
# Result variable:
#   HOST_PYTHON_EXECUTABLE  — absolute path to the interpreter, or NOTFOUND.
#
# The caller can pre-set HOST_PYTHON_EXECUTABLE to skip detection entirely.
#
# Detection order (Windows):
#   1. Glob well-known install dirs (python.org per-user/system + MS Store
#      WindowsApps App Execution Aliases, invisible to find_program).
#   2. py.exe launcher  — registry-based, works without PATH.
#   3. C:\Windows\System32\where.exe (full path, always present).
#   4. find_program with NO_CMAKE_FIND_ROOT_PATH (cross-platform fallback).
#

if(HOST_PYTHON_EXECUTABLE)
    return()
endif()

if(CMAKE_HOST_WIN32)
    # 1. Glob known install locations.
    file(GLOB _hpy_candidates
        "$ENV{LOCALAPPDATA}/Microsoft/WindowsApps/python3*.exe"
        "$ENV{LOCALAPPDATA}/Microsoft/WindowsApps/python.exe"
        "$ENV{LOCALAPPDATA}/Programs/Python/Python3*/python.exe"
        "$ENV{ProgramFiles}/Python3*/python.exe"
        "$ENV{ProgramW6432}/Python3*/python.exe")
    list(SORT _hpy_candidates ORDER DESCENDING)  # highest version first
    if(_hpy_candidates)
        list(GET _hpy_candidates 0 _hpy_first)
        set(HOST_PYTHON_EXECUTABLE "${_hpy_first}" CACHE FILEPATH
            "Host Python 3 interpreter")
    endif()
    unset(_hpy_candidates)
    unset(_hpy_first)
endif()

if(CMAKE_HOST_WIN32 AND NOT HOST_PYTHON_EXECUTABLE)
    # 2. py.exe launcher — present with python.org installs, registry-based.
    find_program(_hpy_launcher NAMES py NO_CMAKE_FIND_ROOT_PATH)
    if(_hpy_launcher)
        execute_process(
            COMMAND "${_hpy_launcher}" -3 -c "import sys; print(sys.executable)"
            OUTPUT_VARIABLE _hpy_from_launcher
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE _hpy_launcher_rc)
        if(_hpy_launcher_rc EQUAL 0 AND _hpy_from_launcher)
            set(HOST_PYTHON_EXECUTABLE "${_hpy_from_launcher}" CACHE FILEPATH
                "Host Python 3 interpreter")
        endif()
    endif()
    unset(_hpy_launcher)
    unset(_hpy_from_launcher)
    unset(_hpy_launcher_rc)
endif()

if(CMAKE_HOST_WIN32 AND NOT HOST_PYTHON_EXECUTABLE)
    # 3. where.exe with full path — resolves App Execution Aliases even when
    #    System32 is not in the subprocess PATH.
    execute_process(
        COMMAND "C:/Windows/System32/where.exe" python
        OUTPUT_VARIABLE _hpy_where_out
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _hpy_where_rc)
    if(_hpy_where_rc EQUAL 0 AND _hpy_where_out)
        string(REGEX REPLACE "\r?\n.*" "" _hpy_where_first "${_hpy_where_out}")
        string(STRIP "${_hpy_where_first}" _hpy_where_first)
        if(_hpy_where_first)
            set(HOST_PYTHON_EXECUTABLE "${_hpy_where_first}" CACHE FILEPATH
                "Host Python 3 interpreter")
        endif()
    endif()
    unset(_hpy_where_out)
    unset(_hpy_where_rc)
    unset(_hpy_where_first)
endif()

# 4. Generic fallback — also the primary path on Linux/macOS.
find_program(HOST_PYTHON_EXECUTABLE NAMES python3 python
    NO_CMAKE_FIND_ROOT_PATH
    DOC "Host Python 3 interpreter")

if(NOT HOST_PYTHON_EXECUTABLE)
    message(FATAL_ERROR
        "FindHostPython: could not find a host Python 3 interpreter.\n"
        "Install Python 3 (https://python.org) and re-run CMake, or pass "
        "-DHOST_PYTHON_EXECUTABLE=<path/to/python>.")
endif()

message(STATUS "Host Python: ${HOST_PYTHON_EXECUTABLE}")
