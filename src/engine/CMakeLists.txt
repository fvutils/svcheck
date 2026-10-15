#****************************************************************************
#* svcheck_engine -- the rule engine, forked from pssparser's src/lint at
#* 57c6b14 (svcheck-plan P1.1; lineage: svcheck-design.md §3.1).
#*
#* The engine knows nothing about SystemVerilog: this target has no slang
#* include path, so an SV include here is a build error (pssparser C9.5).
#* The host gives it a schema (schema/Reflect.h), a context (run/Context.h)
#* and a function library (cel::setHostLib).
#*
#* Inputs:  PACKAGES_DIR, Python3_EXECUTABLE
#*
#* rapidyaml comes from ivpm as the v0.16.0 source release. ivpm keeps the
#* archive's top directory (svcheck-plan P0-6), so the tree is
#* ${PACKAGES_DIR}/rapidyaml/rapidyaml.v<ver>/{src,ext/c4core.src}. The
#* versioned directory is found by glob, so a version bump is an ivpm.yaml
#* change only.
#****************************************************************************

file(GLOB _ryml_hpp "${PACKAGES_DIR}/rapidyaml/*/src/ryml.hpp")
list(LENGTH _ryml_hpp _ryml_n)
if (NOT _ryml_n EQUAL 1)
  message(FATAL_ERROR
    "engine: expected one rapidyaml source tree under ${PACKAGES_DIR}/rapidyaml, "
    "found ${_ryml_n} -- run `ivpm update`")
endif()
get_filename_component(_ryml_src_dir "${_ryml_hpp}" DIRECTORY)
get_filename_component(RAPIDYAML_ROOT "${_ryml_src_dir}" DIRECTORY)

# The same file list rapidyaml's own build uses for its bundled c4core
# (ext/c4core.src.txt), less the headers.
file(GLOB_RECURSE _ryml_src "${RAPIDYAML_ROOT}/src/c4/yml/*.cpp")
file(GLOB _c4core_src "${RAPIDYAML_ROOT}/ext/c4core.src/c4/*.cpp")

set(_engine_dir ${CMAKE_CURRENT_SOURCE_DIR})
file(GLOB_RECURSE _engine_src CONFIGURE_DEPENDS "${_engine_dir}/*.cpp")

#****************************************************************************
#* Built-in rule files (pssparser C4.3): rules/*.yaml at the repository
#* root, embedded as bytes. None yet (P2); the generator then writes the
#* empty table.
#****************************************************************************
set(_builtin_dir "${CMAKE_CURRENT_BINARY_DIR}/builtin")
file(GLOB _builtin_rules CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/rules/*.yaml" "${CMAKE_SOURCE_DIR}/rules/*.yml")
add_custom_command(
  OUTPUT "${_builtin_dir}/BuiltinRules.cpp"
  COMMAND "${Python3_EXECUTABLE}" "${_engine_dir}/rules/mk_builtin_rules.py"
          "${CMAKE_SOURCE_DIR}/rules"
          "${_builtin_dir}/BuiltinRules.cpp"
  DEPENDS ${_builtin_rules} "${_engine_dir}/rules/mk_builtin_rules.py"
  COMMENT "Embedding the built-in rule files"
  VERBATIM)

#****************************************************************************
#* The marker catalogue (pssparser C7.3): catalog/catalog.json, embedded as
#* bytes. svcheck's is hand-maintained: the engine's own markers.
#****************************************************************************
set(_catalog_dir "${CMAKE_CURRENT_BINARY_DIR}/catalog")
add_custom_command(
  OUTPUT "${_catalog_dir}/CatalogData.cpp"
  COMMAND "${Python3_EXECUTABLE}" "${_engine_dir}/catalog/mk_catalog.py"
          "${_engine_dir}/catalog/catalog.json"
          "${_catalog_dir}/CatalogData.cpp"
  DEPENDS "${_engine_dir}/catalog/catalog.json"
          "${_engine_dir}/catalog/mk_catalog.py"
  COMMENT "Embedding the marker catalogue"
  VERBATIM)

add_library(svcheck_engine STATIC
  ${_engine_src} ${_ryml_src} ${_c4core_src}
  "${_builtin_dir}/BuiltinRules.cpp"
  "${_catalog_dir}/CatalogData.cpp")

target_include_directories(svcheck_engine
  PUBLIC
    "${_engine_dir}"
    "${_engine_dir}/include"
    "${_engine_dir}/schema"
  PRIVATE
    "${RAPIDYAML_ROOT}/src"
    "${RAPIDYAML_ROOT}/ext/c4core.src")
set_target_properties(svcheck_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
