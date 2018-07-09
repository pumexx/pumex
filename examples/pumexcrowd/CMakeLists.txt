set( pumexcrowd_shader_names 
  shaders/crowd_filter_instances.comp
  shaders/crowd_instanced_animation.vert
  shaders/crowd_instanced_animation.frag
)
process_shaders( ${CMAKE_CURRENT_LIST_DIR} pumexcrowd_shader_names pumexcrowd_input_shaders pumexcrowd_output_shaders )
add_custom_target ( pumexcrowd-shaders DEPENDS ${pumexcrowd_output_shaders} SOURCES ${pumexcrowd_input_shaders})

add_executable( pumexcrowd pumexcrowd.cpp )
target_include_directories( pumexcrowd PRIVATE ${PUMEX_EXAMPLES_INCLUDES} )
add_dependencies( pumexcrowd pumexcrowd-shaders ${PUMEX_EXAMPLES_EXTERNALS} )
set_target_properties( pumexcrowd PROPERTIES RESOURCE "${pumexcrowd_output_shaders}" )
target_link_libraries( pumexcrowd pumexlib )
set_executable_postfixes( pumexcrowd )

install( TARGETS pumexcrowd 
         EXPORT PumexTargets
         RUNTIME DESTINATION bin 
         RESOURCE DESTINATION share/pumex/shaders
       )
