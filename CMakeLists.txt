cmake_minimum_required(VERSION 2.6)
find_package(PkgConfig)

PROJECT(rockchip_drv_video C)
INCLUDE(GNUInstallDirs)

pkg_search_module(LIBVA libva)
if(LIBVA_FOUND)
string(REPLACE "." ";" LIBVA_VERSION_LIST ${LIBVA_VERSION})
list(GET LIBVA_VERSION_LIST 0 VA_MAJOR_VERSION)
list(GET LIBVA_VERSION_LIST 1 VA_MINOR_VERSION)
else(LIBVA_FOUND)
message (FATAL_ERROR "failed to find libva")
endif(LIBVA_FOUND)

ADD_DEFINITIONS(--std=gnu99 -Wmissing-declarations)
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wall -O0 -g3")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -Os")

if(${VA_MINOR_VERSION} GREATER 36) 
set(VAAPI_SUPPORT_HEVC 1)
endif(${VA_MINOR_VERSION} GREATER 36)
if(${VA_MINOR_VERSION} GREATER 38) 
set(VAAPI_SUPPORT_VP9 1)
endif(${VA_MINOR_VERSION} GREATER 38)

set(VA_DRIVER_INIT_FUNC "__vaDriverInit_${VA_MAJOR_VERSION}_${VA_MINOR_VERSION}")
CONFIGURE_FILE("${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake"
	"${CMAKE_CURRENT_SOURCE_DIR}/config.h")

ADD_LIBRARY(rockchip_drv_video SHARED 
object_heap.c 
rockchip_decoder_dummy.c rockchip_device_info.c rockchip_backend.c 
rockchip_memory.c rockchip_drv_video.c
)

TARGET_LINK_LIBRARIES(rockchip_drv_video ${LIBVA_LIBRARIES} m)
TARGET_INCLUDE_DIRECTORIES(rockchip_drv_video PUBLIC ${LIBVA_INCLUDE_DIRS})
TARGET_COMPILE_OPTIONS(rockchip_drv_video PUBLIC ${LIBVA_CFLAGS})
SET_TARGET_PROPERTIES(rockchip_drv_video PROPERTIES PREFIX "")

INSTALL(TARGETS rockchip_drv_video LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/dri")
