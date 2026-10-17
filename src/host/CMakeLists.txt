#****************************************************************************
#* svcheck_host -- the SV host binding (svcheck-design §3.2): the schemas
#* the engine reads, node handles and locations over slang.
#*
#* Inputs:  Python3_EXECUTABLE; the slang source tree (slang_SOURCE_DIR,
#*          from add_subdirectory) for scripts/syntax.txt
#****************************************************************************

#****************************************************************************
#* The syntax schema's tables, generated from slang's syntax.txt
#* (scripts/gen_syntax_schema.py; svcheck-plan P1.2).
#****************************************************************************
set(_syntax_txt "${PACKAGES_DIR}/slang/scripts/syntax.txt")
set(_gen "${CMAKE_SOURCE_DIR}/scripts/gen_syntax_schema.py")
set(_schema_cpp "${CMAKE_CURRENT_BINARY_DIR}/schema/SyntaxSchema.cpp")
add_custom_command(
  OUTPUT "${_schema_cpp}"
  COMMAND "${Python3_EXECUTABLE}" "${_gen}" "${_syntax_txt}" "${_schema_cpp}"
  DEPENDS "${_syntax_txt}" "${_gen}"
  COMMENT "Generating the syntax schema from slang's syntax.txt"
  VERBATIM)

#****************************************************************************
#* slang's warnings as SLANG### markers (scripts/gen_slang_warnings.py;
#* svcheck-plan P2.3, Q-UVM4): the append-only numbering in
#* slang_warnings.txt, with slang's diagnostics.txt for tiers and summaries.
#****************************************************************************
set(_diag_txt "${PACKAGES_DIR}/slang/scripts/diagnostics.txt")
set(_warn_gen "${CMAKE_SOURCE_DIR}/scripts/gen_slang_warnings.py")
set(_warn_table "${CMAKE_CURRENT_SOURCE_DIR}/slang_warnings.txt")
set(_warn_cpp "${CMAKE_CURRENT_BINARY_DIR}/schema/SlangWarningsTable.cpp")
add_custom_command(
  OUTPUT "${_warn_cpp}"
  COMMAND "${Python3_EXECUTABLE}" "${_warn_gen}" "${_diag_txt}" "${_warn_table}" "${_warn_cpp}"
  DEPENDS "${_diag_txt}" "${_warn_table}" "${_warn_gen}"
  COMMENT "Generating the SLANG marker table from slang's diagnostics.txt"
  VERBATIM)

file(GLOB _host_src CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(svcheck_host STATIC ${_host_src} "${_schema_cpp}" "${_warn_cpp}")
target_include_directories(svcheck_host
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
  PRIVATE "${SVCHECK_SEM_IMPL_DIR}")
target_link_libraries(svcheck_host PUBLIC svcheck_engine svcheck_sem svcheck_lower slang::slang)
set_target_properties(svcheck_host PROPERTIES POSITION_INDEPENDENT_CODE ON)
