cmake_minimum_required(VERSION 3.11.0 FATAL_ERROR)

project( LIBPUMEX LANGUAGES CXX VERSION 0.2.0 )
set( LIBPUMEX_SOVERSION 1 )

message( STATUS "Configuring PUMEX library version ${LIBPUMEX_VERSION_MAJOR}.${LIBPUMEX_VERSION_MINOR}.${LIBPUMEX_VERSIONPUMEX_VERSION_PATCH}" )
# We have to enable assembly language at this stage so that TBB will be able to compile its assembly files from within LIBPUMEX solution
if(WIN32 AND MSVC)
  enable_language(ASM)
endif()

if ( UNIX AND NOT APPLE AND NOT ANDROID AND NOT WEBGL )
  set( LINUX TRUE )
else()
  set( LINUX FALSE )
endif()

if (POLICY CMP0076)
  cmake_policy(SET CMP0076 OLD)
endif()

if(ANDROID)
  option( PUMEX_BUILD_EXAMPLES             "Build examples" OFF )
else()
  option( PUMEX_BUILD_EXAMPLES             "Build examples" ON )
endif()
option( PUMEX_BUILD_QT                   "Build Pumex with QT support" OFF )
option( PUMEX_BUILD_TEXTURE_LOADERS      "Build Pumex with PNG, JPEG, etc support" ON )
if(WIN32)
  option( PUMEX_DOWNLOAD_EXTERNAL_GLM      "Download GLM library"       ON )
  option( PUMEX_DOWNLOAD_EXTERNAL_GLI      "Download GLI library"       ON )
  option( PUMEX_DOWNLOAD_EXTERNAL_ARGS     "Download ARGS library"      ON )
  option( PUMEX_DOWNLOAD_EXTERNAL_ASSIMP   "Download Assimp library"    ON )
  option( PUMEX_DOWNLOAD_EXTERNAL_FREETYPE "Download Freetype2 library" ON )
  option( PUMEX_DOWNLOAD_EXTERNAL_TBB      "Download TBB library"       ON )
  option( PUMEX_DOWNLOAD_EXTERNAL_ZLIB     "Download ZLIB library"      ON )
  option( PUMEX_DOWNLOAD_EXTERNAL_PNG      "Download PNG library"       ON )
  option( PUMEX_DOWNLOAD_EXTERNAL_JPEG     "Download JPEG library"      ON )
elseif(ANDROID)
  option( PUMEX_DOWNLOAD_EXTERNAL_GLM      "Download GLM library"       ON )
  option( PUMEX_DOWNLOAD_EXTERNAL_GLI      "Download GLI library"       ON )
  option( PUMEX_DOWNLOAD_EXTERNAL_ARGS     "Download ARGS library"      ON )
  option( PUMEX_DOWNLOAD_EXTERNAL_ASSIMP   "Download Assimp library"    ON )
  option( PUMEX_DOWNLOAD_EXTERNAL_FREETYPE "Download Freetype2 library" ON )
  option( PUMEX_DOWNLOAD_EXTERNAL_TBB      "Download TBB library"       ON )
  option( PUMEX_DOWNLOAD_EXTERNAL_ZLIB     "Download ZLIB library"      OFF )
  option( PUMEX_DOWNLOAD_EXTERNAL_PNG      "Download PNG library"       ON )
  option( PUMEX_DOWNLOAD_EXTERNAL_JPEG     "Download JPEG library"      ON )
elseif(LINUX)
  option( PUMEX_DOWNLOAD_EXTERNAL_GLM      "Download GLM library"       ON )
  option( PUMEX_DOWNLOAD_EXTERNAL_GLI      "Download GLI library"       ON )
  option( PUMEX_DOWNLOAD_EXTERNAL_ARGS     "Download ARGS library"      ON )
  option( PUMEX_DOWNLOAD_EXTERNAL_ASSIMP   "Download Assimp library"    OFF )
  option( PUMEX_DOWNLOAD_EXTERNAL_FREETYPE "Download Freetype2 library" OFF )
  option( PUMEX_DOWNLOAD_EXTERNAL_TBB      "Download TBB library"       OFF )
  option( PUMEX_DOWNLOAD_EXTERNAL_ZLIB     "Download ZLIB library"      OFF )
  option( PUMEX_DOWNLOAD_EXTERNAL_PNG      "Download PNG library"       OFF )
  option( PUMEX_DOWNLOAD_EXTERNAL_JPEG     "Download JPEG library"      OFF )
