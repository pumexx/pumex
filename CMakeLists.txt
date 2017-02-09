cmake_minimum_required(VERSION 3.7.0 FATAL_ERROR)

project( Pumex )

include_directories(include)

find_package(VULKAN REQUIRED)
include_directories(${Vulkan_INCLUDE_DIRS})

include(ExternalProject)

set(GLM_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/external/glm")
ExternalProject_Add( glm-external
  PREFIX "${GLM_BUILD_DIR}"
  BINARY_DIR "${GLM_BUILD_DIR}/build"
  STAMP_DIR "${GLM_BUILD_DIR}/stamp"
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
include_directories(${CMAKE_SOURCE_DIR}/external/glm)

set(GLI_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/external/gli")
ExternalProject_Add( gli-external
  PREFIX "${GLI_BUILD_DIR}"
  BINARY_DIR "${GLI_BUILD_DIR}/build"
  STAMP_DIR "${GLI_BUILD_DIR}/stamp"
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
include_directories(${CMAKE_SOURCE_DIR}/external/gli)

set(ASSIMP_SOURCE_DIR ${CMAKE_SOURCE_DIR}/external/assimp)
set(ASSIMP_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/external/assimp")
ExternalProject_Add( assimp-external
  PREFIX "${ASSIMP_BUILD_DIR}"
  BINARY_DIR "${ASSIMP_BUILD_DIR}/build"
  STAMP_DIR "${ASSIMP_BUILD_DIR}/stamp"
  GIT_REPOSITORY "https://github.com/assimp/assimp.git"
  GIT_TAG "v3.3.1"
  SOURCE_DIR "${ASSIMP_SOURCE_DIR}"
  UPDATE_COMMAND ""
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR} -DASSIMP_BUILD_ASSIMP_TOOLS=off -DASSIMP_BUILD_TESTS=off -DCMAKE_DEBUG_POSTFIX=d
  UPDATE_DISCONNECTED 1
)

ExternalProject_Add_Step( assimp-external assimp-external-copy-binaries
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_BINARY_DIR}/bin" "${CMAKE_CURRENT_BINARY_DIR}"
  COMMENT "Copying dlls to build folder"
  DEPENDEES install
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
else(WIN32)
	find_library(ASSIMP_LIBRARIES NAMES assimp PATHS /usr/local/lib/ )
endif(WIN32)

set(TBB_SOURCE_DIR ${CMAKE_SOURCE_DIR}/external/tbb)
set(TBB_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/external/tbb")
ExternalProject_Add( tbb-external
  PREFIX "${TBB_BUILD_DIR}"
  BINARY_DIR "${TBB_BUILD_DIR}/build"
  STAMP_DIR "${TBB_BUILD_DIR}/stamp"
  GIT_REPOSITORY "https://github.com/wjakob/tbb.git"
  SOURCE_DIR "${TBB_SOURCE_DIR}"
  UPDATE_COMMAND ""
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}
  UPDATE_DISCONNECTED 1
)

if(WIN32)
  set(TBB_LIBRARY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/lib/tbb.lib)
  set(TBB_LIBRARY_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/lib/tbb_debug.lib)
  set(TBB_LIBRARIES optimized "${TBB_LIBRARY_RELEASE}" debug "${TBB_LIBRARY_DEBUG}")
else(WIN32)
	find_library(TBB_LIBRARIES NAMES tbb PATHS /usr/local/lib/ )
endif(WIN32)



include_directories(${CMAKE_CURRENT_BINARY_DIR}/include)


# FIXME - android not implemented, linux never tested
if(WIN32)
#	find_library(ASSIMP_LIBRARIES NAMES assimp libassimp.dll.a PATHS ${CMAKE_SOURCE_DIR}/lib)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_WIN32_KHR")
else(WIN32)
#	find_package(ASSIMP REQUIRED)
	find_package(XCB REQUIRED)
	find_package(Threads REQUIRED)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_XCB_KHR")
endif(WIN32)

#add_definitions(-D_CRT_SECURE_NO_WARNINGS)
#add_definitions(-std=c++14)
#add_definitions(-std=c++0x)
#add_definitions(-std=gnu++14)

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
                      COMMAND glslangvalidator
                      ARGS    -V ${_file} -o ${_file_out} )
	list (APPEND PUMEX_SHADERS_COMPILED ${_file_out})
endforeach()
add_custom_target (shaders DEPENDS ${PUMEX_SHADERS_COMPILED} SOURCES ${PUMEX_SHADERS})

add_library( pumex SHARED ${PUMEX_SOURCE} ${PUMEX_PLATFORM_SOURCE} )
target_compile_definitions( pumex PRIVATE PUMEX_EXPORTS=1 )
target_compile_definitions( pumex PUBLIC NOMINMAX=1 GLM_ENABLE_EXPERIMENTAL=1 GLM_FORCE_RADIANS=1 GLM_FORCE_DEPTH_ZERO_TO_ONE=1 )
add_dependencies( pumex glm-external gli-external assimp-external )
target_link_libraries( pumex ${WINLIBS} ${TBB_LIBRARIES} ${Vulkan_LIBRARIES} ${ASSIMP_LIBRARIES} )

add_executable( pumexgpucull src/pumexgpucull.cpp )
add_dependencies( pumexgpucull shaders )
target_link_libraries( pumexgpucull pumex )

add_executable( pumexviewer src/pumexviewer.cpp )
add_dependencies( pumexviewer shaders )
target_link_libraries( pumexviewer pumex )

add_executable( crowd src/crowd.cpp )
add_dependencies( crowd shaders )
target_link_libraries( crowd pumex )
