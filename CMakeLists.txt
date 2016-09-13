cmake_minimum_required(VERSION 2.6)
find_package(PkgConfig)

PROJECT(rockchip_drv_video C)
INCLUDE(GNUInstallDirs)

pkg_search_module(PTHREAD pthread)
# check the libva
pkg_search_module(LIBVA libva)
if(LIBVA_FOUND)
string(REPLACE "." ";" LIBVA_VERSION_LIST ${LIBVA_VERSION})
list(GET LIBVA_VERSION_LIST 0 VA_MAJOR_VERSION)
list(GET LIBVA_VERSION_LIST 1 VA_MINOR_VERSION)
else(LIBVA_FOUND)
message (FATAL_ERROR "failed to find libva (VA-API Library)")
endif(LIBVA_FOUND)

ADD_DEFINITIONS(--std=gnu99 -Wmissing-declarations)
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wall -O0 -g3")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -Os")

# Begin generate the build configure file
set(CODEC_BACKEND "" CACHE STRING "Select the backend for both encoder/decoder")
set(HAVE_VA_X11 "" CACHE BOOL "Support X11 rendering")
set(HAVE_VA_EGL "" CACHE BOOL "Support EGL rendering")
set(HAVE_VA_DRM "" CACHE BOOL "Support DRM rendering")

if(HAVE_VA_X11)

set(HAVE_VA_EGL ON)
pkg_search_module(LIBX11 x11)
if(LIBX11_FOUND)
set(DISPLAY_BACKEND_LIBRARY ${DISPLAY_BACKEND_LIBRARY} ${LIBX11_LIBRARIES})
set(DISPLAY_BACKEND_INCLUDES ${DISPLAY_BACKEDN_INCLUDES} ${LIBX11_INCLUES})
set(DISPLAY_BACKEND_CFLAGS ${DISPLAY_BACKEND_CFLAGS} ${LIBX11_CFLAGS})
else(LIBX11_FOUND)
message (FATAL_ERROR "failed to find x11 (X11 Library)")
endif(LIBX11_FOUND)

endif(HAVE_VA_X11)

if(HAVE_VA_EGL)

pkg_search_module(LIBGLESv2 glesv2)
set(DISPLAY_BACKEND ${DISPLAY_BACKEND} rockchip_x11_gles.c)
if(LIBGLESv2_FOUND)
set(DISPLAY_BACKEND_LIBRARY ${DISPLAY_BACKEND_LIBRARY} ${LIBGLESv2_LIBRARIES})
set(DISPLAY_BACKEND_INCLUDES ${DISPLAY_BACKEDN_INCLUDES} ${LIBGLESv2_INCLUES})
set(DISPLAY_BACKEND_CFLAGS ${DISPLAY_BACKEND_CFLAGS} ${LIBGLESv2_CFLAGS})
else(LIBGLESv2_FOUND)
message (FATAL_ERROR "failed to find GLESv2 (OpenGL EGL v2 Library)")
endif(LIBGLESv2_FOUND)

endif(HAVE_VA_EGL)

if(${CODEC_BACKEND} MATCHES "libvpu")
set(BUILD_IN_BACKEND ${BUILD_IN_BACKEND} v4l2_utils.c v4l2_memory.c)
set(DECODER_BACKEND_LIBVPU 1)
set(ENCODER_BACKEND_LIBVPU 1)
set(BUILD_IN_BACKEND ${BUILD_IN_BACKEND} v4l2_utils.c rockchip_decoder_v4l2.c)

ADD_SUBDIRECTORY (librkdec)
set(BACKEND_SUPPORT_LIBRARY ${BACKEND_SUPPORT_LIBRARY} rkdec)
set(BACKEND_INCLUDE_DIR ${BACKEND_INCLUDE_DIR} librkdec/include/)
endif(${CODEC_BACKEND} MATCHES "libvpu")

set(VA_DRIVER_INIT_FUNC "__vaDriverInit_${VA_MAJOR_VERSION}_${VA_MINOR_VERSION}")
CONFIGURE_FILE("${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake"
	"${CMAKE_CURRENT_SOURCE_DIR}/config.h")
# End generate the build configure file

ADD_LIBRARY(rockchip_drv_video SHARED 
${BUILD_IN_BACKEND} 
object_heap.c 
rockchip_device_info.c rockchip_backend.c 
rockchip_debug.c rockchip_memory.c rockchip_image.c
rockchip_drv_video.c
${DISPLAY_BACKEND}
)

TARGET_LINK_LIBRARIES(rockchip_drv_video 
${PTHREAD_LIBRARIES} 
${LIBVA_LIBRARIES} 
${BACKEND_SUPPORT_LIBRARY}
${DISPLAY_BACKEND_LIBRARY}
)

TARGET_INCLUDE_DIRECTORIES(rockchip_drv_video PUBLIC 
${PTHREAD_INCLUDE_DIRS}
${LIBVA_INCLUDE_DIRS} 
${BACKEND_INCLUDE_DIR}
${DISPLAY_BACKEND_INCLUDES}
)

TARGET_COMPILE_OPTIONS(rockchip_drv_video PUBLIC 
${PTHREAD_CFLAGS}
${LIBVA_CFLAGS} 
${DISPLAY_BACKEND_CFLAGS}
)

SET_TARGET_PROPERTIES(rockchip_drv_video PROPERTIES PREFIX "")

INSTALL(TARGETS rockchip_drv_video LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/dri")
