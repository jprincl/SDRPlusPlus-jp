#
# Run with -DSRC=<source-dir> -P this-script.
#
# LimeSuite v23.11.0 uses std::chrono::high_resolution_clock without
# #include <chrono> in several files. Older MSVC headers leaked the include
# transitively; VS 2026's stricter headers do not. Sweep src/ for any file
# that uses std::chrono:: but doesn't #include <chrono>, and prepend it.
#
file(GLOB_RECURSE _files
    "${SRC}/src/*.cpp"
    "${SRC}/src/*.h"
    "${SRC}/src/*.hpp")

set(_patched_count 0)
foreach (_f ${_files})
    file(READ "${_f}" _c)
    if (_c MATCHES "std::chrono::" AND NOT _c MATCHES "#include[ \t]*<chrono>")
        file(WRITE "${_f}" "#include <chrono>\n${_c}")
        math(EXPR _patched_count "${_patched_count} + 1")
    endif ()
endforeach ()
message(STATUS "patch_limesuite: added #include <chrono> to ${_patched_count} files")
