set( PUMEXVIEWERQT_SHADER_NAMES
  shaders/viewerqt_basic.vert
  shaders/viewerqt_basic.frag
)
process_shaders( ${CMAKE_CURRENT_LIST_DIR} PUMEXVIEWERQT_SHADER_NAMES PUMEXVIEWERQT_INPUT_SHADERS PUMEXVIEWERQT_OUTPUT_SHADERS )
add_custom_target ( shaders-pumexviewerqt DEPENDS ${PUMEXVIEWERQT_OUTPUT_SHADERS} SOURCES ${PUMEXVIEWERQT_INPUT_SHADERS})

find_package( Qt5 REQUIRED COMPONENTS Widgets )

qt5_wrap_cpp( PUMEXVIEWERQT_SOURCES_MOC MainWindow.h )

add_executable( pumexviewerqt pumexviewerqt.cpp MainWindow.cpp ${PUMEXVIEWERQT_SOURCES_MOC} )
target_include_directories( pumexviewerqt PRIVATE ${PUMEX_EXAMPLES_INCLUDES} )
add_dependencies( pumexviewerqt shaders-pumexviewerqt  )
set_target_properties( pumexviewerqt PROPERTIES 
  RESOURCE "${PUMEXVIEWERQT_OUTPUT_SHADERS}"
)

if(UNIX AND NOT APPLE)
  target_link_libraries( pumexviewerqt pumex ${PUMEX_LIBRARIES_EXAMPLES} pthread Qt5::Widgets )
else()
  target_link_libraries( pumexviewerqt pumex ${PUMEX_LIBRARIES_EXAMPLES} Qt5::Widgets )
endif()

set_target_postfixes( pumexviewerqt )

install( TARGETS pumexviewerqt
         EXPORT PumexTargets
         RUNTIME DESTINATION bin COMPONENT examples
         RESOURCE DESTINATION share/pumex/shaders COMPONENT examples
       )
