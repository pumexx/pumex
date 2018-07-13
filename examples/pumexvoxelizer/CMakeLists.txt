set( PUMEXVOXELIZER_SHADER_NAMES 
  shaders/voxelizer_basic.frag
  shaders/voxelizer_basic.vert
  shaders/voxelizer_raymarch.frag
  shaders/voxelizer_raymarch.vert
  shaders/voxelizer_voxelize.frag
  shaders/voxelizer_voxelize.geom
  shaders/voxelizer_voxelize.vert
)
process_shaders( ${CMAKE_CURRENT_LIST_DIR} PUMEXVOXELIZER_SHADER_NAMES PUMEXVOXELIZER_INPUT_SHADERS PUMEXVOXELIZER_OUTPUT_SHADERS )
add_custom_target ( pumexvoxelizer-shaders DEPENDS ${PUMEXVOXELIZER_OUTPUT_SHADERS} SOURCES ${PUMEXVOXELIZER_INPUT_SHADERS})

add_executable( pumexvoxelizer pumexvoxelizer.cpp )
target_include_directories( pumexvoxelizer PRIVATE ${PUMEX_EXAMPLES_INCLUDES} )
add_dependencies( pumexvoxelizer pumexvoxelizer-shaders ${PUMEX_EXAMPLES_EXTERNALS} )
set_target_properties( pumexvoxelizer PROPERTIES RESOURCE "${PUMEXVOXELIZER_OUTPUT_SHADERS}" )
target_link_libraries( pumexvoxelizer pumexlib )
set_target_postfixes( pumexvoxelizer )

install( TARGETS pumexvoxelizer EXPORT PumexTargets
         RUNTIME DESTINATION bin COMPONENT examples
         RESOURCE DESTINATION share/pumex/shaders COMPONENT examples
       )
