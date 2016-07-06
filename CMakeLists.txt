cmake_minimum_required(VERSION 2.6)
find_package(PkgConfig)

PROJECT(rockchip_drv_video C)

pkg_search_module(LIBVA libva)
string(REPLACE "." ";" LIBVA_VERSION_LIST ${LIBVA_VERSION})
list(GET LIBVA_VERSION_LIST 0 VA_MAJOR_VERSION)
list(GET LIBVA_VERSION_LIST 1 VA_MINOR_VERSION)

ADD_DEFINITIONS(-Os -Wall --std=gnu99 -g3 -Wmissing-declarations)

set(VA_DRIVER_INIT_FUNC "__vaDriverInit_${VA_MAJOR_VERSION}_${VA_MINOR_VERSION}")
CONFIGURE_FILE(config.h.in config.h)

ADD_LIBRARY(rockchip_drv_video SHARED rockchip_drv_video.c object_heap.c)
TARGET_LINK_LIBRARIES(rockchip_drv_video ${LIBVA_LIBRARIES})
TARGET_INCLUDE_DIRECTORIES(rockchip_drv_video PUBLIC ${LIBVA_INCLUDE_DIRS})
TARGET_COMPILE_OPTIONS(rockchip_drv_video PUBLIC ${LIBVA_CFLAGS})
SET_TARGET_PROPERTIES(rockchip_drv_video PROPERTIES PREFIX "")

INSTALL(TARGETS rockchip_drv_video LIBRARY DESTINATION lib/dri)
