set( PUMEXLIB_EXTERNALS )
set( PUMEXLIB_PUBLIC_INCLUDES )
set( PUMEXLIB_PRIVATE_INCLUDES )
set( PUMEXLIB_LIBRARIES )
set( PUMEX_EXAMPLES_EXTERNALS )
set( PUMEX_EXAMPLES_INCLUDES )

set( INTERMEDIATE_INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/deps )
file ( MAKE_DIRECTORY ${INTERMEDIATE_INSTALL_DIR}/bin ${INTERMEDIATE_INSTALL_DIR}/lib ${INTERMEDIATE_INSTALL_DIR}/include )

list( APPEND PUMEXLIB_PUBLIC_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_BINARY_DIR}/include )

if( LINUX )
  find_package( X11 REQUIRED )
  find_package( XCB REQUIRED )
  find_package( Threads REQUIRED )
  list( APPEND PUMEXLIB_LIBRARIES ${XCB_LIBRARIES} ${X11_LIBRARIES} )
endif()

find_package( Vulkan REQUIRED )
list( APPEND PUMEXLIB_LIBRARIES Vulkan::Vulkan )

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  SET(EXPERIMENTAL_FILESYSTEM_LIBRARIES stdc++fs)
elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
  SET(EXPERIMENTAL_FILESYSTEM_LIBRARIES c++experimental)
else()
  SET(EXPERIMENTAL_FILESYSTEM_LIBRARIES )
endif()
list( APPEND PUMEXLIB_LIBRARIES ${EXPERIMENTAL_FILESYSTEM_LIBRARIES} )

IF(ANDROID)
  SET( CROSS_COMPILE_PARAMETERS -DANDROID_PLATFORM=${ANDROID_PLATFORM} -DANDROID_ABI=${ANDROID_ABI} -DANDROID_STL=${ANDROID_STL} -DANDROID_NDK=${ANDROID_NDK} -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM} -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE} )
else()  
  SET( CROSS_COMPILE_PARAMETERS )
endif()

include( ExternalProject )
#include( FindPackageHandleStandardArgs )

if( PUMEX_DOWNLOAD_EXTERNAL_GLM )
  set( GLM_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/glm )
  set( GLM_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/external/glm )
  ExternalProject_Add( glm-external
    PREFIX "${GLM_BUILD_DIR}"
    BINARY_DIR "${GLM_BUILD_DIR}/build"
    STAMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/stamp/glm"
    GIT_REPOSITORY "https://github.com/g-truc/glm.git"
    GIT_TAG "0.9.9.3"
    SOURCE_DIR "${GLM_SOURCE_DIR}"
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    UPDATE_COMMAND ""
    PATCH_COMMAND ""
    TEST_COMMAND ""
    INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_directory "${GLM_SOURCE_DIR}/glm" "${INTERMEDIATE_INSTALL_DIR}/include/glm"
    UPDATE_DISCONNECTED TRUE
  )
  ExternalProject_Add_Step( glm-external glm-copy-intermediate
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${INTERMEDIATE_INSTALL_DIR}" "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDEES install
  )
  list( APPEND PUMEXLIB_EXTERNALS glm-external )
else()
  find_package( glm REQUIRED )
  list( APPEND PUMEXLIB_PUBLIC_INCLUDES ${GLM_INCLUDE_DIRS} )
endif()

if( PUMEX_DOWNLOAD_EXTERNAL_GLI )
  set( GLI_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/gli )
  set( GLI_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/external/gli )
  ExternalProject_Add( gli-external
    PREFIX "${GLI_BUILD_DIR}"
    BINARY_DIR "${GLI_BUILD_DIR}/build"
    STAMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/stamp/gli"
    GIT_REPOSITORY "https://github.com/g-truc/gli.git"
    SOURCE_DIR "${GLI_SOURCE_DIR}"
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    UPDATE_COMMAND ""
    PATCH_COMMAND ""
    TEST_COMMAND ""
    INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_directory "${GLI_SOURCE_DIR}/gli" "${INTERMEDIATE_INSTALL_DIR}/include/gli"
    UPDATE_DISCONNECTED TRUE
  )
  ExternalProject_Add_Step( gli-external gli-copy-intermediate
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${INTERMEDIATE_INSTALL_DIR}" "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDEES install
  )
  list( APPEND PUMEXLIB_EXTERNALS gli-external )
