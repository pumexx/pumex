set( pumexgpucull_shader_names 
  shaders/gpucull_dynamic_filter_instances.comp
  shaders/gpucull_dynamic_render.frag
  shaders/gpucull_dynamic_render.vert
  shaders/gpucull_static_filter_instances.comp
  shaders/gpucull_static_render.frag
  shaders/gpucull_static_render.vert
)
process_shaders( ${CMAKE_CURRENT_LIST_DIR} pumexgpucull_shader_names pumexgpucull_input_shaders pumexgpucull_output_shaders )
add_custom_target ( pumexgpucull-shaders DEPENDS ${pumexgpucull_output_shaders} SOURCES ${pumexgpucull_input_shaders})

add_executable( pumexgpucull pumexgpucull.cpp )
target_include_directories( pumexgpucull PRIVATE ${PUMEX_EXAMPLES_INCLUDES} )
add_dependencies( pumexgpucull pumexgpucull-shaders ${PUMEX_EXAMPLES_EXTERNALS} )
set_target_properties( pumexgpucull PROPERTIES RESOURCE "${pumexgpucull_output_shaders}" )
target_link_libraries( pumexgpucull pumexlib )
set_executable_postfixes( pumexgpucull )

install( TARGETS pumexgpucull EXPORT PumexTargets
         RUNTIME DESTINATION bin COMPONENT examples
         RESOURCE DESTINATION share/pumex/shaders COMPONENT examples
       )
