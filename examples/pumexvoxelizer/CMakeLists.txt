set( pumexvoxelizer_shader_names 
  shaders/voxelizer_basic.frag
  shaders/voxelizer_basic.vert
  shaders/voxelizer_raymarch.frag
  shaders/voxelizer_raymarch.vert
  shaders/voxelizer_voxelize.frag
  shaders/voxelizer_voxelize.geom
  shaders/voxelizer_voxelize.vert
)
process_shaders( ${CMAKE_CURRENT_LIST_DIR} pumexvoxelizer_shader_names pumexvoxelizer_input_shaders pumexvoxelizer_output_shaders )
add_custom_target ( pumexvoxelizer-shaders DEPENDS ${pumexvoxelizer_output_shaders} SOURCES ${pumexvoxelizer_input_shaders})

add_executable( pumexvoxelizer pumexvoxelizer.cpp )
target_include_directories( pumexvoxelizer PRIVATE ${PUMEX_EXAMPLES_INCLUDES} )
add_dependencies( pumexvoxelizer pumexvoxelizer-shaders ${PUMEX_EXAMPLES_EXTERNALS} )
set_target_properties( pumexvoxelizer PROPERTIES RESOURCE "${pumexvoxelizer_output_shaders}" )
target_link_libraries( pumexvoxelizer pumexlib )
set_executable_postfixes( pumexvoxelizer )

install( TARGETS pumexvoxelizer EXPORT PumexTargets
         RUNTIME DESTINATION bin COMPONENT examples
         RESOURCE DESTINATION share/pumex/shaders COMPONENT examples
       )
