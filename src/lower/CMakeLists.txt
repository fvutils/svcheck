#****************************************************************************
#* svcheck_lower -- the lowering pass, slang's semantic model to the curated
#* model (svcheck-design §3.6, svcheck-plan P1.3). Knows nothing of the rule
#* engine.
#****************************************************************************
file(GLOB _lower_src CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_library(svcheck_lower STATIC ${_lower_src})
target_include_directories(svcheck_lower
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
  PRIVATE "${SVCHECK_SEM_IMPL_DIR}")
target_link_libraries(svcheck_lower PUBLIC svcheck_sem slang::slang)
set_target_properties(svcheck_lower PROPERTIES POSITION_INDEPENDENT_CODE ON)
