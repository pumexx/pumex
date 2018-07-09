set( pumexdeferred_shader_names 
  shaders/deferred_composite.frag
  shaders/deferred_composite.vert
  shaders/deferred_gbuffers.frag
  shaders/deferred_gbuffers.vert
)
process_shaders( ${CMAKE_CURRENT_LIST_DIR} pumexdeferred_shader_names pumexdeferred_input_shaders pumexdeferred_output_shaders )
add_custom_target ( pumexdeferred-shaders DEPENDS ${pumexdeferred_output_shaders} SOURCES ${pumexdeferred_input_shaders})

add_executable( pumexdeferred pumexdeferred.cpp )
target_include_directories( pumexdeferred PRIVATE ${PUMEX_EXAMPLES_INCLUDES} )
add_dependencies( pumexdeferred pumexdeferred-shaders ${PUMEX_EXAMPLES_EXTERNALS} )
set_target_properties( pumexdeferred PROPERTIES RESOURCE "${pumexdeferred_output_shaders}" )
target_link_libraries( pumexdeferred pumexlib )
set_executable_postfixes( pumexdeferred )

install( TARGETS pumexdeferred EXPORT PumexTargets
         RUNTIME DESTINATION bin 
         RESOURCE DESTINATION share/pumex/shaders
       )