else()
  find_package( gli REQUIRED )
  list( APPEND PUMEXLIB_PUBLIC_INCLUDES ${GLI_INCLUDE_DIRS} )
endif()

if( PUMEX_BUILD_EXAMPLES )
  if( PUMEX_DOWNLOAD_EXTERNAL_ARGS )
    set( ARGS_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/args )
    set( ARGS_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/external/args" )
    ExternalProject_Add( args-external
      PREFIX "${ARGS_BUILD_DIR}"
      BINARY_DIR "${ARGS_BUILD_DIR}/build"
      STAMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/stamp/args"
      GIT_REPOSITORY "https://github.com/Taywee/args.git"
      GIT_TAG "6.1.0"
      SOURCE_DIR "${ARGS_SOURCE_DIR}"
      CONFIGURE_COMMAND ""
      BUILD_COMMAND ""
      UPDATE_COMMAND ""
      PATCH_COMMAND ""
      TEST_COMMAND ""
      INSTALL_COMMAND ${CMAKE_COMMAND} -E copy "${ARGS_SOURCE_DIR}/args.hxx" "${INTERMEDIATE_INSTALL_DIR}/include/args.hxx"
      UPDATE_DISCONNECTED TRUE
    )
    ExternalProject_Add_Step( args-external args-copy-intermediate
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${INTERMEDIATE_INSTALL_DIR}" "${CMAKE_CURRENT_BINARY_DIR}"
      DEPENDEES install
    )
    list( APPEND PUMEX_EXAMPLES_EXTERNALS args-external )
  else()
    find_package( args REQUIRED )
    list( APPEND PUMEX_EXAMPLES_INCLUDES ${ARGS_INCLUDE_DIRS} )
  endif()
endif()

if( PUMEX_DOWNLOAD_EXTERNAL_ASSIMP )
  set( ASSIMP_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/assimp )
  set( ASSIMP_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/external/assimp )
  ExternalProject_Add( assimp-external
    PREFIX "${ASSIMP_BUILD_DIR}"
    BINARY_DIR "${ASSIMP_BUILD_DIR}/build"
    STAMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/stamp/assimp"
    GIT_REPOSITORY "https://github.com/assimp/assimp.git"
    GIT_TAG "v4.1.0"
    SOURCE_DIR "${ASSIMP_SOURCE_DIR}"
    UPDATE_COMMAND ""
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${INTERMEDIATE_INSTALL_DIR} -DASSIMP_BUILD_ASSIMP_TOOLS=off -DASSIMP_BUILD_TESTS=off -DCMAKE_DEBUG_POSTFIX=d "${CROSS_COMPILE_PARAMETERS}"
    UPDATE_DISCONNECTED TRUE
  )
  
  ExternalProject_Add_Step( assimp-external assimp-copy-intermediate
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${INTERMEDIATE_INSTALL_DIR}" "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDEES install
  )

  if( MSVC12 )
    set( ASSIMP_MSVC_VERSION "vc120" )
  else()
    set( ASSIMP_MSVC_VERSION "vc140" )
  endif()
  if( MSVC12 OR MSVC14 )
    set( ASSIMP_LIBRARY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/lib/assimp-${ASSIMP_MSVC_VERSION}-mt.lib )
    set( ASSIMP_LIBRARY_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/lib/assimp-${ASSIMP_MSVC_VERSION}-mtd.lib )
    set( ASSIMP_LIBRARIES optimized "${ASSIMP_LIBRARY_RELEASE}" debug "${ASSIMP_LIBRARY_DEBUG}" )
  endif()
  list( APPEND PUMEXLIB_LIBRARIES ${ASSIMP_LIBRARIES} )
  list( APPEND PUMEXLIB_EXTERNALS assimp-external )
else()
  find_package( ASSIMP REQUIRED )
  list( APPEND PUMEXLIB_PRIVATE_INCLUDES ${ASSIMP_INCLUDE_DIR} )
  list( APPEND PUMEXLIB_LIBRARIES ${ASSIMP_LIBRARIES} )
endif()

if( PUMEX_DOWNLOAD_EXTERNAL_FREETYPE )
  set( FREETYPE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/freetype )
  set( FREETYPE_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/external/freetype )
  ExternalProject_Add( freetype-external
    PREFIX "${FREETYPE_BUILD_DIR}"
    BINARY_DIR "${FREETYPE_BUILD_DIR}/build"
    STAMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/stamp/freetype"
    GIT_REPOSITORY "git://git.sv.nongnu.org/freetype/freetype2.git"
    GIT_TAG "VER-2-8"
    SOURCE_DIR "${FREETYPE_SOURCE_DIR}"
    UPDATE_COMMAND ""
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${INTERMEDIATE_INSTALL_DIR} -DCMAKE_DEBUG_POSTFIX=d -DBUILD_SHARED_LIBS:BOOL=true "${CROSS_COMPILE_PARAMETERS}"
    UPDATE_DISCONNECTED TRUE
  )
  ExternalProject_Add_Step( freetype-external freetype-copy-intermediate
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${INTERMEDIATE_INSTALL_DIR}" "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDEES install
  )

  if(WIN32)
    set( FREETYPE_LIBRARY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/lib/freetype.lib )
    set( FREETYPE_LIBRARY_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/lib/freetyped.lib )
  else()
    set( FREETYPE_LIBRARY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/lib/libfreetype.so )
    set( FREETYPE_LIBRARY_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/lib/libfreetyped.so )
  endif()
  set( FREETYPE_LIBRARIES optimized "${FREETYPE_LIBRARY_RELEASE}" debug "${FREETYPE_LIBRARY_DEBUG}" )
  list( APPEND PUMEXLIB_PUBLIC_INCLUDES ${FREETYPE_SOURCE_DIR}/include )
  list( APPEND PUMEXLIB_LIBRARIES ${FREETYPE_LIBRARIES} )
  list( APPEND PUMEXLIB_EXTERNALS freetype-external )
else()
  find_package( Freetype REQUIRED )
  list( APPEND PUMEXLIB_PUBLIC_INCLUDES ${FREETYPE_INCLUDE_DIRS} )
  list( APPEND PUMEXLIB_LIBRARIES ${FREETYPE_LIBRARIES} )
endif()

if( PUMEX_DOWNLOAD_EXTERNAL_TBB )
  set( TBB_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/tbb )
  set( TBB_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/external/tbb )
  ExternalProject_Add( tbb-external
    PREFIX "${TBB_BUILD_DIR}"
    BINARY_DIR "${TBB_BUILD_DIR}/build"
    STAMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/stamp/tbb"
    GIT_REPOSITORY "https://github.com/wjakob/tbb.git"
    SOURCE_DIR "${TBB_SOURCE_DIR}"
    UPDATE_COMMAND ""
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${INTERMEDIATE_INSTALL_DIR} -DCMAKE_DEBUG_POSTFIX=_debug -DTBB_BUILD_TESTS=OFF "${CROSS_COMPILE_PARAMETERS}"
    UPDATE_DISCONNECTED TRUE
  )
  ExternalProject_Add_Step( tbb-external tbb-copy-intermediate
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${INTERMEDIATE_INSTALL_DIR}" "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDEES install
  )
  if( WIN32 )
    set( TBB_LIBRARY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/lib/tbb.lib )
    set( TBB_LIBRARY_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/lib/tbb_debug.lib )
  else()
    set( TBB_LIBRARY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/lib/libtbb.so )
    set( TBB_LIBRARY_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/lib/libtbb_debug.so )
  endif()
  set( TBB_LIBRARIES optimized "${TBB_LIBRARY_RELEASE}" debug "${TBB_LIBRARY_DEBUG}" )
  list( APPEND PUMEXLIB_LIBRARIES ${TBB_LIBRARIES} )
  list( APPEND PUMEXLIB_EXTERNALS tbb-external )
else()
  find_package( TBB COMPONENTS tbbmalloc tbbmalloc_proxy )
  list( APPEND PUMEXLIB_PRIVATE_INCLUDES  ${TBB_INCLUDE_DIRS} )
  list( APPEND PUMEXLIB_LIBRARIES ${TBB_LIBRARIES} )
endif()

if(PUMEX_BUILD_QT)
  find_package( Qt5 REQUIRED COMPONENTS Core Gui )
  list( APPEND PUMEXLIB_LIBRARIES Qt5::Core Qt5::Gui )
endif()

# additional texture loaders consist of ZLIB, LIBPNG
if(PUMEX_BUILD_TEXTURE_LOADERS)
  if(PUMEX_DOWNLOAD_EXTERNAL_ZLIB)
    set( ZLIB_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/zlib )
    set( ZLIB_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/external/zlib )
    ExternalProject_Add( zlib-external
      PREFIX "${ZLIB_BUILD_DIR}"
      BINARY_DIR "${ZLIB_BUILD_DIR}/build"
      STAMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/stamp/zlib"
      GIT_REPOSITORY "https://github.com/madler/zlib.git"
      GIT_TAG "v1.2.11"
      SOURCE_DIR "${ZLIB_SOURCE_DIR}"
      UPDATE_COMMAND ""
      CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${INTERMEDIATE_INSTALL_DIR} -DCMAKE_DEBUG_POSTFIX=d "${CROSS_COMPILE_PARAMETERS}"
      UPDATE_DISCONNECTED TRUE
    )
    ExternalProject_Add_Step( zlib-external zlib-copy-intermediate
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${INTERMEDIATE_INSTALL_DIR}" "${CMAKE_CURRENT_BINARY_DIR}"
      DEPENDEES install
    )
    if(WIN32)
      set( ZLIB_LIBRARY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/lib/zlib.lib )
      set( ZLIB_LIBRARY_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/lib/zlibd.lib )
    else()
      set( ZLIB_LIBRARY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/lib/zlib.so )
      set( ZLIB_LIBRARY_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/lib/zlibd.so )
    endif()
    set( ZLIB_LIBRARIES optimized "${ZLIB_LIBRARY_RELEASE}" debug "${ZLIB_LIBRARY_DEBUG}" )
    list( APPEND PUMEXLIB_PUBLIC_INCLUDES ${ZLIB_SOURCE_DIR}/include )
    list( APPEND PUMEXLIB_LIBRARIES ${ZLIB_LIBRARIES} )
    list( APPEND PUMEXLIB_EXTERNALS zlib-external )
  else()
    find_package( ZLIB REQUIRED )
    list( APPEND PUMEXLIB_PUBLIC_INCLUDES ${ZLIB_INCLUDE_DIRS} )
    list( APPEND PUMEXLIB_LIBRARIES ${ZLIB_LIBRARIES} )
  endif()
  
  if(PUMEX_DOWNLOAD_EXTERNAL_PNG)
    set( PNG_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/libpng )
    set( PNG_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/external/libpng )
    ExternalProject_Add( libpng-external
      PREFIX "${PNG_BUILD_DIR}"
      BINARY_DIR "${PNG_BUILD_DIR}/build"
      STAMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/stamp/libpng"
      GIT_REPOSITORY "git://git.code.sf.net/p/libpng/code"
      GIT_TAG "v1.6.36"
      SOURCE_DIR "${PNG_SOURCE_DIR}"
      UPDATE_COMMAND ""
      CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${INTERMEDIATE_INSTALL_DIR} -DCMAKE_DEBUG_POSTFIX=d "${CROSS_COMPILE_PARAMETERS}"
      UPDATE_DISCONNECTED TRUE
    )
    ExternalProject_Add_Step( libpng-external libpng-copy-intermediate
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${INTERMEDIATE_INSTALL_DIR}" "${CMAKE_CURRENT_BINARY_DIR}"
      DEPENDEES install
    )
    if(WIN32)
      set( PNG_LIBRARY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/lib/libpng16.lib )
      set( PNG_LIBRARY_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/lib/libpng16d.lib )
    else()
      set( PNG_LIBRARY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/lib/libpng16.so )
      set( PNG_LIBRARY_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/lib/libpng16d.so )
    endif()
    set( PNG_LIBRARIES optimized "${PNG_LIBRARY_RELEASE}" debug "${PNG_LIBRARY_DEBUG}" )
    list( APPEND PUMEXLIB_PUBLIC_INCLUDES ${PNG_BUILD_DIR}/include )
    list( APPEND PUMEXLIB_LIBRARIES ${PNG_LIBRARIES} )
    list( APPEND PUMEXLIB_EXTERNALS libpng-external )
  else()
    find_package( PNG REQUIRED )
    list( APPEND PUMEXLIB_PUBLIC_INCLUDES ${PNG_INCLUDE_DIRS} )
    list( APPEND PUMEXLIB_LIBRARIES ${PNG_LIBRARIES} )
  endif()
  
  if(PUMEX_DOWNLOAD_EXTERNAL_JPEG)
    set( JPEG_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/libjpeg )
    set( JPEG_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/external/libjpeg )
    ExternalProject_Add( libjpeg-external
      PREFIX "${JPEG_BUILD_DIR}"
      BINARY_DIR "${JPEG_BUILD_DIR}/build"
      STAMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/stamp/libjpeg"
      GIT_REPOSITORY "https://github.com/LuaDist/libjpeg.git"
      PATCH_COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/external/libjpeg_fix/jmorecfg.h" "${JPEG_SOURCE_DIR}/jmorecfg.h"
      SOURCE_DIR "${JPEG_SOURCE_DIR}"
      UPDATE_COMMAND ""
      CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${INTERMEDIATE_INSTALL_DIR} -DCMAKE_DEBUG_POSTFIX=d "${CROSS_COMPILE_PARAMETERS}"
      UPDATE_DISCONNECTED TRUE
    )
    ExternalProject_Add_Step( libjpeg-external libjpeg-copy-intermediate
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${INTERMEDIATE_INSTALL_DIR}" "${CMAKE_CURRENT_BINARY_DIR}"
      DEPENDEES install
    )
    if(WIN32)
      set( JPEG_LIBRARY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/lib/jpeg.lib )
      set( JPEG_LIBRARY_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/lib/jpegd.lib )
    else()
      set( JPEG_LIBRARY_RELEASE ${CMAKE_CURRENT_BINARY_DIR}/lib/libjpeg.so )
      set( JPEG_LIBRARY_DEBUG ${CMAKE_CURRENT_BINARY_DIR}/lib/libjpegd.so )
    endif()
    set( JPEG_LIBRARIES optimized "${JPEG_LIBRARY_RELEASE}" debug "${JPEG_LIBRARY_DEBUG}" )
    list( APPEND PUMEXLIB_PUBLIC_INCLUDES ${JPEG_BUILD_DIR}/include )
    list( APPEND PUMEXLIB_LIBRARIES ${JPEG_LIBRARIES} )
    list( APPEND PUMEXLIB_EXTERNALS libjpeg-external )
  else()
    find_package( JPEG REQUIRED )
    list( APPEND PUMEXLIB_PUBLIC_INCLUDES ${JPEG_INCLUDE_DIRS} )
    list( APPEND PUMEXLIB_LIBRARIES ${JPEG_LIBRARIES} )
  endif()
  
endif()