set( PUMEX_PUBLIC_INCLUDES )
set( PUMEX_EXAMPLES_INCLUDES )
set( PUMEX_LIBRARIES_PUBLIC_DOWNLOADED )
set( PUMEX_LIBRARIES_PUBLIC )
set( PUMEX_LIBRARIES_PRIVATE_DOWNLOADED )
set( PUMEX_LIBRARIES_PRIVATE )
set( PUMEX_LIBRARIES_EXAMPLES )
set( PUMEX_EXTERNAL_TARGETS_INSTALL )

if( LINUX )
  find_package( X11 REQUIRED )
  find_package( XCB REQUIRED )
  find_package( Threads REQUIRED )
  list( APPEND PUMEX_PUBLIC_INCLUDES ${X11_INCLUDE_DIR} ${XCB_INCLUDE_DIRS} )
  list( APPEND PUMEX_LIBRARIES_PUBLIC ${X11_LIBRARIES} ${XCB_LIBRARIES} Threads::Threads )
endif()

find_package( Vulkan REQUIRED )
list( APPEND PUMEX_LIBRARIES_PUBLIC Vulkan::Vulkan )
list( APPEND PUMEX_PUBLIC_INCLUDES "." )
list( APPEND PUMEX_PUBLIC_INCLUDES ${Vulkan_INCLUDE_DIRS} )

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  SET(EXPERIMENTAL_FILESYSTEM_LIBRARIES stdc++fs)
elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
  SET(EXPERIMENTAL_FILESYSTEM_LIBRARIES c++experimental)
else()
  SET(EXPERIMENTAL_FILESYSTEM_LIBRARIES )
endif()
list( APPEND PUMEX_LIBRARIES_PUBLIC ${EXPERIMENTAL_FILESYSTEM_LIBRARIES} )

include( FetchContent )

set( CMAKE_DEBUG_POSTFIX "d" CACHE STRING "Overriden by Pumex" )
if( PUMEX_DOWNLOAD_EXTERNAL_GLM )
  message( STATUS "Fetching content from GLM library")
  set( GLM_TEST_ENABLE off CACHE BOOL "Overriden by Pumex" )
  FetchContent_Declare(
    glm
    GIT_REPOSITORY "https://github.com/g-truc/glm.git"
    GIT_TAG        "0.9.9.3"
  )
  FetchContent_GetProperties(glm)
  if(NOT glm_POPULATED)
    FetchContent_Populate(glm)
    add_subdirectory( ${glm_SOURCE_DIR} ${glm_BINARY_DIR} )
    add_library( glm::glm ALIAS glm )
  endif()
  list( APPEND PUMEX_LIBRARIES_PUBLIC_DOWNLOADED glm::glm )
  list( APPEND PUMEX_PUBLIC_INCLUDES ${glm_SOURCE_DIR} )
  list( APPEND PUMEX_EXTERNAL_TARGETS_INSTALL glm )
else()
  find_package( glm REQUIRED )
  list( APPEND PUMEX_LIBRARIES_PUBLIC glm::glm )
endif()

if( PUMEX_DOWNLOAD_EXTERNAL_GLI )
  message( STATUS "Fetching content from GLI library")
  set( GLI_TEST_ENABLE off CACHE BOOL "Overriden by Pumex" )
  FetchContent_Declare(
    gli
    GIT_REPOSITORY "https://github.com/g-truc/gli.git"
    PATCH_COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/external/gli_fix/CMakeLists.txt" "${FETCHCONTENT_BASE_DIR}/gli-src/CMakeLists.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/external/gli_fix/test/CMakeLists.txt" "${FETCHCONTENT_BASE_DIR}/gli-src/test/CMakeLists.txt"
  )
  FetchContent_GetProperties(gli)
  if(NOT gli_POPULATED)
    FetchContent_Populate(gli)
    add_subdirectory( ${gli_SOURCE_DIR} ${gli_BINARY_DIR} )
    add_library( gli::gli ALIAS gli )
  endif()
  list( APPEND PUMEX_LIBRARIES_PUBLIC_DOWNLOADED gli::gli )
  list( APPEND PUMEX_PUBLIC_INCLUDES ${gli_SOURCE_DIR} )
  list( APPEND PUMEX_EXTERNAL_TARGETS_INSTALL gli )
