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
