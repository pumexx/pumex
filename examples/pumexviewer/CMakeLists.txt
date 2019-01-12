set( PUMEXVIEWER_SHADER_NAMES
  shaders/viewer_basic.vert
  shaders/viewer_basic.frag
)
process_shaders( ${CMAKE_CURRENT_LIST_DIR} PUMEXVIEWER_SHADER_NAMES PUMEXVIEWER_INPUT_SHADERS PUMEXVIEWER_OUTPUT_SHADERS )
add_custom_target ( shaders-pumexviewer DEPENDS ${PUMEXVIEWER_OUTPUT_SHADERS} SOURCES ${PUMEXVIEWER_INPUT_SHADERS})
if(NOT ANDROID)
  add_executable( pumexviewer pumexviewer.cpp )
else()
  add_library( pumexviewer SHARED pumexviewer.cpp )
endif()
target_include_directories( pumexviewer PRIVATE ${PUMEX_EXAMPLES_INCLUDES} )
add_dependencies( pumexviewer shaders-pumexviewer )
set_target_properties( pumexviewer PROPERTIES RESOURCE "${PUMEXVIEWER_OUTPUT_SHADERS}" )
target_link_libraries( pumexviewer pumex ${PUMEX_LIBRARIES_EXAMPLES} )
if( ANDROID )
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate")
  target_link_libraries( pumexviewer native_app_glue )
endif()
set_target_postfixes( pumexviewer )

install( TARGETS pumexviewer
         EXPORT PumexTargets
         RUNTIME DESTINATION bin COMPONENT examples
         ARCHIVE DESTINATION lib COMPONENT libraries
         LIBRARY DESTINATION lib COMPONENT libraries
         RESOURCE DESTINATION share/pumex/shaders COMPONENT examples
       )
