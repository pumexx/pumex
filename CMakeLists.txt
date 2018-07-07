cmake_minimum_required(VERSION 3.7.0 FATAL_ERROR)

project( PUMEX LANGUAGES CXX VERSION 0.1.0 )
set( PUMEX_SOVERSION 1 )

message( STATUS "Building PUMEX library version ${PUMEX_VERSION_MAJOR}.${PUMEX_VERSION_MINOR}.${PUMEX_VERSION_PATCH}" )

option( PUMEX_BUILD_EXAMPLES             "Build examples" ON )
if(WIN32)
  option( PUMEX_DOWNLOAD_EXTERNAL_GLM      "Download GLM library"       ON )
  option( PUMEX_DOWNLOAD_EXTERNAL_GLI      "Download GLI library"       ON )
  option( PUMEX_DOWNLOAD_EXTERNAL_ARGS     "Download ARGS library"      ON )
  option( PUMEX_DOWNLOAD_EXTERNAL_ASSIMP   "Download Assimp library"    ON )
  option( PUMEX_DOWNLOAD_EXTERNAL_FREETYPE "Download Freetype2 library" ON )
  option( PUMEX_DOWNLOAD_EXTERNAL_TBB      "Download TBB library"       ON )
else()
  option( PUMEX_DOWNLOAD_EXTERNAL_GLM      "Download GLM library"       ON )
  option( PUMEX_DOWNLOAD_EXTERNAL_GLI      "Download GLI library"       ON )
  option( PUMEX_DOWNLOAD_EXTERNAL_ARGS     "Download ARGS library"      ON )
  option( PUMEX_DOWNLOAD_EXTERNAL_ASSIMP   "Download Assimp library"    OFF )
  option( PUMEX_DOWNLOAD_EXTERNAL_FREETYPE "Download Freetype2 library" OFF )
  option( PUMEX_DOWNLOAD_EXTERNAL_TBB      "Download TBB library"       OFF )
endif()


list( APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" )
list( APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake/Modules" )

set( CMAKE_CXX_STANDARD 14 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )
set( CMAKE_CXX_EXTENSIONS OFF )

configure_file (
  "${PROJECT_SOURCE_DIR}/include/pumex/Version.in"
  "${CMAKE_BINARY_DIR}/include/pumex/Version.h"
  )

include( pumex_macros )
include( pumex_externals )

set( pumexlib_input_shaders 
  shaders/text_draw.vert
  shaders/text_draw.geom
  shaders/text_draw.frag
)
process_shaders( ${CMAKE_CURRENT_LIST_DIR} pumexlib_input_shaders pumexlib_output_shaders )
add_custom_target ( pumexlib-shaders DEPENDS ${pumexlib_output_shaders} SOURCES ${pumexlib_input_shaders} )

add_library( pumexlib SHARED "" )
target_sources( pumexlib PUBLIC 
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/Asset.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/AssetBuffer.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/AssetBufferNode.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/AssetLoaderAssimp.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/AssetNode.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/BoundingBox.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/Camera.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/CombinedImageSampler.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/Command.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/Descriptor.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/Device.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/DeviceMemoryAllocator.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/DispatchNode.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/Export.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/FrameBuffer.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/HPClock.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/Image.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/InputAttachment.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/Kinematic.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/MaterialSet.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/MemoryBuffer.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/MemoryImage.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/MemoryObject.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/MemoryObjectBarrier.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/Node.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/NodeVisitor.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/PerObjectData.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/PhysicalDevice.h 
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/Pipeline.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/Pumex.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/Query.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/RenderContext.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/RenderPass.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/RenderVisitors.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/RenderWorkflow.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/Resource.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/SampledImage.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/Sampler.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/StorageBuffer.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/StorageImage.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/Surface.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/Text.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/TextureLoaderGli.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/UniformBuffer.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/Viewer.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/Window.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/utils/ActionQueue.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/utils/Buffer.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/utils/HashCombine.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/utils/Log.h
  ${CMAKE_CURRENT_LIST_DIR}/include/pumex/utils/Shapes.h  
  ${CMAKE_BINARY_DIR}/include/pumex/Version.h
)
target_sources( pumexlib PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/Asset.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/AssetBuffer.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/AssetBufferNode.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/AssetLoaderAssimp.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/AssetNode.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/BoundingBox.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/Camera.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/CombinedImageSampler.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/Command.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/Descriptor.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/Device.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/DeviceMemoryAllocator.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/DispatchNode.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/FrameBuffer.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/Image.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/InputAttachment.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/Kinematic.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/MaterialSet.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/MemoryBuffer.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/MemoryImage.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/MemoryObject.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/MemoryObjectBarrier.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/Node.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/NodeVisitor.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/PerObjectData.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/PhysicalDevice.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/Pipeline.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/Query.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/RenderContext.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/RenderPass.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/RenderVisitors.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/RenderWorkflow.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/Resource.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/SampledImage.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/Sampler.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/StorageBuffer.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/StorageImage.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/Surface.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/Text.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/TextureLoaderGli.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/UniformBuffer.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/Viewer.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/Window.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/utils/Buffer.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/utils/Log.cpp
  ${CMAKE_CURRENT_LIST_DIR}/src/pumex/utils/Shapes.cpp
)
if(WIN32)
  target_sources( pumexlib PUBLIC 
    ${CMAKE_CURRENT_LIST_DIR}/include/pumex/platform/win32/HPClockWin32.h
    ${CMAKE_CURRENT_LIST_DIR}/include/pumex/platform/win32/WindowWin32.h
  )
  target_sources( pumexlib PRIVATE 
    ${CMAKE_CURRENT_LIST_DIR}/src/pumex/platform/win32/HPClockWin32.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/pumex/platform/win32/WindowWin32.cpp
  )
elseif(UNIX AND NOT APPLE)
  target_sources( pumexlib PUBLIC 
    ${CMAKE_CURRENT_LIST_DIR}/include/pumex/platform/linux/WindowXcb.h
  )
  target_sources( pumexlib PRIVATE 
    ${CMAKE_CURRENT_LIST_DIR}/src/pumex/platform/linux/WindowXcb.cpp
  )
endif()

target_include_directories( pumexlib PUBLIC  ${PUMEXLIB_PUBLIC_INCLUDES} )
target_include_directories( pumexlib PRIVATE ${PUMEXLIB_PRIVATE_INCLUDES} )
target_compile_definitions( pumexlib PRIVATE PUMEX_EXPORTS )
target_compile_definitions( pumexlib PUBLIC NOMINMAX GLM_ENABLE_EXPERIMENTAL GLM_FORCE_RADIANS GLM_FORCE_DEPTH_ZERO_TO_ONE )
if(WIN32)
  target_compile_definitions( pumexlib PUBLIC VK_USE_PLATFORM_WIN32_KHR )
elseif(UNIX AND NOT APPLE)
  target_compile_definitions( pumexlib PUBLIC VK_USE_PLATFORM_XCB_KHR )
endif()
add_dependencies( pumexlib pumexlib-shaders ${PUMEXLIB_EXTERNALS} )
set_target_properties( pumexlib PROPERTIES VERSION ${PUMEX_VERSION} SOVERSION ${PUMEX_SOVERSION} )
set_target_properties( pumexlib PROPERTIES RESOURCE "${pumexlib_output_shaders}" )
target_link_libraries( pumexlib ${PUMEXLIB_LIBRARIES} )
set_executable_postfixes( pumexlib )

install( TARGETS pumexlib
         ARCHIVE DESTINATION lib 
         LIBRARY DESTINATION lib 
         RUNTIME DESTINATION bin 
         INCLUDES DESTINATION include
         RESOURCE DESTINATION share/pumex/shaders
         )
install( FILES ${PUMEX_INSTALL_LIBRARIES} DESTINATION lib )
install( DIRECTORY ${PROJECT_SOURCE_DIR}/include/ DESTINATION include FILES_MATCHING PATTERN "*.h" )
install( DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include/ DESTINATION include )
install( DIRECTORY ${PROJECT_SOURCE_DIR}/data/fonts DESTINATION share/pumex )
install( DIRECTORY ${PROJECT_SOURCE_DIR}/data/people DESTINATION share/pumex )
install( DIRECTORY ${PROJECT_SOURCE_DIR}/data/sponza DESTINATION share/pumex )

if( PUMEX_BUILD_EXAMPLES )
  add_subdirectory( examples )
endif()
