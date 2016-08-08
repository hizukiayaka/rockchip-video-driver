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

union codec_state {
	struct codec_state_base base;
	struct decode_state decode;
	struct codec_state_base encode;
	struct codec_state_base proc;
};

#endif
