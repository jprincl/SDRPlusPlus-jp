include(FindPackageHandleStandardArgs)

get_filename_component(_pthreads4w_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_pthreads4w_lib_names pthreadVC3d pthreadVC3)
    set(_pthreads4w_dll_names pthreadVC3d.dll pthreadVC3.dll)
else ()
    set(_pthreads4w_lib_names pthreadVC3 pthreadVC3d)
    set(_pthreads4w_dll_names pthreadVC3.dll pthreadVC3d.dll)
endif ()

find_path(pthreads4w_INCLUDE_DIR
    NAMES pthread.h
    HINTS "${_pthreads4w_prefix}/include"
    NO_DEFAULT_PATH)

find_library(pthreads4w_PTHREADVC3_LIBRARY
    NAMES ${_pthreads4w_lib_names}
    HINTS "${_pthreads4w_prefix}/lib"
    NO_DEFAULT_PATH)

find_file(pthreads4w_PTHREADVC3_DLL
    NAMES ${_pthreads4w_dll_names}
    HINTS "${_pthreads4w_prefix}/bin"
    NO_DEFAULT_PATH)

if (NOT CMAKE_FIND_PACKAGE_NAME)
    set(CMAKE_FIND_PACKAGE_NAME pthreads4w)
endif ()

find_package_handle_standard_args(${CMAKE_FIND_PACKAGE_NAME}
    REQUIRED_VARS pthreads4w_INCLUDE_DIR pthreads4w_PTHREADVC3_LIBRARY)

if (${CMAKE_FIND_PACKAGE_NAME}_FOUND AND NOT TARGET pthreads4w::pthreadVC3)
    if (pthreads4w_PTHREADVC3_DLL)
        add_library(pthreads4w::pthreadVC3 SHARED IMPORTED)
        set_target_properties(pthreads4w::pthreadVC3 PROPERTIES
            IMPORTED_LOCATION "${pthreads4w_PTHREADVC3_DLL}"
            IMPORTED_IMPLIB "${pthreads4w_PTHREADVC3_LIBRARY}"
            INTERFACE_COMPILE_DEFINITIONS "__PTW32_CLEANUP_C")
    else ()
        add_library(pthreads4w::pthreadVC3 UNKNOWN IMPORTED)
        set_target_properties(pthreads4w::pthreadVC3 PROPERTIES
            IMPORTED_LOCATION "${pthreads4w_PTHREADVC3_LIBRARY}"
            INTERFACE_COMPILE_DEFINITIONS "__PTW32_CLEANUP_C;__PTW32_STATIC_LIB")
    endif ()

    set_target_properties(pthreads4w::pthreadVC3 PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${pthreads4w_INCLUDE_DIR}"
        IMPORTED_LINK_INTERFACE_LANGUAGES "C")
endif ()

if (${CMAKE_FIND_PACKAGE_NAME}_FOUND AND NOT TARGET PThreads4W::PThreads4W)
    add_library(PThreads4W::PThreads4W INTERFACE IMPORTED)
    set_target_properties(PThreads4W::PThreads4W PROPERTIES
        INTERFACE_LINK_LIBRARIES pthreads4w::pthreadVC3)
endif ()