else()
  find_package( gli REQUIRED )
  list( APPEND PUMEX_LIBRARIES_PUBLIC gli::gli )
endif()

if( PUMEX_BUILD_EXAMPLES )
  if( PUMEX_DOWNLOAD_EXTERNAL_ARGS )
    message( STATUS "Fetching content from ARGS library")
    set( ARGS_BUILD_UNITTESTS off CACHE BOOL "Overriden by Pumex" )
    set( ARGS_BUILD_EXAMPLE off CACHE BOOL "Overriden by Pumex" )
    FetchContent_Declare(
      args
      GIT_REPOSITORY "https://github.com/Taywee/args.git"
    )
    FetchContent_GetProperties(args)
    if(NOT args_POPULATED)
      FetchContent_Populate(args)
      add_subdirectory( ${args_SOURCE_DIR} ${args_BINARY_DIR} )
      add_library( args::args ALIAS args )
    endif()
    list( APPEND PUMEX_LIBRARIES_EXAMPLES args::args )
    list( APPEND PUMEX_EXAMPLES_INCLUDES ${args_SOURCE_DIR} )
    list( APPEND PUMEX_EXTERNAL_TARGETS_INSTALL args )
  else()
    find_package( args REQUIRED )
    list( APPEND PUMEX_LIBRARIES_PUBLIC args::args )
  endif()
endif()

if(PUMEX_DOWNLOAD_EXTERNAL_ZLIB)
  message( STATUS "Fetching content from ZLIB library")
  FetchContent_Declare(
    zlib
    GIT_REPOSITORY "https://github.com/madler/zlib.git"
    GIT_TAG "v1.2.11"
  )
  FetchContent_GetProperties(zlib)
  if(NOT zlib_POPULATED)
    FetchContent_Populate(zlib)
    add_subdirectory( ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR} )
    add_library( zlib::zlib ALIAS zlib )
  endif()
  list( APPEND PUMEX_LIBRARIES_PUBLIC_DOWNLOADED zlib::zlib )
  list( APPEND PUMEX_PUBLIC_INCLUDES ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR} )
  list( APPEND PUMEX_EXTERNAL_TARGETS_INSTALL zlib )

  if(WIN32)
    set( ZLIB_LIBRARY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/lib/zlib.lib )
    set( ZLIB_LIBRARY_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/lib/zlibd.lib )
  else()
    set( ZLIB_LIBRARY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/lib/zlib.so )
    set( ZLIB_LIBRARY_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/lib/zlibd.so )
  endif()
  set( ZLIB_LIBRARY optimized ${ZLIB_LIBRARY_RELEASE} debug ${ZLIB_LIBRARY_DEBUG})
  set( ZLIB_INCLUDE_DIR ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR} )
else()
  find_package( ZLIB REQUIRED )
  list( APPEND PUMEX_PUBLIC_INCLUDES ${ZLIB_INCLUDE_DIR} )
  list( APPEND PUMEX_LIBRARIES_PUBLIC ${ZLIB_LIBRARIES} )
endif()
set( ASSIMP_BUILD_ZLIB off CACHE BOOL "Overriden by Pumex" )
set( PNG_BUILD_ZLIB off CACHE BOOL "Overriden by Pumex" )

