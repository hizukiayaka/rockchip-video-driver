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

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <va/va.h>
#include <va/va_backend.h>
#include "rockchip_driver.h"
#include "rockchip_decoder_dummy.h"

static VAStatus
generate_image_i420(uint8_t *image_data, const VARectangle *rect, 
uint32_t square_index, uint32_t square_size)
{
	uint8_t *dst[3];
	/* Always YVU 4:2:0 */
	const int Y = 0;
	const int V = 1;
	const int U = 2;
	int x, y;
	double rx, ry;

	VAStatus va_status = VA_STATUS_SUCCESS;

	/* Dest VA image has either I420 or YV12 format. */
	dst[Y] = image_data;
	dst[U] = image_data + (rect->width / 2) * (rect->height / 2);
	dst[V] = image_data + 2 * ((rect->width / 2) * (rect->height / 2));

	memset(dst[Y], 0, rect->width * rect->height);
	memset(dst[U], 128, (rect->width / 2) * (rect->height / 2));
	memset(dst[V], 128, (rect->width / 2) * (rect->height / 2));

	rx = cos(7 * square_index / 3.14 / 25 * 100 / rect->width);
	ry = sin(6 * square_index / 3.14 / 25 * 100 / rect->width);

	x = (rx + 1) / 2 * (rect->width - 2 * square_size) + square_size;
	y = (ry + 1) / 2 * (rect->height - 2 * square_size) + square_size;

	for(int i = MIN(square_size, rect->width) - 1; i >= 0; i--)
		for(int j = MIN(square_size, rect->height) - 1; j >= 0; --j)
			dst[Y][x + i + (y + j) * rect->width] = 255;

	return va_status;
}

static VAStatus
rk_decoder_dummy_decode_picture
(VADriverContextP ctx, VAProfile profile, 
union codec_state *codec_state, struct hw_context *hw_context)
{
	struct rk_decoder_dummy_context *rk_dummy_ctx =
		(struct rk_decoder_dummy_context *)hw_context;
	struct decode_state *decode_state = &codec_state->decode;

	VAStatus vaStatus;
	uint8_t *slice_data;
	VAPictureParameterBufferH264 *pic_param;
	VASliceParameterBufferH264 *slice_param, *next_slice_param, 
				   *next_slice_group_param;


	assert(rk_dummy_ctx);

	for (int32_t i = 0; i < decode_state->num_slice_params; i++)
	{
		assert(decode_state->slice_params 
				&& decode_state->slice_params[i]->buffer);
		slice_param = (VASliceParameterBufferH264 *)
			decode_state->slice_params[i]->buffer;
		slice_data = decode_state->slice_datas[i]->buffer;

		if (i == decode_state->num_slice_params - 1)
			next_slice_group_param = NULL;
		else
			next_slice_group_param = (VASliceParameterBufferH264 *)
				decode_state->slice_params[i + 1]->buffer;
		if (i == 0 && slice_param->first_mb_in_slice)
		{
			/* 
			 * First slice
			 * Doing nothing here
			 */
		}

		/* Process the number of slices that the param order */
		for (int32_t j = 0; 
			j < decode_state->slice_params[i]->num_elements; j++)
		{
			/* 
			 * check whether the buffer is big enough to hold the 
			 * whole slice, we only support process a the completely
			 * slice data
			 */
			assert(slice_param->slice_data_flag == 
					VA_SLICE_DATA_FLAG_ALL);
			/* Only support those frame type for H.264 */
			assert((slice_param->slice_type == SLICE_TYPE_I) ||
				(slice_param->slice_type == SLICE_TYPE_SI) ||
				(slice_param->slice_type == SLICE_TYPE_P) ||
				(slice_param->slice_type == SLICE_TYPE_SP) ||
				(slice_param->slice_type == SLICE_TYPE_B));

			if (j < decode_state->slice_params[i]->num_elements - 1)
				next_slice_param = slice_param + 1;
			else
				next_slice_param = next_slice_group_param;
			/* Hardware job begin here */


			/* Hardware job end here */
			slice_param++;
		}

	}

	return VA_STATUS_SUCCESS;
}

struct hw_context *
decoder_dummy_create_context()
{
	struct rk_decoder_dummy_context *dummy_ctx =
		malloc(sizeof(struct rk_decoder_dummy_context));

	if (NULL == dummy_ctx)
		return NULL;

	dummy_ctx->base.run = rk_decoder_dummy_decode_picture;

	return (struct hw_context *) dummy_ctx;
}
