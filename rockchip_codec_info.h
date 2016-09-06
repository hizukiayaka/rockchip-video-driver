/*
 * Copyright © 2016 Rockchip Co., Ltd.
 * Randy Li, <randy.li@rock-chips.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef _ROCKCHIP_CODEC_INFO_H
#define _ROCKCHIP_CODEC_INFO_H
#include "rockchip_buffer.h"

#define ENTROPY_CAVLD           0
#define ENTROPY_CABAC           1

/* Used for the identification of VASliceParameterBufferH264->slice_type */
#define SLICE_TYPE_P            0
#define SLICE_TYPE_B            1
#define SLICE_TYPE_I            2
#define SLICE_TYPE_SP           3
#define SLICE_TYPE_SI           4

#define CODEC_DEC       0
#define CODEC_ENC       1
#define CODEC_PROC      2

#define NUM_SLICES     10

struct codec_state_base {
	uint32_t chroma_formats;
};

struct decode_state {
	struct codec_state_base base;
	struct buffer_store *pic_param;
	struct buffer_store **slice_params;
	struct buffer_store *iq_matrix;
	struct buffer_store *bit_plane;
	struct buffer_store *huffman_table;
	struct buffer_store **slice_datas;
	struct buffer_store *probability_data;

	int max_slice_params;
	int num_slice_params;

	int max_slice_datas;
	int num_slice_datas;

	VASurfaceID current_render_target;
	struct object_surface *render_object;
	struct object_surface *reference_objects[16];	/* Up to 2 reference surfaces are valid for MPEG-2, */

	struct buffer_store *image_data;
};

struct encode_state {
	struct codec_state_base base;
	struct buffer_store *seq_param;
	struct buffer_store *pic_param;
	struct buffer_store *pic_control;
	struct buffer_store *iq_matrix;
	struct buffer_store *q_matrix;
	struct buffer_store *huffman_table;

	struct buffer_store **slice_params;
	int max_slice_params;
	int num_slice_params;

	/* for ext */
	struct buffer_store *seq_param_ext;
	struct buffer_store *pic_param_ext;
	struct buffer_store *packed_header_param[5];
	struct buffer_store *packed_header_data[5];

	struct buffer_store **slice_params_ext;
	int max_slice_params_ext;
	int num_slice_params_ext;

	struct buffer_store *encmb_map;

	/* Check the user-configurable packed_header attribute.
	 * Currently it is mainly used to check whether the packed slice_header data
	 * is provided by user or the driver.
	 * TBD: It will check for the packed SPS/PPS/MISC/RAWDATA and so on.
	 */
	unsigned int packed_header_flag;
	/* For the packed data that needs to be inserted into video clip */
	/* currently it is mainly to track packed raw data and packed slice_header data. */
	struct buffer_store **packed_header_params_ext;
	int max_packed_header_params_ext;
	int num_packed_header_params_ext;
	struct buffer_store **packed_header_data_ext;
	int max_packed_header_data_ext;
	int num_packed_header_data_ext;

	/* the index of current vps and sps ,special for HEVC*/
	int vps_sps_seq_index;
	/* the index of current slice */
	int slice_index;
	/* the array is determined by max_slice_params_ext */
	int max_slice_num;
	/* This is to store the first index of packed data for one slice */
	int *slice_rawdata_index;
	/* This is to store the number of packed data for one slice.
	 * Both packed rawdata and slice_header data are tracked by this
	 * this variable. That is to say: When one packed slice_header is parsed,
	 * this variable will also be increased.
	 */
	int *slice_rawdata_count;

	/* This is to store the index of packed slice header for one slice */
	int *slice_header_index;

	int last_packed_header_type;

	struct buffer_store *misc_param[16];

	VASurfaceID current_render_target;
	struct object_surface *input_yuv_object;
	struct object_surface *reconstructed_object;
	struct object_buffer *coded_buf_object;
	struct object_surface *reference_objects[16]; /* Up to 2 reference surfaces are valid for MPEG-2,*/
};

union codec_state {
	struct codec_state_base base;
	struct decode_state decode;
	struct encode_state encode;
	struct codec_state_base proc;
};

#endif