if( PUMEX_DOWNLOAD_EXTERNAL_ASSIMP )
  message( STATUS "Fetching content from ASSIMP library")
  set( ASSIMP_BUILD_TESTS off CACHE BOOL "Overriden by Pumex" )
  set( ASSIMP_BUILD_ASSIMP_TOOLS off CACHE BOOL "Overriden by Pumex" )
  set( ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT off CACHE BOOL "Overriden by Pumex" )
  set( ASSIMP_BUILD_3DS_IMPORTER on CACHE BOOL "Overriden by Pumex" )
  set( ASSIMP_BUILD_BVH_IMPORTER on CACHE BOOL "Overriden by Pumex" )
  set( ASSIMP_BUILD_COLLADA_IMPORTER on CACHE BOOL "Overriden by Pumex" )
  set( ASSIMP_BUILD_LWO_IMPORTER on CACHE BOOL "Overriden by Pumex" )
  set( ASSIMP_BUILD_OBJ_IMPORTER on CACHE BOOL "Overriden by Pumex" )
  set( ASSIMP_BUILD_PLY_IMPORTER on CACHE BOOL "Overriden by Pumex" )
  set( ASSIMP_BUILD_FBX_IMPORTER on CACHE BOOL "Overriden by Pumex" )
  set( ASSIMP_BUILD_STL_IMPORTER on CACHE BOOL "Overriden by Pumex" )
  set( ASSIMP_BUILD_X_IMPORTER on CACHE BOOL "Overriden by Pumex" )
  set( ASSIMP_BUILD_GLTF_IMPORTER on CACHE BOOL "Overriden by Pumex" )
  set( ASSIMP_BUILD_ASSBIN_IMPORTER on CACHE BOOL "Overriden by Pumex" )
  set( ASSIMP_BUILD_ASSXML_IMPORTER on CACHE BOOL "Overriden by Pumex" )
  set( ASSIMP_BUILD_X3D_IMPORTER on CACHE BOOL "Overriden by Pumex" )
  set( ASSIMP_BUILD_3MF_IMPORTER on CACHE BOOL "Overriden by Pumex" )
  FetchContent_Declare(
    assimp
    GIT_REPOSITORY "https://github.com/assimp/assimp.git"
    GIT_TAG "v4.1.0"
  )
  FetchContent_GetProperties(assimp)
  if(NOT assimp_POPULATED)
    FetchContent_Populate(assimp)
    add_subdirectory( ${assimp_SOURCE_DIR} ${assimp_BINARY_DIR} )
    add_library( Assimp::assimp ALIAS assimp )
  endif()
  list( APPEND PUMEX_LIBRARIES_PUBLIC_DOWNLOADED Assimp::assimp )
  list( APPEND PUMEX_PUBLIC_INCLUDES ${assimp_SOURCE_DIR}/include ${assimp_BINARY_DIR}/include )
  list( APPEND PUMEX_EXTERNAL_TARGETS_INSTALL assimp IrrXML )
else()
  find_package( Assimp REQUIRED )
  list( APPEND PUMEX_PUBLIC_INCLUDES ${ASSIMP_INCLUDE_DIR} )
  list( APPEND PUMEX_LIBRARIES_PUBLIC ${ASSIMP_LIBRARIES} )
endif()

if( PUMEX_DOWNLOAD_EXTERNAL_FREETYPE )
  message( STATUS "Fetching content from Freetype library")
  set( FREETYPE_NO_DIST on CACHE BOOL "Overriden by Pumex" )
  FetchContent_Declare(
    freetype
    GIT_REPOSITORY "git://git.sv.nongnu.org/freetype/freetype2.git"
    GIT_TAG        "VER-2-8"
    PATCH_COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/external/freetype_fix/CMakeLists.txt" "${FETCHCONTENT_BASE_DIR}/freetype-src/CMakeLists.txt"
  )
  FetchContent_GetProperties(freetype)
  if(NOT freetype_POPULATED)
    FetchContent_Populate(freetype)
    add_subdirectory( ${freetype_SOURCE_DIR} ${freetype_BINARY_DIR} )
    add_library( freetype::freetype ALIAS freetype )
  endif()
  list( APPEND PUMEX_LIBRARIES_PUBLIC_DOWNLOADED freetype::freetype )
  list( APPEND PUMEX_PUBLIC_INCLUDES ${freetype_SOURCE_DIR}/include ${freetype_BINARY_DIR}/include )
  list( APPEND PUMEX_EXTERNAL_TARGETS_INSTALL freetype )
else()
  find_package( Freetype REQUIRED )
  list( APPEND PUMEX_PUBLIC_INCLUDES ${FREETYPE_INCLUDE_DIRS} )
  list( APPEND PUMEX_LIBRARIES_PUBLIC ${FREETYPE_LIBRARIES} )
endif()

if( PUMEX_DOWNLOAD_EXTERNAL_TBB )
  message( STATUS "Fetching content from TBB library")
  if(MSVC)
    enable_language(ASM_MASM)
  endif()
  set( TBB_BUILD_TESTS off CACHE BOOL "Overriden by Pumex" )
  FetchContent_Declare(
    tbb
    GIT_REPOSITORY "https://github.com/wjakob/tbb.git"
    PATCH_COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/external/tbb_fix/CMakeLists.txt" "${FETCHCONTENT_BASE_DIR}/tbb-src/CMakeLists.txt"
  )
  FetchContent_GetProperties(tbb)
  if(NOT tbb_POPULATED)
    FetchContent_Populate(tbb)
    add_subdirectory( ${tbb_SOURCE_DIR} ${tbb_BINARY_DIR} )
    add_library( tbb::tbb ALIAS tbb )
  endif()
  list( APPEND PUMEX_LIBRARIES_PUBLIC_DOWNLOADED tbb::tbb )
  list( APPEND PUMEX_PUBLIC_INCLUDES ${tbb_SOURCE_DIR}/include )
  list( APPEND PUMEX_EXTERNAL_TARGETS_INSTALL tbb )
