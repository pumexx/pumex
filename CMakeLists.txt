cmake_minimum_required(VERSION 3.7.0 FATAL_ERROR)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules")

project( Pumex )
set(Pumex_VERSION_MAJOR 0)
set(Pumex_VERSION_MINOR 1)
set(Pumex_VERSION_PATCH 0)

configure_file (
  "${PROJECT_SOURCE_DIR}/include/pumex/Version.in"
  "${CMAKE_CURRENT_BINARY_DIR}/include/pumex/Version.h"
  )
include_directories(include)
include_directories( ${CMAKE_CURRENT_BINARY_DIR}/include )

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)
include_directories(${Vulkan_INCLUDE_DIRS})
  
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/lib)
foreach( OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES} )
    string( TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG )
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_CURRENT_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_CURRENT_BINARY_DIR}/bin)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_CURRENT_BINARY_DIR}/lib)
endforeach( OUTPUTCONFIG CMAKE_CONFIGURATION_TYPES )

include(ExternalProject)
include(FindPackageHandleStandardArgs)

set(GLM_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/external/glm")
ExternalProject_Add( glm-external
  PREFIX "${GLM_BUILD_DIR}"
  BINARY_DIR "${GLM_BUILD_DIR}/build"
  STAMP_DIR "${CMAKE_SOURCE_DIR}/stamp/glm"
  GIT_REPOSITORY "https://github.com/g-truc/glm.git"
  GIT_TAG "0.9.8"
  SOURCE_DIR "${CMAKE_SOURCE_DIR}/external/glm"
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  UPDATE_COMMAND ""
  PATCH_COMMAND ""
  TEST_COMMAND ""
  INSTALL_COMMAND ""
  UPDATE_DISCONNECTED 1
)
ExternalProject_Add_Step( glm-external glm-external-copy-includes
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/external/glm/glm" "${CMAKE_CURRENT_BINARY_DIR}/include/glm"
  COMMENT "Copying glm headers"
  DEPENDEES install
)

set(GLI_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/external/gli")
ExternalProject_Add( gli-external
  PREFIX "${GLI_BUILD_DIR}"
  BINARY_DIR "${GLI_BUILD_DIR}/build"
  STAMP_DIR "${CMAKE_SOURCE_DIR}/stamp/gli"
  GIT_REPOSITORY "https://github.com/g-truc/gli.git"
  GIT_TAG "0.8.2"
  SOURCE_DIR "${CMAKE_SOURCE_DIR}/external/gli"
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  UPDATE_COMMAND ""
  PATCH_COMMAND ""
  TEST_COMMAND ""
  INSTALL_COMMAND ""
  UPDATE_DISCONNECTED 1
)
ExternalProject_Add_Step( gli-external gli-external-copy-includes
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/external/gli/gli" "${CMAKE_CURRENT_BINARY_DIR}/include/gli"
  COMMENT "Copying gli headers"
  DEPENDEES install
)

set(CXXOPTS_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/external/cxxopts")
ExternalProject_Add( cxxopts-external
  PREFIX "${CXXOPTS_BUILD_DIR}"
  BINARY_DIR "${CXXOPTS_BUILD_DIR}/build"
  STAMP_DIR "${CMAKE_SOURCE_DIR}/stamp/cxxopts"
  GIT_REPOSITORY "https://github.com/jarro2783/cxxopts.git"
  GIT_TAG "v1.2.0"
  SOURCE_DIR "${CMAKE_SOURCE_DIR}/external/cxxopts"
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  UPDATE_COMMAND ""
  PATCH_COMMAND ""
  TEST_COMMAND ""
  INSTALL_COMMAND ""
  UPDATE_DISCONNECTED 1
)
ExternalProject_Add_Step( cxxopts-external cxxopts-external-copy-includes
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/external/cxxopts/include" "${CMAKE_CURRENT_BINARY_DIR}/include"
  COMMENT "Copying cxxopts headers"
  DEPENDEES install
)

set( PUMEX_DEPENDENCIES )

if(WIN32)
  set(ASSIMP_SOURCE_DIR ${CMAKE_SOURCE_DIR}/external/assimp)
  set(ASSIMP_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/external/assimp")
  ExternalProject_Add( assimp-external
    PREFIX "${ASSIMP_BUILD_DIR}"
    BINARY_DIR "${ASSIMP_BUILD_DIR}/build"
    STAMP_DIR "${CMAKE_SOURCE_DIR}/stamp/assimp"
    GIT_REPOSITORY "https://github.com/assimp/assimp.git"
    GIT_TAG "v3.3.1"
    SOURCE_DIR "${ASSIMP_SOURCE_DIR}"
    UPDATE_COMMAND ""
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR} -DASSIMP_BUILD_ASSIMP_TOOLS=off -DASSIMP_BUILD_TESTS=off -DCMAKE_DEBUG_POSTFIX=d
    UPDATE_DISCONNECTED 1
  )
  if(WIN32)
    if(MSVC12)
      set(ASSIMP_MSVC_VERSION "vc120")
    elseif(MSVC14)	
      set(ASSIMP_MSVC_VERSION "vc140")
    endif(MSVC12)
    if(MSVC12 OR MSVC14)
      set(ASSIMP_LIBRARY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/lib/assimp-${ASSIMP_MSVC_VERSION}-mt.lib)
      set(ASSIMP_LIBRARY_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/lib/assimp-${ASSIMP_MSVC_VERSION}-mtd.lib)
      set(ASSIMP_LIBRARIES optimized "${ASSIMP_LIBRARY_RELEASE}" debug "${ASSIMP_LIBRARY_DEBUG}")
    endif()
    list ( APPEND PUMEX_DEPENDENCIES assimp-external )
  else(WIN32)
# This code is inactive now.
      set(ASSIMP_LIBRARY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/lib/libassimp.so)
      set(ASSIMP_LIBRARY_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/lib/libassimpd.so)
      set(ASSIMP_LIBRARIES optimized "${ASSIMP_LIBRARY_RELEASE}" debug "${ASSIMP_LIBRARY_DEBUG}")
  endif(WIN32)
else(WIN32)
  find_package( ASSIMP REQUIRED )
#  find_package_handle_standard_args( ASSIMP REQUIRED_VARS ASSIMP_LIBRARIES )
endif(WIN32)

if( WIN32 )
  set(FREETYPE_SOURCE_DIR ${CMAKE_SOURCE_DIR}/external/freetype )
  set(FREETYPE_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/external/freetype")
  ExternalProject_Add( freetype-external
    PREFIX "${FREETYPE_BUILD_DIR}"
    BINARY_DIR "${FREETYPE_BUILD_DIR}/build"
    STAMP_DIR "${CMAKE_SOURCE_DIR}/stamp/freetype"
    GIT_REPOSITORY "git://git.sv.nongnu.org/freetype/freetype2.git"
    GIT_TAG "VER-2-8"
    SOURCE_DIR "${FREETYPE_SOURCE_DIR}"
    UPDATE_COMMAND ""
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR} -DCMAKE_DEBUG_POSTFIX=d
    UPDATE_DISCONNECTED 1
  )
  set(FREETYPE_LIBRARY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/lib/freetype.lib)
  set(FREETYPE_LIBRARY_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/lib/freetyped.lib)
  set(FREETYPE_LIBRARIES optimized "${FREETYPE_LIBRARY_RELEASE}" debug "${FREETYPE_LIBRARY_DEBUG}")
  include_directories( ${CMAKE_CURRENT_BINARY_DIR}/include/freetype2 )
  list ( APPEND PUMEX_DEPENDENCIES freetype-external )
else( WIN32 )
  find_package( Freetype REQUIRED )
  include_directories( ${FREETYPE_INCLUDE_DIRS} )
endif( WIN32 )

if(WIN32)
  set(TBB_SOURCE_DIR ${CMAKE_SOURCE_DIR}/external/tbb)
  set(TBB_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/external/tbb")
  ExternalProject_Add( tbb-external
    PREFIX "${TBB_BUILD_DIR}"
    BINARY_DIR "${TBB_BUILD_DIR}/build"
    STAMP_DIR "${CMAKE_SOURCE_DIR}/stamp/tbb"
    GIT_REPOSITORY "https://github.com/wjakob/tbb.git"
    SOURCE_DIR "${TBB_SOURCE_DIR}"
    UPDATE_COMMAND ""
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR} -DCMAKE_DEBUG_POSTFIX=_debug -DTBB_BUILD_TESTS=OFF
    UPDATE_DISCONNECTED 1
  )

  if(WIN32)
    set(TBB_LIBRARY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/lib/tbb.lib)
    set(TBB_LIBRARY_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/lib/tbb_debug.lib)
    set(TBB_LIBRARIES optimized "${TBB_LIBRARY_RELEASE}" debug "${TBB_LIBRARY_DEBUG}")
  else(WIN32)
#  find_library(TBB_LIBRARIES NAMES tbb PATHS /usr/local/lib/ )
    set(TBB_LIBRARY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/lib/libtbb.so)
    set(TBB_LIBRARY_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/lib/libtbb_debug.so)
    set(TBB_LIBRARIES optimized "${TBB_LIBRARY_RELEASE}" debug "${TBB_LIBRARY_DEBUG}")
  endif(WIN32)
  list ( APPEND PUMEX_DEPENDENCIES tbb-external )
else( WIN32 )
  find_package( TBB COMPONENTS tbbmalloc tbbmalloc_proxy )
endif( WIN32 )

# FIXME - android not implemented
if(WIN32)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_WIN32_KHR")
  set( SYSTEM_LIBRARIES ${WINLIBS} )
else(WIN32)
  find_package(X11 REQUIRED)
  find_package(XCB REQUIRED)
  find_package(Threads REQUIRED)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_XCB_KHR")
  set( SYSTEM_LIBRARIES ${XCB_LIBRARIES} ${X11_LIBRARIES} )
endif(WIN32)

# FIXME - android not implemented
file(GLOB PUMEX_SOURCE include/pumex/*.h include/pumex/utils/*.h src/pumex/*.cpp src/pumex/utils/*.cpp)
if(WIN32)
  file(GLOB PUMEX_PLATFORM_SOURCE include/pumex/platform/win32/*.h src/pumex/platform/win32/*.cpp)
else(WIN32)
  file(GLOB PUMEX_PLATFORM_SOURCE include/pumex/platform/linux/*.h src/pumex/platform/linux/*.cpp)
endif(WIN32)

file( GLOB PUMEX_SHADERS data/*.vert data/*.tesc data/*.tese data/*.geom data/*.frag data/*.comp )
set( PUMEX_SHADERS_COMPILED )
foreach( _file ${PUMEX_SHADERS} )
  set( _file_out ${_file}.spv )
  add_custom_command (OUTPUT  ${_file_out}
                      DEPENDS ${_file}
                      COMMAND glslangValidator
                      ARGS    -V ${_file} -o ${_file_out} )
  list (APPEND PUMEX_SHADERS_COMPILED ${_file_out})
endforeach()

add_custom_target (shaders DEPENDS ${PUMEX_SHADERS_COMPILED} SOURCES ${PUMEX_SHADERS})

SET(CMAKE_DEBUG_POSTFIX "d")
SET(CMAKE_RELEASE_POSTFIX "")
SET(CMAKE_RELWITHDEBINFO_POSTFIX "rd")
SET(CMAKE_MINSIZEREL_POSTFIX "s")

add_library( pumex SHARED ${PUMEX_SOURCE} ${PUMEX_PLATFORM_SOURCE} )
target_compile_definitions( pumex PRIVATE PUMEX_EXPORTS=1 )
target_compile_definitions( pumex PUBLIC NOMINMAX=1 GLM_ENABLE_EXPERIMENTAL=1 GLM_FORCE_RADIANS=1 GLM_FORCE_DEPTH_ZERO_TO_ONE=1 )
add_dependencies( pumex glm-external gli-external ${PUMEX_DEPENDENCIES} )
target_link_libraries( pumex ${SYSTEM_LIBRARIES} ${Vulkan_LIBRARIES} ${TBB_LIBRARIES} ${ASSIMP_LIBRARIES} ${FREETYPE_LIBRARIES} )

MACRO(set_executable_postfixes target)
  set_target_properties(${target} PROPERTIES DEBUG_OUTPUT_NAME "${target}${CMAKE_DEBUG_POSTFIX}")
  set_target_properties(${target} PROPERTIES RELEASE_OUTPUT_NAME "${target}${CMAKE_RELEASE_POSTFIX}")
  set_target_properties(${target} PROPERTIES RELWITHDEBINFO_OUTPUT_NAME "${target}${CMAKE_RELWITHDEBINFO_POSTFIX}")
  set_target_properties(${target} PROPERTIES MINSIZEREL_OUTPUT_NAME "${target}${CMAKE_MINSIZEREL_POSTFIX}")
ENDMACRO(set_executable_postfixes)

add_executable( pumexviewer src/pumexviewer.cpp )
add_dependencies( pumexviewer shaders cxxopts-external )
target_link_libraries( pumexviewer pumex )
set_executable_postfixes( pumexviewer )

add_executable( pumexcrowd src/pumexcrowd.cpp )
add_dependencies( pumexcrowd shaders cxxopts-external )
target_link_libraries( pumexcrowd pumex )
set_executable_postfixes( pumexcrowd )

add_executable( pumexgpucull src/pumexgpucull.cpp )
add_dependencies( pumexgpucull shaders cxxopts-external )
target_link_libraries( pumexgpucull pumex )
set_executable_postfixes( pumexgpucull )

add_executable( pumexdeferred src/pumexdeferred.cpp )
add_dependencies( pumexdeferred shaders cxxopts-external )
target_link_libraries( pumexdeferred pumex )
set_executable_postfixes( pumexdeferred )

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT pumexcrowd)
