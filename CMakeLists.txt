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

if(${CODEC_BACKEND} MATCHES "libvpu")
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
)

TARGET_LINK_LIBRARIES(rockchip_drv_video 
${PTHREAD_LIBRARIES} 
${LIBVA_LIBRARIES} 
${BACKEND_SUPPORT_LIBRARY}
)

TARGET_INCLUDE_DIRECTORIES(rockchip_drv_video PUBLIC 
${PTHREAD_INCLUDE_DIRS}
${LIBVA_INCLUDE_DIRS} 
${BACKEND_INCLUDE_DIR}
)

TARGET_COMPILE_OPTIONS(rockchip_drv_video PUBLIC 
${PTHREAD_CFLAGS}
${LIBVA_CFLAGS} 
)

SET_TARGET_PROPERTIES(rockchip_drv_video PROPERTIES PREFIX "")

INSTALL(TARGETS rockchip_drv_video LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/dri")
