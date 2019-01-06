set( PUMEXIBL_SHADER_NAMES
  shaders/ibl_background.vert
  shaders/ibl_background.frag
  shaders/ibl_equirectangular_to_cubemap.vert
  shaders/ibl_equirectangular_to_cubemap.frag
  shaders/ibl_calculate_diffuse_irradiance.vert
  shaders/ibl_calculate_diffuse_irradiance.frag
  shaders/ibl_calculate_prefiltered_environment.vert
  shaders/ibl_calculate_prefiltered_environment.frag
  shaders/ibl_calculate_brdf.vert
  shaders/ibl_calculate_brdf.frag
  shaders/ibl_render.vert
  shaders/ibl_render.frag
)
process_shaders( ${CMAKE_CURRENT_LIST_DIR} PUMEXIBL_SHADER_NAMES PUMEXIBL_INPUT_SHADERS PUMEXIBL_OUTPUT_SHADERS )
add_custom_target ( shaders-pumexibl DEPENDS ${PUMEXIBL_OUTPUT_SHADERS} SOURCES ${PUMEXIBL_SHADER_NAMES})

add_executable( pumexibl pumexibl.cpp )
target_include_directories( pumexibl PRIVATE ${PUMEX_EXAMPLES_INCLUDES} )
add_dependencies( pumexibl shaders-pumexibl )
set_target_properties( pumexibl PROPERTIES RESOURCE "${PUMEXIBL_OUTPUT_SHADERS}" )
target_link_libraries( pumexibl pumex ${PUMEX_LIBRARIES_EXAMPLES} )
set_target_postfixes( pumexibl )

install( TARGETS pumexibl
         EXPORT PumexTargets
         RUNTIME DESTINATION bin COMPONENT examples
         RESOURCE DESTINATION share/pumex/shaders COMPONENT examples
       )
