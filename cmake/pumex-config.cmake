get_filename_component(SELF_DIR "${CMAKE_CURRENT_LIST_FILE}" PATH)
include(${SELF_DIR}/pumex-targets.cmake)

# Nasty hack for that f_king Freetype library :(
get_filename_component(_IMPORT_PREFIX "${CMAKE_CURRENT_LIST_FILE}" PATH)
get_filename_component(_IMPORT_PREFIX "${_IMPORT_PREFIX}" PATH)
get_filename_component(_IMPORT_PREFIX "${_IMPORT_PREFIX}" PATH)
get_filename_component(_IMPORT_PREFIX "${_IMPORT_PREFIX}" PATH)
if(_IMPORT_PREFIX STREQUAL "/")
  set(_IMPORT_PREFIX "")
endif()

set( _FTINCLUDEDIRS )
if(UNIX AND NOT APPLE)
  find_package(Freetype)
  set( _FTINCLUDEDIRS "${FREETYPE_INCLUDE_DIRS}" )
else()
  set( _FTINCLUDEDIRS "${_IMPORT_PREFIX}/include;${_IMPORT_PREFIX}/include/freetype2;" )
endif()

set_target_properties(pumex::pumexlib PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES "${_FTINCLUDEDIRS}"
)  
# Cleanup temporary variables.
set( _FTINCLUDEDIRS )
set( _IMPORT_PREFIX )
