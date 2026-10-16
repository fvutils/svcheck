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

file(GLOB _host_src CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(svcheck_host STATIC ${_host_src} "${_schema_cpp}")
target_include_directories(svcheck_host PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(svcheck_host PUBLIC svcheck_engine slang::slang)
set_target_properties(svcheck_host PROPERTIES POSITION_INDEPENDENT_CODE ON)
