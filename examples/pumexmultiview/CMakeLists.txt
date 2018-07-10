set( PUMEXMULTIVIEW_SHADER_NAMES 
  shaders/multiview_composite.frag
  shaders/multiview_composite.vert
  shaders/multiview_display.frag
  shaders/multiview_display.vert
  shaders/multiview_gbuffers.frag
  shaders/multiview_gbuffers.vert
)
process_shaders( ${CMAKE_CURRENT_LIST_DIR} PUMEXMULTIVIEW_SHADER_NAMES PUMEXMULTIVIEW_INPUT_SHADERS PUMEXMULTIVIEW_OUTPUT_SHADERS )
add_custom_target ( pumexmultiview-shaders DEPENDS ${PUMEXMULTIVIEW_OUTPUT_SHADERS} SOURCES ${PUMEXMULTIVIEW_INPUT_SHADERS})

add_executable( pumexmultiview pumexmultiview.cpp )
target_include_directories( pumexmultiview PRIVATE ${PUMEX_EXAMPLES_INCLUDES} )
add_dependencies( pumexmultiview pumexmultiview-shaders ${PUMEX_EXAMPLES_EXTERNALS} )
set_target_properties( pumexmultiview PROPERTIES RESOURCE "${PUMEXMULTIVIEW_OUTPUT_SHADERS}" )
target_link_libraries( pumexmultiview pumexlib )
set_executable_postfixes( pumexmultiview )

install( TARGETS pumexmultiview EXPORT PumexTargets
         RUNTIME DESTINATION bin COMPONENT examples
         RESOURCE DESTINATION share/pumex/shaders COMPONENT examples
       )
