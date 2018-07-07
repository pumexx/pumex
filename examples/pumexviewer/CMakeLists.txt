set( pumexviewer_input_shaders 
  shaders/viewer_basic.vert
  shaders/viewer_basic.frag
)
process_shaders( ${CMAKE_CURRENT_LIST_DIR} pumexviewer_input_shaders pumexviewer_output_shaders )
add_custom_target ( pumexviewer-shaders DEPENDS ${pumexviewer_output_shaders} SOURCES ${pumexviewer_input_shaders})

add_executable( pumexviewer pumexviewer.cpp )
target_include_directories( pumexviewer PRIVATE ${PUMEX_EXAMPLES_INCLUDES} )
add_dependencies( pumexviewer pumexviewer-shaders ${PUMEX_EXAMPLES_EXTERNALS} )
set_target_properties( pumexviewer PROPERTIES RESOURCE "${pumexviewer_output_shaders}" )
target_link_libraries( pumexviewer pumexlib )
set_executable_postfixes( pumexviewer )

install( TARGETS pumexviewer
         RUNTIME DESTINATION bin 
         RESOURCE DESTINATION share/pumex/shaders
       )
