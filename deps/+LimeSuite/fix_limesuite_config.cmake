#
# LimeSuite v23.11.0 installs a Config.cmake that creates a SHARED IMPORTED
# target with IMPORTED_LOCATION pointing at the .lib import library. Visual
# Studio generators need IMPORTED_IMPLIB=<lib> and IMPORTED_LOCATION=<dll>.
#
if (NOT DEFINED ROOT)
    message(FATAL_ERROR "fix_limesuite_config.cmake requires -DROOT=<install-prefix>")
endif ()

set(_config "${ROOT}/lib/cmake/LimeSuite/LimeSuiteConfig.cmake")
if (NOT EXISTS "${_config}")
    message(FATAL_ERROR "LimeSuiteConfig.cmake not found: ${_config}")
endif ()

set(_content [=[
if(DEFINED INCLUDED_LIMESUITE_CONFIG_CMAKE)
  return()
endif()
set(INCLUDED_LIMESUITE_CONFIG_CMAKE TRUE)

get_filename_component(LIMESUITE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
if (NOT EXISTS "${LIMESUITE_ROOT}/include" AND EXISTS "${LIMESUITE_ROOT}/../include")
    get_filename_component(LIMESUITE_ROOT "${LIMESUITE_ROOT}/.." ABSOLUTE)
endif ()

find_library(LIMESUITE_LIBRARY LimeSuite
  PATHS "${LIMESUITE_ROOT}/lib${LIB_SUFFIX}"
  PATH_SUFFIXES ${CMAKE_LIBRARY_ARCHITECTURE}
  NO_DEFAULT_PATH)
if(NOT LIMESUITE_LIBRARY)
  message(FATAL_ERROR "cannot find LimeSuite import library in ${LIMESUITE_ROOT}/lib${LIB_SUFFIX}")
endif()

find_file(LIMESUITE_RUNTIME LimeSuite.dll
  PATHS "${LIMESUITE_ROOT}/bin"
  NO_DEFAULT_PATH)
if(WIN32 AND NOT LIMESUITE_RUNTIME)
  message(FATAL_ERROR "cannot find LimeSuite runtime in ${LIMESUITE_ROOT}/bin")
endif()

find_path(LIMESUITE_INCLUDE_DIR lime/lms7_device.h
  PATHS "${LIMESUITE_ROOT}/include"
  NO_DEFAULT_PATH)
if(NOT LIMESUITE_INCLUDE_DIR)
  message(FATAL_ERROR "cannot find LimeSuite includes in ${LIMESUITE_ROOT}/include")
endif()

set(LimeSuite_LIBRARIES ${LIMESUITE_LIBRARY})
set(LimeSuite_INCLUDE_DIRS ${LIMESUITE_INCLUDE_DIR})

if(NOT TARGET LimeSuite)
  add_library(LimeSuite SHARED IMPORTED)
  set_property(TARGET LimeSuite PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${LIMESUITE_INCLUDE_DIR}")
  if(WIN32)
    set_property(TARGET LimeSuite PROPERTY IMPORTED_IMPLIB "${LIMESUITE_LIBRARY}")
    set_property(TARGET LimeSuite PROPERTY IMPORTED_LOCATION "${LIMESUITE_RUNTIME}")
  else()
    set_property(TARGET LimeSuite PROPERTY IMPORTED_LOCATION "${LIMESUITE_LIBRARY}")
  endif()
endif()

if(NOT TARGET LimeSuite::LimeSuite)
  add_library(LimeSuite::LimeSuite INTERFACE IMPORTED)
  set_property(TARGET LimeSuite::LimeSuite PROPERTY INTERFACE_LINK_LIBRARIES LimeSuite)
  set_property(TARGET LimeSuite::LimeSuite PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${LIMESUITE_INCLUDE_DIR}")
endif()
]=])

file(WRITE "${_config}" "${_content}")
message(STATUS "Rewrote ${_config}")
