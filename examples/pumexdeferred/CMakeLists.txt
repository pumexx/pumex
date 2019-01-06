set( PUMEXDEFERRED_SHADER_NAMES
  shaders/deferred_buildz.frag
  shaders/deferred_buildz.vert
  shaders/deferred_composite.frag
  shaders/deferred_composite.vert
  shaders/deferred_gbuffers.frag
  shaders/deferred_gbuffers.vert
)
process_shaders( ${CMAKE_CURRENT_LIST_DIR} PUMEXDEFERRED_SHADER_NAMES PUMEXDEFERRED_INPUT_SHADERS PUMEXDEFERRED_OUTPUT_SHADERS )
add_custom_target ( shaders-pumexdeferred DEPENDS ${PUMEXDEFERRED_OUTPUT_SHADERS} SOURCES ${PUMEXDEFERRED_INPUT_SHADERS})

add_executable( pumexdeferred pumexdeferred.cpp )
target_include_directories( pumexdeferred PRIVATE ${PUMEX_EXAMPLES_INCLUDES} )
add_dependencies( pumexdeferred shaders-pumexdeferred )
set_target_properties( pumexdeferred PROPERTIES RESOURCE "${PUMEXDEFERRED_OUTPUT_SHADERS}" )
target_link_libraries( pumexdeferred pumex ${PUMEX_LIBRARIES_EXAMPLES} )
set_target_postfixes( pumexdeferred )

install( TARGETS pumexdeferred
         EXPORT PumexTargets
         RUNTIME DESTINATION bin COMPONENT examples
         RESOURCE DESTINATION share/pumex/shaders COMPONENT examples
       )
