cmake_minimum_required(VERSION 2.6)
find_package(PkgConfig)

INCLUDE(GNUInstallDirs)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")

ADD_LIBRARY (rkdec STATIC
h264d.c h264_stream.c
h264_dec/h264decapi.c h264_dec/h264hwd_nal_unit.c
h264_dec/h264hwd_slice_header.c h264_dec/h264hwd_vui.c
h264_dec/h264hwd_asic.c h264_dec/h264hwd_pic_order_cnt.c
h264_dec/h264hwd_storage.c h264_dec/pv_avcdec_api.c
h264_dec/h264hwd_byte_stream.c h264_dec/h264hwd_pic_param_set.c
h264_dec/h264hwd_stream.c h264_dec/refbuffer.c
h264_dec/h264hwd_decoder.c h264_dec/h264hwd_seq_param_set.c
h264_dec/h264hwd_util.c h264_dec/regdrv.c
h264_dec/h264hwd_dpb.c h264_dec/h264hwd_slice_group_map.c
h264_dec/h264hwd_vlc.c h264_dec/vpu_mem.c
)

TARGET_INCLUDE_DIRECTORIES(rkdec PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
"${CMAKE_CURRENT_SOURCE_DIR}/include"
"${CMAKE_CURRENT_SOURCE_DIR}/h264_dec"
)
