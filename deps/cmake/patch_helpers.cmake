#
# patch_replace_or_fail(<var> <needle> <replacement>)
#
# Read variable <var> from the caller's scope, replace the literal string
# <needle> with <replacement>, and write back via PARENT_SCOPE.
#
# Errors out (FATAL_ERROR) when neither <needle> nor <replacement> is found
# in <var>. This catches upstream shape changes that would otherwise let the
# underlying string(REPLACE) silently no-op, leaving the build to fail much
# later in a confusing way.
#
# Idempotent: if <replacement> is already present (from an earlier run on
# the same source tree), the call returns silently — re-patching is a no-op.
#
#
# patch_apply_git_or_fail(<src-dir> <patch-file>)
#
# Apply <patch-file> inside <src-dir> with `git apply`. Used for changes kept
# as proper git patches (upstream-PR material) rather than string edits.
#
# Idempotent: if the patch is already applied (reverse-check succeeds), the
# call is a no-op. Errors out (FATAL_ERROR) when the patch neither applies
# nor is already applied — the upstream shape changed.
#
function(patch_apply_git_or_fail src patch)
    find_program(_git git REQUIRED)
    execute_process(
        COMMAND "${_git}" apply --check "${patch}"
        WORKING_DIRECTORY "${src}"
        RESULT_VARIABLE _check_rc
        ERROR_VARIABLE _check_err
    )
    if (_check_rc EQUAL 0)
        execute_process(
            COMMAND "${_git}" apply "${patch}"
            WORKING_DIRECTORY "${src}"
            RESULT_VARIABLE _apply_rc
            ERROR_VARIABLE _apply_err
        )
        if (NOT _apply_rc EQUAL 0)
            message(FATAL_ERROR "git apply ${patch} failed:\n${_apply_err}")
        endif ()
        message(STATUS "Applied ${patch}")
    else ()
        execute_process(
            COMMAND "${_git}" apply --reverse --check "${patch}"
            WORKING_DIRECTORY "${src}"
            RESULT_VARIABLE _reverse_rc
            ERROR_QUIET
        )
        if (_reverse_rc EQUAL 0)
            message(STATUS "Already applied: ${patch}")
        else ()
            message(FATAL_ERROR
                "${patch} neither applies nor is already applied — upstream shape changed:\n${_check_err}")
        endif ()
    endif ()
endfunction()

function(patch_replace_or_fail var needle replacement)
    set(_content "${${var}}")
    string(FIND "${_content}" "${needle}" _nidx)
    if (_nidx LESS 0)
        string(FIND "${_content}" "${replacement}" _ridx)
        if (_ridx GREATER_EQUAL 0)
            return()
        endif ()
        message(FATAL_ERROR
            "patch_replace_or_fail: expected text not found in ${var}:\n  ${needle}")
    endif ()
    string(REPLACE "${needle}" "${replacement}" _new "${_content}")
    set(${var} "${_new}" PARENT_SCOPE)
endfunction()
