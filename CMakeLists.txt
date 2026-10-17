cmake_minimum_required(VERSION 3.20)

project(svcheck LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT PACKAGES_DIR)
  set(PACKAGES_DIR ${CMAKE_SOURCE_DIR}/packages)
endif()
message(STATUS "svcheck: PACKAGES_DIR=${PACKAGES_DIR}")

#*****************************************************************************
#* slang (pinned through ivpm; svcheck-design.md S-5)
#*
#* The configure step must not reach the network (svcheck-plan PR-1):
#*  - fmt: slang FetchContent-declares it; we hand it the ivpm copy.
#*  - mimalloc: off for now. Revisit with the performance gate (P2.9), when it
#*    can be supplied the same way as fmt.
#*  - boost-unordered: slang falls back to its vendored single header.
#*****************************************************************************
set(FETCHCONTENT_SOURCE_DIR_FMT ${PACKAGES_DIR}/fmt CACHE PATH "" FORCE)
set(FETCHCONTENT_FULLY_DISCONNECTED ON CACHE BOOL "" FORCE)

# libsvcheck (svcheck-plan P2.6b) is a shared library, except on WASI,
# which has none. Then everything is position-independent, slang and fmt too.
if (CMAKE_SYSTEM_NAME STREQUAL "WASI")
  set(_svcheck_shared_default OFF)
else()
  set(_svcheck_shared_default ON)
endif()
option(SVCHECK_SHARED "Build libsvcheck as a shared library" ${_svcheck_shared_default})
if (SVCHECK_SHARED)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

set(SLANG_USE_MIMALLOC OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_TOOLS OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_INSTALL OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_PYLIB OFF CACHE BOOL "" FORCE)

add_subdirectory(${PACKAGES_DIR}/slang ${CMAKE_BINARY_DIR}/slang EXCLUDE_FROM_ALL)

#*****************************************************************************
#* The rule engine (svcheck-plan P1.1): no slang include path.
#*****************************************************************************
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_subdirectory(src/engine)
add_subdirectory(src/ast)
add_subdirectory(src/lower)
add_subdirectory(src/host)

#*****************************************************************************
#* svcheck
#*****************************************************************************
# One version, kept in the Python package.
file(STRINGS ${CMAKE_SOURCE_DIR}/python/svcheck/__version__.py SVCHECK_BASE_LINE
     REGEX "^BASE = ")
file(STRINGS ${CMAKE_SOURCE_DIR}/python/svcheck/__version__.py SVCHECK_SUFFIX_LINE
     REGEX "^SUFFIX = ")
string(REGEX REPLACE "^BASE = \"([^\"]*)\".*" "\\1" SVCHECK_BASE "${SVCHECK_BASE_LINE}")
string(REGEX REPLACE "^SUFFIX = \"([^\"]*)\".*" "\\1" SVCHECK_SUFFIX "${SVCHECK_SUFFIX_LINE}")
set(SVCHECK_VERSION "${SVCHECK_BASE}${SVCHECK_SUFFIX}")
message(STATUS "svcheck: version ${SVCHECK_VERSION}")
# Reconfigure when it changes (CI stamps SUFFIX).
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             ${CMAKE_SOURCE_DIR}/python/svcheck/__version__.py)

# libsvcheck (svcheck-plan P2.6b): the command, the engine, the host, the
# model and slang in one shared library, with the factory the Python
# bindings load (src/api/include/svcheck/IFactory.h). The executable is a
# main() on it. On WASI (SVCHECK_SHARED off) it is one static executable,
# as before.

set(_svcheck_lib_src
  src/api/Factory.cpp
  src/cli/Cli.cpp
  src/cli/Config.cpp
  src/cli/Lint.cpp
  src/cli/Out.cpp
  src/cli/Query.cpp)
if (SVCHECK_SHARED)
  add_library(svcheck_lib SHARED ${_svcheck_lib_src})
  set_target_properties(svcheck_lib PROPERTIES
    OUTPUT_NAME svcheck
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    BUILD_RPATH_USE_ORIGIN ON)
  target_compile_definitions(svcheck_lib PRIVATE SVCHECK_BUILD_DLL)
  if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # A missing symbol fails the link, not the Python import.
    target_link_options(svcheck_lib PRIVATE "-Wl,--no-undefined")
  endif()
else()
  add_library(svcheck_lib STATIC ${_svcheck_lib_src})
endif()
target_include_directories(svcheck_lib PUBLIC
  ${CMAKE_SOURCE_DIR}/src/api/include
  ${CMAKE_SOURCE_DIR}/src/cli)
target_compile_definitions(svcheck_lib PRIVATE SVCHECK_VERSION="${SVCHECK_VERSION}")
target_link_libraries(svcheck_lib PUBLIC svcheck_host)

add_executable(svcheck src/cli/main.cpp)
target_link_libraries(svcheck PRIVATE svcheck_lib)
if (SVCHECK_SHARED)
  # build/bin/svcheck finds build/lib/libsvcheck.so; in the wheel,
  # svcheck/bin/svcheck finds svcheck/libsvcheck.so.
  if (APPLE)
    set_target_properties(svcheck PROPERTIES
      BUILD_RPATH "@loader_path/../lib;@loader_path/.."
      INSTALL_RPATH "@loader_path/../lib;@loader_path/..")
  else()
    set_target_properties(svcheck PROPERTIES
      BUILD_RPATH "\$ORIGIN/../lib:\$ORIGIN/.."
      INSTALL_RPATH "\$ORIGIN/../lib:\$ORIGIN/..")
  endif()
endif()

install(TARGETS svcheck RUNTIME DESTINATION bin)
if (SVCHECK_SHARED)
  install(TARGETS svcheck_lib LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
endif()
