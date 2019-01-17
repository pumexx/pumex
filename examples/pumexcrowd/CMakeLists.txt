set( PUMEXCROWD_SHADER_NAMES
  shaders/crowd_filter_instances.comp
  shaders/crowd_instanced_animation.vert
  shaders/crowd_instanced_animation.frag
)
process_shaders( ${CMAKE_CURRENT_LIST_DIR} PUMEXCROWD_SHADER_NAMES PUMEXCROWD_INPUT_SHADERS PUMEXCROWD_OUTPUT_SHADERS )
add_custom_target ( shaders-pumexcrowd DEPENDS ${PUMEXCROWD_OUTPUT_SHADERS} SOURCES ${PUMEXCROWD_INPUT_SHADERS})

if(NOT ANDROID)
  add_executable( pumexcrowd pumexcrowd.cpp )
else()
  add_library( pumexcrowd SHARED pumexcrowd.cpp )
endif()
target_include_directories( pumexcrowd PRIVATE ${PUMEX_EXAMPLES_INCLUDES} )
add_dependencies( pumexcrowd shaders-pumexcrowd )
set_target_properties( pumexcrowd PROPERTIES RESOURCE "${PUMEXCROWD_OUTPUT_SHADERS}" )
target_link_libraries( pumexcrowd pumex ${PUMEX_LIBRARIES_EXAMPLES} )
if( ANDROID )
  target_link_libraries( pumexcrowd native_app_glue )
endif()
set_target_postfixes( pumexcrowd )

install( TARGETS pumexcrowd
         EXPORT PumexTargets
         RUNTIME DESTINATION bin COMPONENT examples
         ARCHIVE DESTINATION lib COMPONENT libraries
         LIBRARY DESTINATION lib COMPONENT libraries
         RESOURCE DESTINATION share/pumex/shaders COMPONENT examples
       )
