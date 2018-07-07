add_subdirectory( pumexviewer )
add_subdirectory( pumexcrowd )
add_subdirectory( pumexgpucull )
add_subdirectory( pumexdeferred )
add_subdirectory( pumexvoxelizer )
add_subdirectory( pumexmultiview )

set_property(DIRECTORY ${PROJECT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT pumexcrowd)
