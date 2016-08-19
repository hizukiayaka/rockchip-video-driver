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

#include <stdint.h>
#include <stdlib.h>
#include "rockchip_backend.h"
#ifdef DECODER_BACKEND_DUMMY
#include "rockchip_decoder_dummy.h"
#endif
#ifdef DECODER_BACKEND_MPP
#include "rockchip_decoder_mpp.h"
#endif

struct hw_context *rk3288_dec_hw_context_init
    (VADriverContextP ctx, struct object_config *obj_config) 
{

	struct hw_context *hw_ctx = NULL;
#ifdef DECODER_BACKEND_DUMMY
	hw_ctx = decoder_dummy_create_context();
#endif

#ifdef DECODER_BACKEND_MPP
	hw_ctx = decoder_mpp_create_context();
	if (!rk_mpp_init(hw_ctx, obj_config))
	{
		free(hw_ctx);
		hw_ctx = NULL;
	}
#endif
#ifdef DECODER_BACKEND_LIBVPU
	hw_ctx = decoder_v4l2_create_context();
	if (!decoder_rk_v4l2_init(hw_ctx, obj_config))
	{
		free(hw_ctx);
		hw_ctx = NULL;
	}

#endif

	return hw_ctx;

}

struct hw_context *rk3288_enc_hw_context_init
    (VADriverContextP ctx, struct object_config *obj_config) {
	struct hw_context *hw_ctx = NULL;

	return hw_ctx;
}

/* Render Buffer */
#define ROCKCHIP_RENDER_BUFFER(category, name) rockchip_render_##category##_##name##_buffer(ctx, obj_context, obj_buffer)

#define DEF_RENDER_SINGLE_BUFFER_FUNC(category, name, member)           \
    static VAStatus                                                     \
    rockchip_render_##category##_##name##_buffer(VADriverContextP ctx,      \
                                             struct object_context *obj_context, \
                                             struct object_buffer *obj_buffer) \
    {                                                                   \
        struct category##_state *category = &obj_context->codec_state.category; \
        rockchip_release_buffer_store(&category->member);                   \
        rockchip_reference_buffer_store(&category->member, obj_buffer->buffer_store); \
        return VA_STATUS_SUCCESS;                                       \
    }

#define DEF_RENDER_MULTI_BUFFER_FUNC(category, name, member)            \
    static VAStatus                                                     \
    rockchip_render_##category##_##name##_buffer(VADriverContextP ctx,      \
                                             struct object_context *obj_context, \
                                             struct object_buffer *obj_buffer) \
    {                                                                   \
        struct category##_state *category = &obj_context->codec_state.category; \
        if (category->num_##member == category->max_##member) {         \
            category->member = realloc(category->member, (category->max_##member + NUM_SLICES) * sizeof(*category->member)); \
            memset(category->member + category->max_##member, 0, NUM_SLICES * sizeof(*category->member)); \
            category->max_##member += NUM_SLICES;                       \
        }                                                               \
        rockchip_release_buffer_store(&category->member[category->num_##member]); \
        rockchip_reference_buffer_store(&category->member[category->num_##member], obj_buffer->buffer_store); \
        category->num_##member++;                                       \
        return VA_STATUS_SUCCESS;                                       \
    }

/* Decoder Render */
#define ROCKCHIP_RENDER_DECODE_BUFFER(name) ROCKCHIP_RENDER_BUFFER(decode, name)

#define DEF_RENDER_DECODE_SINGLE_BUFFER_FUNC(name, member) DEF_RENDER_SINGLE_BUFFER_FUNC(decode, name, member)
DEF_RENDER_DECODE_SINGLE_BUFFER_FUNC(picture_parameter, pic_param)
DEF_RENDER_DECODE_SINGLE_BUFFER_FUNC(iq_matrix, iq_matrix)
DEF_RENDER_DECODE_SINGLE_BUFFER_FUNC(bit_plane, bit_plane)
DEF_RENDER_DECODE_SINGLE_BUFFER_FUNC(huffman_table, huffman_table)
DEF_RENDER_DECODE_SINGLE_BUFFER_FUNC(probability_data, probability_data)

#define DEF_RENDER_DECODE_MULTI_BUFFER_FUNC(name, member) DEF_RENDER_MULTI_BUFFER_FUNC(decode, name, member)
DEF_RENDER_DECODE_MULTI_BUFFER_FUNC(slice_parameter, slice_params)
DEF_RENDER_DECODE_MULTI_BUFFER_FUNC(slice_data, slice_datas)

VAStatus
rockchip_decoder_render_picture(VADriverContextP ctx, VAContextID context,
VABufferID * buffers, int num_buffers)
{
	struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);
	struct object_context *obj_context = CONTEXT(context);

	VAStatus vaStatus = VA_STATUS_SUCCESS;
	int i;

	/* verify that we got valid buffer references */
	for (i = 0; i < num_buffers; i++) {
		object_buffer_p obj_buffer = BUFFER(buffers[i]);
		ASSERT(obj_buffer);
		if (NULL == obj_buffer) {
			return VA_STATUS_ERROR_INVALID_BUFFER;
		}
		switch (obj_buffer->type) {
		case VAPictureParameterBufferType:
			vaStatus =
			    ROCKCHIP_RENDER_DECODE_BUFFER(picture_parameter);
			break;
		case VAIQMatrixBufferType:
			vaStatus =
			    ROCKCHIP_RENDER_DECODE_BUFFER(iq_matrix);
			break;
		case VABitPlaneBufferType:
			vaStatus =
			    ROCKCHIP_RENDER_DECODE_BUFFER(bit_plane);
			break;
		case VASliceParameterBufferType:
			vaStatus =
			    ROCKCHIP_RENDER_DECODE_BUFFER(slice_parameter);
			break;
		case VASliceDataBufferType:
			vaStatus =
			    ROCKCHIP_RENDER_DECODE_BUFFER(slice_data);
			break;
		default:
			vaStatus = VA_STATUS_ERROR_UNSUPPORTED_BUFFERTYPE;
			break;
		}
	}

	return vaStatus;
}
