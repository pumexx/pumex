set( PUMEXCROWD_SHADER_NAMES 
  shaders/crowd_filter_instances.comp
  shaders/crowd_instanced_animation.vert
  shaders/crowd_instanced_animation.frag
)
process_shaders( ${CMAKE_CURRENT_LIST_DIR} PUMEXCROWD_SHADER_NAMES PUMEXCROWD_INPUT_SHADERS PUMEXCROWD_OUTPUT_SHADERS )
add_custom_target ( pumexcrowd-shaders DEPENDS ${PUMEXCROWD_OUTPUT_SHADERS} SOURCES ${PUMEXCROWD_INPUT_SHADERS})

add_executable( pumexcrowd pumexcrowd.cpp )
target_include_directories( pumexcrowd PRIVATE ${PUMEX_EXAMPLES_INCLUDES} )
add_dependencies( pumexcrowd pumexcrowd-shaders ${PUMEX_EXAMPLES_EXTERNALS} )
set_target_properties( pumexcrowd PROPERTIES RESOURCE "${PUMEXCROWD_OUTPUT_SHADERS}" )
target_link_libraries( pumexcrowd pumexlib )
set_target_postfixes( pumexcrowd )

install( TARGETS pumexcrowd 
         EXPORT PumexTargets
         RUNTIME DESTINATION bin COMPONENT examples
         RESOURCE DESTINATION share/pumex/shaders COMPONENT examples
       )