endif()

list( APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" )
list( APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules" )

set( CMAKE_CXX_STANDARD 14 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )
set( CMAKE_CXX_EXTENSIONS OFF )

configure_file (
  "${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/Version.in"
  "${CMAKE_CURRENT_BINARY_DIR}/include/pumex/Version.h"
  )

include( pumex_macros )
include( pumex_externals )

set( PUMEX_SHADER_NAMES
  shaders/text_draw.vert
  shaders/text_draw.geom
  shaders/text_draw.frag
  shaders/stat_draw.vert
  shaders/stat_draw.frag
)
process_shaders( ${CMAKE_CURRENT_LIST_DIR} PUMEX_SHADER_NAMES PUMEX_INPUT_SHADERS PUMEX_OUTPUT_SHADERS )
add_custom_target ( shaders-pumex DEPENDS ${PUMEX_OUTPUT_SHADERS} SOURCES ${PUMEX_INPUT_SHADERS} )
add_custom_command(TARGET shaders-pumex PRE_BUILD COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders")

set( PUMEX_HEADERS )
set( PUMEX_HEADERS_QT )
list( APPEND PUMEX_HEADERS
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/Asset.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/AssetBuffer.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/AssetBufferNode.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/AssetLoaderAssimp.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/AssetNode.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/BlitImageNode.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/BoundingBox.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/Camera.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/CombinedImageSampler.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/Command.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/CopyNode.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/Descriptor.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/Device.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/DeviceMemoryAllocator.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/DispatchNode.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/DrawNode.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/DrawVerticesNode.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/Export.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/FrameBuffer.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/HPClock.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/Image.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/InputAttachment.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/InputEvent.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/Kinematic.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/MaterialSet.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/MemoryBuffer.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/MemoryImage.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/MemoryObject.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/MemoryObjectBarrier.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/Node.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/NodeVisitor.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/PerObjectData.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/PhysicalDevice.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/Pipeline.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/Pumex.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/Query.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/Queue.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/RenderContext.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/RenderPass.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/RenderVisitors.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/RenderGraph.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/RenderGraphCompiler.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/RenderGraphExecution.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/Resource.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/ResourceRange.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/SampledImage.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/Sampler.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/StandardHandlers.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/StorageBuffer.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/StorageImage.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/Surface.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/Text.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/TextureLoaderGli.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/TimeStatistics.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/UniformBuffer.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/Viewer.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/Window.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/utils/ActionQueue.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/utils/Buffer.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/utils/HashCombine.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/utils/Log.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/utils/Shapes.h
  ${CMAKE_CURRENT_BINARY_DIR}/include/pumex/Version.h
)
if(WIN32)
  list( APPEND PUMEX_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/platform/win32/HPClockWin32.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/platform/win32/WindowWin32.h
  )
elseif(LINUX)
  list( APPEND PUMEX_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/platform/linux/WindowXcb.h
  )  
elseif(ANDROID)
  list( APPEND PUMEX_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/platform/android/WindowAndroid.h
  )  
endif()
if(PUMEX_BUILD_QT)
  list( APPEND PUMEX_HEADERS_QT
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/platform/qt/WindowQT.h
  )
endif()
if(PUMEX_BUILD_TEXTURE_LOADERS)
  list( APPEND PUMEX_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/TextureLoaderPNG.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pumex/TextureLoaderJPEG.h
  )
endif()

set( PUMEX_SOURCES )
set( PUMEX_SOURCES_QT )
list( APPEND PUMEX_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/Asset.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/AssetBuffer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/AssetBufferNode.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/AssetLoaderAssimp.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/AssetNode.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/BlitImageNode.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/BoundingBox.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/Camera.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/CombinedImageSampler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/Command.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/CopyNode.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/Descriptor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/Device.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/DeviceMemoryAllocator.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/DispatchNode.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/DrawNode.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/DrawVerticesNode.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/FrameBuffer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/Image.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/InputEvent.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/InputAttachment.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/Kinematic.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/MaterialSet.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/MemoryBuffer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/MemoryImage.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/MemoryObject.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/MemoryObjectBarrier.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/Node.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/NodeVisitor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/PerObjectData.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/PhysicalDevice.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/Pipeline.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/Query.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/Queue.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/RenderContext.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/RenderPass.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/RenderVisitors.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/RenderGraph.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/RenderGraphCompiler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/RenderGraphExecution.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/Resource.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/ResourceRange.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/SampledImage.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/Sampler.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/StandardHandlers.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/StorageBuffer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/StorageImage.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/Surface.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/Text.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/TextureLoaderGli.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/TimeStatistics.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/UniformBuffer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/Viewer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/Window.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/utils/Buffer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/utils/Log.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/utils/Shapes.cpp
)
if(WIN32)
  list( APPEND PUMEX_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/platform/win32/HPClockWin32.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/platform/win32/WindowWin32.cpp
  )
elseif(LINUX)
  list( APPEND PUMEX_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/platform/linux/WindowXcb.cpp
  )
elseif(ANDROID)
  list( APPEND PUMEX_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/platform/android/WindowAndroid.cpp
  )
endif()
if(PUMEX_BUILD_QT)
  list( APPEND PUMEX_SOURCES_QT
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/platform/qt/WindowQT.cpp
  )
endif()
if(PUMEX_BUILD_TEXTURE_LOADERS)
  list( APPEND PUMEX_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/TextureLoaderPNG.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pumex/TextureLoaderJPEG.cpp
  )
endif()

set( PUMEX_SOURCES_QT_MOC )
if( PUMEX_BUILD_QT )
  qt5_wrap_cpp( PUMEX_SOURCES_QT_MOC ${PUMEX_HEADERS_QT} )
endif()

add_library( pumex SHARED "" )
target_sources( pumex
  PUBLIC
    $<BUILD_INTERFACE: ${PUMEX_HEADERS} ${PUMEX_HEADERS_QT}>
  PRIVATE
    ${PUMEX_SOURCES}
    ${PUMEX_SOURCES_QT}
	${PUMEX_SOURCES_QT_MOC}
)
target_include_directories( pumex
  PUBLIC
    $<BUILD_INTERFACE:${PUMEX_PUBLIC_INCLUDES}>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    $<BUILD_INTERFACE:${PUMEX_PRIVATE_INCLUDES}>
)
target_compile_definitions( pumex
  PUBLIC
    NOMINMAX GLM_ENABLE_EXPERIMENTAL GLM_FORCE_RADIANS GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_CTOR_INIT
  PRIVATE
    PUMEX_EXPORTS
)
if(WIN32)
  target_compile_definitions( pumex PUBLIC VK_USE_PLATFORM_WIN32_KHR )
elseif(LINUX)
  target_compile_definitions( pumex PUBLIC VK_USE_PLATFORM_XCB_KHR )
elseif(ANDROID)
  target_compile_definitions( pumex PUBLIC VK_USE_PLATFORM_ANDROID_KHR )
endif()

if(PUMEX_BUILD_TEXTURE_LOADERS)
  target_compile_definitions( pumex PRIVATE PUMEX_BUILD_TEXTURE_LOADERS )
endif()

add_dependencies( pumex shaders-pumex )
set_target_properties( pumex PROPERTIES 
  VERSION   ${LIBPUMEX_VERSION} 
  SOVERSION ${LIBPUMEX_SOVERSION}
  RESOURCE  "${PUMEX_OUTPUT_SHADERS}"
)
target_link_libraries( pumex 
  PUBLIC
    ${PUMEX_LIBRARIES_PUBLIC_DOWNLOADED} 
    ${PUMEX_LIBRARIES_PUBLIC} 
  PRIVATE
    ${PUMEX_LIBRARIES_PRIVATE_DOWNLOADED} 
    ${PUMEX_LIBRARIES_PRIVATE} 
)
set_target_postfixes( pumex )

if( PUMEX_BUILD_EXAMPLES )
  add_subdirectory( examples )
endif()

#install( TARGETS pumex 
#         EXPORT PumexTargets
#         ARCHIVE DESTINATION lib COMPONENT libraries
#         LIBRARY DESTINATION lib COMPONENT libraries
#         RUNTIME DESTINATION bin COMPONENT libraries
#         RESOURCE DESTINATION share/pumex/shaders COMPONENT libraries
#         )
#install( DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/ DESTINATION include COMPONENT headers FILES_MATCHING PATTERN "*.h"  )
#install( FILES ${CMAKE_CURRENT_BINARY_DIR}/include/pumex/Version.h DESTINATION include/pumex COMPONENT headers )
#install( DIRECTORY ${INTERMEDIATE_INSTALL_DIR}/include/ DESTINATION include COMPONENT dependencies )
#install( DIRECTORY ${INTERMEDIATE_INSTALL_DIR}/bin/ DESTINATION bin COMPONENT dependencies )
#install( DIRECTORY ${INTERMEDIATE_INSTALL_DIR}/lib/ DESTINATION lib COMPONENT dependencies )
#install( DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data/fonts/ DESTINATION share/pumex/fonts COMPONENT examples )
#install( DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data/people/ DESTINATION share/pumex/people COMPONENT examples )
#install( DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data/sponza/ DESTINATION share/pumex/sponza COMPONENT examples )
#install( EXPORT PumexTargets
#  FILE pumex-targets.cmake
#  NAMESPACE pumex::
#  DESTINATION lib/cmake/pumex
#  COMPONENT cmake_files
#)
#install( FILES ${CMAKE_CURRENT_SOURCE_DIR}/cmake/pumex-config.cmake DESTINATION lib/cmake/pumex COMPONENT cmake_files )
#
#include( CMakePackageConfigHelpers )
#write_basic_package_version_file( ${CMAKE_CURRENT_BINARY_DIR}/pumex-config-version.cmake
#  VERSION ${LIBPUMEX_VERSION}
#  COMPATIBILITY SameMajorVersion
#)
#install( FILES ${CMAKE_CURRENT_BINARY_DIR}/pumex-config-version.cmake DESTINATION lib/cmake/pumex COMPONENT cmake_files )
#
#set( CPACK_PACKAGE_NAME "pumex" )
#set( CPACK_PACKAGE_VENDOR "CMake.org" )
#set( CPACK_PACKAGE_DESCRIPTION_SUMMARY "Pumex - Vulkan library oriented on high speed rendering" )
#set( CPACK_PACKAGE_VERSION ${LIBPUMEX_VERSION} )
#set( CPACK_PACKAGE_VERSION_MAJOR ${LIBPUMEX_VERSION_MAJOR} )
#set( CPACK_PACKAGE_VERSION_MINOR ${LIBPUMEX_VERSION_MINOR} )
#set( CPACK_PACKAGE_VERSION_PATCH ${LIBPUMEX_VERSION_PATCH} )
#set( CPACK_PACKAGE_INSTALL_DIRECTORY "Pumex" )
#set( CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE" )
#
#include(CPack)
#
#cpack_add_install_type( Full      DISPLAY_NAME "Full install ( development files with examples )" )
#cpack_add_install_type( Developer DISPLAY_NAME "Development files WITHOUT examples" )
#
#cpack_add_component_group( DevGroup DISPLAY_NAME "Development files" DESCRIPTION "All files required to develop your applications using Pumex library" )
#cpack_add_component_group( ExampleApps DISPLAY_NAME "Example applications" DESCRIPTION "Example applications show Pumex library in action" )
#
#cpack_add_component( dependencies
#  DISPLAY_NAME "3rd party libraries"
#  DESCRIPTION "Libraries that must be installed in order to run Pumex library"
#  GROUP DevGroup
#  INSTALL_TYPES Developer Full
#)
#cpack_add_component( libraries
#  DISPLAY_NAME "Library files"
#  DESCRIPTION "Dynamic libraries used to build applications with Pumex library. Libraries are also used by example applications"
#  GROUP DevGroup
#  INSTALL_TYPES Developer Full
#  DEPENDS dependencies
#)
#cpack_add_component( headers
#  DISPLAY_NAME "Header files"
#  DESCRIPTION "Header files required to write and build applications with Pumex library"
#  GROUP DevGroup
#  INSTALL_TYPES Developer Full
#  DEPENDS libraries
#)
#cpack_add_component( cmake_files
#  DISPLAY_NAME "CMake configuration files"
#  DESCRIPTION "Files that help find Pumex library and use it in your application"
#  GROUP DevGroup
#  INSTALL_TYPES Developer Full
#  DEPENDS libraries
#)
#cpack_add_component( examples
#  DISPLAY_NAME "Example applications"
#  DESCRIPTION "Example applications show Pumex library in action"
#  GROUP ExampleApps
#  INSTALL_TYPES Full
#  DEPENDS libraries
#)
