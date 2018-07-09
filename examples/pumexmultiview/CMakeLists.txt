set( pumexmultiview_shader_names 
  shaders/multiview_composite.frag
  shaders/multiview_composite.vert
  shaders/multiview_display.frag
  shaders/multiview_display.vert
  shaders/multiview_gbuffers.frag
  shaders/multiview_gbuffers.vert
)
process_shaders( ${CMAKE_CURRENT_LIST_DIR} pumexmultiview_shader_names pumexmultiview_input_shaders pumexmultiview_output_shaders )
add_custom_target ( pumexmultiview-shaders DEPENDS ${pumexmultiview_output_shaders} SOURCES ${pumexmultiview_input_shaders})

add_executable( pumexmultiview pumexmultiview.cpp )
target_include_directories( pumexmultiview PRIVATE ${PUMEX_EXAMPLES_INCLUDES} )
add_dependencies( pumexmultiview pumexmultiview-shaders ${PUMEX_EXAMPLES_EXTERNALS} )
set_target_properties( pumexmultiview PROPERTIES RESOURCE "${pumexmultiview_output_shaders}" )
target_link_libraries( pumexmultiview pumexlib )
set_executable_postfixes( pumexmultiview )

install( TARGETS pumexmultiview EXPORT PumexTargets
         RUNTIME DESTINATION bin 
         RESOURCE DESTINATION share/pumex/shaders
       )
