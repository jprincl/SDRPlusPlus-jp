#
# Run with -DSRC=<source-dir> -P this-script.
#

if (NOT DEFINED SRC)
    message(FATAL_ERROR "patch_codec2: SRC is not set")
endif ()

find_package(Git REQUIRED)

set(_codec2_patches
    "${CMAKE_CURRENT_LIST_DIR}/patches/0001-sdrpp-windows-clangcl-build.patch")

foreach (_patch IN LISTS _codec2_patches)
    if (NOT EXISTS "${_patch}")
        message(FATAL_ERROR "patch_codec2: missing patch ${_patch}")
    endif ()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${SRC}" apply
            --check --ignore-whitespace --whitespace=nowarn "${_patch}"
        RESULT_VARIABLE _check_result
        OUTPUT_VARIABLE _check_output
        ERROR_VARIABLE _check_error)

    if (_check_result EQUAL 0)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${SRC}" apply
                --ignore-whitespace --whitespace=nowarn "${_patch}"
            RESULT_VARIABLE _apply_result
            OUTPUT_VARIABLE _apply_output
            ERROR_VARIABLE _apply_error)
        if (NOT _apply_result EQUAL 0)
            message(FATAL_ERROR
                "patch_codec2: failed applying ${_patch}\n"
                "${_apply_output}${_apply_error}")
        endif ()
        message(STATUS "patch_codec2: applied ${_patch}")
    else ()
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${SRC}" apply
                --check --reverse --ignore-whitespace --whitespace=nowarn "${_patch}"
            RESULT_VARIABLE _reverse_check_result
            OUTPUT_VARIABLE _reverse_check_output
            ERROR_VARIABLE _reverse_check_error)

        if (_reverse_check_result EQUAL 0)
            message(STATUS "patch_codec2: ${_patch} already applied")
        else ()
            message(FATAL_ERROR
                "patch_codec2: ${_patch} does not apply cleanly\n"
                "Forward check:\n${_check_output}${_check_error}\n"
                "Reverse check:\n${_reverse_check_output}${_reverse_check_error}")
        endif ()
    endif ()
endforeach ()
