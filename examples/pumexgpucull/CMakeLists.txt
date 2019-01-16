set( PUMEXGPUCULL_SHADER_NAMES
  shaders/gpucull_dynamic_filter_instances.comp
  shaders/gpucull_dynamic_render.frag
  shaders/gpucull_dynamic_render.vert
  shaders/gpucull_static_filter_instances.comp
  shaders/gpucull_static_render.frag
  shaders/gpucull_static_render.vert
)
process_shaders( ${CMAKE_CURRENT_LIST_DIR} PUMEXGPUCULL_SHADER_NAMES PUMEXGPUCULL_INPUT_SHADERS PUMEXGPUCULL_OUTPUT_SHADERS )
add_custom_target ( shaders-pumexgpucull DEPENDS ${PUMEXGPUCULL_OUTPUT_SHADERS} SOURCES ${PUMEXGPUCULL_INPUT_SHADERS})

if(NOT ANDROID)
  add_executable( pumexgpucull pumexgpucull.cpp )
else()
  add_library( pumexgpucull SHARED pumexgpucull.cpp )
endif()

target_include_directories( pumexgpucull PRIVATE ${PUMEX_EXAMPLES_INCLUDES} )
add_dependencies( pumexgpucull shaders-pumexgpucull )
set_target_properties( pumexgpucull PROPERTIES RESOURCE "${PUMEXGPUCULL_OUTPUT_SHADERS}" )
target_link_libraries( pumexgpucull pumex ${PUMEX_LIBRARIES_EXAMPLES} )
if( ANDROID )
  target_link_libraries( pumexgpucull native_app_glue )
endif()
set_target_postfixes( pumexgpucull )

install( TARGETS pumexgpucull
         EXPORT PumexTargets
         RUNTIME DESTINATION bin COMPONENT examples
         ARCHIVE DESTINATION lib COMPONENT libraries
         LIBRARY DESTINATION lib COMPONENT libraries
         RESOURCE DESTINATION share/pumex/shaders COMPONENT examples
       )
