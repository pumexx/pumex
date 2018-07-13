set( PUMEXVIEWER_SHADER_NAMES 
  shaders/viewer_basic.vert
  shaders/viewer_basic.frag
)
process_shaders( ${CMAKE_CURRENT_LIST_DIR} PUMEXVIEWER_SHADER_NAMES PUMEXVIEWER_INPUT_SHADERS PUMEXVIEWER_OUTPUT_SHADERS )
add_custom_target ( pumexviewer-shaders DEPENDS ${PUMEXVIEWER_OUTPUT_SHADERS} SOURCES ${PUMEXVIEWER_INPUT_SHADERS})

add_executable( pumexviewer pumexviewer.cpp )
target_include_directories( pumexviewer PRIVATE ${PUMEX_EXAMPLES_INCLUDES} )
add_dependencies( pumexviewer pumexviewer-shaders ${PUMEX_EXAMPLES_EXTERNALS} )
set_target_properties( pumexviewer PROPERTIES RESOURCE "${PUMEXVIEWER_OUTPUT_SHADERS}" )
target_link_libraries( pumexviewer pumexlib )
set_target_postfixes( pumexviewer )

install( TARGETS pumexviewer EXPORT PumexTargets
         RUNTIME DESTINATION bin COMPONENT examples
         RESOURCE DESTINATION share/pumex/shaders COMPONENT examples
       )
