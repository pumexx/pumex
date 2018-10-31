set( PUMEXVIEWERQT_SHADER_NAMES
  shaders/viewerqt_basic.vert
  shaders/viewerqt_basic.frag
)
process_shaders( ${CMAKE_CURRENT_LIST_DIR} PUMEXVIEWERQT_SHADER_NAMES PUMEXVIEWERQT_INPUT_SHADERS PUMEXVIEWERQT_OUTPUT_SHADERS )
add_custom_target ( pumexviewerqt-shaders DEPENDS ${PUMEXVIEWERQT_OUTPUT_SHADERS} SOURCES ${PUMEXVIEWERQT_INPUT_SHADERS})

add_executable( pumexviewerqt pumexviewerqt.cpp )
target_include_directories( pumexviewerqt PRIVATE ${PUMEX_EXAMPLES_INCLUDES} )
add_dependencies( pumexviewerqt pumexviewerqt-shaders ${PUMEX_EXAMPLES_EXTERNALS} )
set_target_properties( pumexviewerqt PROPERTIES RESOURCE "${PUMEXVIEWERQT_OUTPUT_SHADERS}" )
if(UNIX AND NOT APPLE)
  target_link_libraries( pumexviewerqt pumexlib pthread )
else()
  target_link_libraries( pumexviewerqt pumexlib )
endif()
set_target_postfixes( pumexviewerqt )

install( TARGETS pumexviewerqt
         EXPORT PumexTargets
         RUNTIME DESTINATION bin COMPONENT examples
         RESOURCE DESTINATION share/pumex/shaders COMPONENT examples
       )
