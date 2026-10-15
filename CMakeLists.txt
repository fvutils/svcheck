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

set(SLANG_USE_MIMALLOC OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_TOOLS OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_INSTALL OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_PYLIB OFF CACHE BOOL "" FORCE)

add_subdirectory(${PACKAGES_DIR}/slang ${CMAKE_BINARY_DIR}/slang EXCLUDE_FROM_ALL)

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

add_executable(svcheck
  src/cli/main.cpp)
target_compile_definitions(svcheck PRIVATE SVCHECK_VERSION="${SVCHECK_VERSION}")
target_link_libraries(svcheck PRIVATE slang::slang)

install(TARGETS svcheck RUNTIME DESTINATION bin)