else()
  find_package( TBB COMPONENTS tbbmalloc tbbmalloc_proxy )
  list( APPEND PUMEX_PUBLIC_INCLUDES ${TBB_INCLUDE_DIRS} )
  list( APPEND PUMEX_LIBRARIES_PUBLIC ${TBB_LIBRARIES} )
endif()

if(PUMEX_BUILD_QT)
  find_package( Qt5 REQUIRED COMPONENTS Core Gui )
  list( APPEND PUMEX_LIBRARIES_PUBLIC Qt5::Core Qt5::Gui )
endif()

# additional texture loaders consist of ZLIB, LIBPNG
if(PUMEX_BUILD_TEXTURE_LOADERS)
  if(PUMEX_DOWNLOAD_EXTERNAL_PNG)
    message( STATUS "Fetching content from LibPNG library")
    set( PNG_TESTS off CACHE BOOL "Overriden by Pumex" )
    set( SKIP_INSTALL_ALL on CACHE BOOL "Overriden by Pumex" )
    FetchContent_Declare(
      png
      GIT_REPOSITORY "git://git.code.sf.net/p/libpng/code"
      GIT_TAG "v1.6.36"
    )
    FetchContent_GetProperties(png)
    if(NOT png_POPULATED)
      FetchContent_Populate(png)
      add_subdirectory( ${png_SOURCE_DIR} ${png_BINARY_DIR} )
      add_library( png::png ALIAS png )
    endif()
    list( APPEND PUMEX_LIBRARIES_PUBLIC_DOWNLOADED png::png )
    list( APPEND PUMEX_PUBLIC_INCLUDES ${png_SOURCE_DIR} ${png_BINARY_DIR} )
    list( APPEND PUMEX_EXTERNAL_TARGETS_INSTALL png )
  else()
    find_package( PNG REQUIRED )
    list( APPEND PUMEX_LIBRARIES_PUBLIC PNG::PNG )
  endif()
  
  if(PUMEX_DOWNLOAD_EXTERNAL_JPEG)
    message( STATUS "Fetching content from JPEG library")
    FetchContent_Declare(
      jpeg
      GIT_REPOSITORY "https://github.com/LuaDist/libjpeg.git"
      PATCH_COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/external/libjpeg_fix/jmorecfg.h" "${FETCHCONTENT_BASE_DIR}/jpeg-src/jmorecfg.h"
    )
    FetchContent_GetProperties(jpeg)
    if(NOT jpeg_POPULATED)
      FetchContent_Populate(jpeg)
      add_subdirectory( ${jpeg_SOURCE_DIR} ${jpeg_BINARY_DIR} )
      add_library( jpeg::jpeg ALIAS jpeg )
    endif()
    list( APPEND PUMEX_LIBRARIES_PUBLIC_DOWNLOADED jpeg::jpeg )
    list( APPEND PUMEX_PUBLIC_INCLUDES ${jpeg_SOURCE_DIR} ${jpeg_BINARY_DIR} )
    list( APPEND PUMEX_EXTERNAL_TARGETS_INSTALL jpeg )
  else()
    find_package( JPEG REQUIRED )
    list( APPEND PUMEX_LIBRARIES_PUBLIC JPEG::JPEG )
  endif()
endif()

list( APPEND PUMEX_PUBLIC_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_BINARY_DIR}/include )

#install( TARGETS ${PUMEX_EXTERNAL_TARGETS_INSTALL}
#         EXPORT PumexTargets
#         PUBLIC_HEADER DESTINATION include COMPONENT headers
#         ARCHIVE DESTINATION lib COMPONENT libraries
#         LIBRARY DESTINATION lib COMPONENT libraries
#         RUNTIME DESTINATION bin COMPONENT libraries
#         )
