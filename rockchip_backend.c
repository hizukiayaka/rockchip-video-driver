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
#include "rockchip_debug.h"
#include "rockchip_decoder_v4l2.h"
#include "rockchip_encoder_v4l2.h"

struct hw_context *rk3288_dec_hw_context_init
    (VADriverContextP ctx, struct object_context *obj_context) 
{

	struct hw_context *hw_ctx = NULL;

	hw_ctx = decoder_v4l2_create_context();
	if (VA_STATUS_SUCCESS != 
			decoder_rk_v4l2_init(ctx, obj_context, hw_ctx))
	{
		free(hw_ctx);
		hw_ctx = NULL;
	}

	return hw_ctx;
}

struct hw_context *rk3288_enc_hw_context_init
    (VADriverContextP ctx, struct object_context *obj_context) 
{
	struct hw_context *hw_ctx = NULL;

	hw_ctx = encoder_v4l2_create_context();
	if (VA_STATUS_SUCCESS != 
			encoder_rk_v4l2_init(ctx, obj_context, hw_ctx))
	{
		free(hw_ctx);
		hw_ctx = NULL;
	}

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

/* Encoder Render */
#define ROCKCHIP_RENDER_ENCODE_BUFFER(name) ROCKCHIP_RENDER_BUFFER(encode, name)

#define DEF_RENDER_ENCODE_SINGLE_BUFFER_FUNC(name, member) \
	DEF_RENDER_SINGLE_BUFFER_FUNC(encode, name, member)
DEF_RENDER_ENCODE_SINGLE_BUFFER_FUNC(qmatrix, q_matrix)
DEF_RENDER_ENCODE_SINGLE_BUFFER_FUNC(iqmatrix, iq_matrix)
DEF_RENDER_ENCODE_SINGLE_BUFFER_FUNC(huffman_table, huffman_table)
/* extended buffer */
DEF_RENDER_ENCODE_SINGLE_BUFFER_FUNC(sequence_parameter, seq_param_ext)
DEF_RENDER_ENCODE_SINGLE_BUFFER_FUNC(picture_parameter, pic_param_ext)
DEF_RENDER_ENCODE_SINGLE_BUFFER_FUNC(encmb_map, encmb_map)

#define DEF_RENDER_ENCODE_MULTI_BUFFER_FUNC(name, member) \
	DEF_RENDER_MULTI_BUFFER_FUNC(encode, name, member)
DEF_RENDER_ENCODE_MULTI_BUFFER_FUNC(slice_parameter_ext, slice_params_ext)
DEF_RENDER_ENCODE_MULTI_BUFFER_FUNC
	(packed_header_params_ext, packed_header_params_ext)
DEF_RENDER_ENCODE_MULTI_BUFFER_FUNC
(packed_header_data_ext, packed_header_data_ext)

#define SLICE_PACKED_DATA_INDEX_TYPE    0x80000000

#define ROCKCHIP_PACKED_HEADER_BASE         0
#define ROCKCHIP_SEQ_PACKED_HEADER_BASE     0
#define ROCKCHIP_SEQ_PACKED_HEADER_END      2
#define ROCKCHIP_PIC_PACKED_HEADER_BASE     2
#define ROCKCHIP_PACKED_MISC_HEADER_BASE    4

static int
va_enc_packed_type_to_idx(int32_t packed_type)
{
    int32_t idx = 0;

    if (packed_type & VAEncPackedHeaderMiscMask) {
        idx = ROCKCHIP_PACKED_MISC_HEADER_BASE;
        packed_type = (~VAEncPackedHeaderMiscMask & packed_type);
        ASSERT_RET(packed_type > 0, 0);
        idx += (packed_type - 1);
    } else {
        idx = ROCKCHIP_PACKED_HEADER_BASE;

        switch (packed_type) {
        case VAEncPackedHeaderSequence:
            idx = ROCKCHIP_SEQ_PACKED_HEADER_BASE + 0;
            break;

        case VAEncPackedHeaderPicture:
            idx = ROCKCHIP_PIC_PACKED_HEADER_BASE + 0;
            break;

        case VAEncPackedHeaderSlice:
            idx = ROCKCHIP_PIC_PACKED_HEADER_BASE + 1;
            break;

        default:
            /* Should not get here */
            ASSERT_RET(0, 0);
            break;
        }
    }

    ASSERT_RET(idx < 5, 0);
    return idx;
}


static VAStatus
rockchip_encoder_render_packed_header_parameter_buffer(VADriverContextP ctx, 
		struct object_context *obj_context, 
		struct object_buffer *obj_buffer,
		int32_t type_index)
{
    struct encode_state *encode = &obj_context->codec_state.encode;

    ASSERT_RET(obj_buffer->buffer_store->buffer, VA_STATUS_ERROR_INVALID_BUFFER);
    rockchip_release_buffer_store(&encode->packed_header_param[type_index]);
    rockchip_reference_buffer_store(&encode->packed_header_param[type_index],
		    obj_buffer->buffer_store);

    return VA_STATUS_SUCCESS;
}

static VAStatus
rockchip_encoder_render_packed_header_data_buffer(VADriverContextP ctx,
		struct object_context *obj_context,
		struct object_buffer *obj_buffer,
		int type_index)
{
    struct encode_state *encode = &obj_context->codec_state.encode;

    ASSERT_RET(obj_buffer->buffer_store->buffer, VA_STATUS_ERROR_INVALID_BUFFER);
    rockchip_release_buffer_store(&encode->packed_header_data[type_index]);
    rockchip_reference_buffer_store(&encode->packed_header_data[type_index],
		    obj_buffer->buffer_store);

    return VA_STATUS_SUCCESS;
}

static VAStatus
rockchip_encoder_render_misc_parameter_buffer(VADriverContextP ctx,
                                          struct object_context *obj_context,
                                          struct object_buffer *obj_buffer)
{
    struct encode_state *encode = &obj_context->codec_state.encode;
    VAEncMiscParameterBuffer *param = NULL;

    ASSERT_RET(obj_buffer->buffer_store->buffer, VA_STATUS_ERROR_INVALID_BUFFER);

    param = (VAEncMiscParameterBuffer *)obj_buffer->buffer_store->buffer;

    if (param->type >= ARRAY_ELEMS(encode->misc_param))
        return VA_STATUS_ERROR_INVALID_PARAMETER;

    rockchip_release_buffer_store(&encode->misc_param[param->type]);
    rockchip_reference_buffer_store(&encode->misc_param[param->type],
		    obj_buffer->buffer_store);

    return VA_STATUS_SUCCESS;
}

static VAStatus
rockchip_encoder_render_slice_parameter_buffer
(VADriverContextP ctx, struct object_context *obj_context, 
 struct object_buffer *obj_buffer)
{
	struct encode_state *encode = &obj_context->codec_state.encode;

	VAStatus vaStatus;

	vaStatus = ROCKCHIP_RENDER_ENCODE_BUFFER(slice_parameter_ext);
	if (vaStatus != VA_STATUS_SUCCESS)
		return vaStatus;

	/* When the max number of slices is updated, it also needs
	 * to reallocate the arrays that is used to store
	 * the packed data index/count for the slice
	 */
	if (!(encode->packed_header_flag & VA_ENC_PACKED_HEADER_SLICE)) 
	{
	   encode->slice_index++;
	}
	if (encode->slice_index == encode->max_slice_num) 
	{
		int32_t slice_num = encode->max_slice_num;
		encode->slice_rawdata_index 
			= realloc(encode->slice_rawdata_index,
				  (slice_num + NUM_SLICES) * sizeof(int));
		encode->slice_rawdata_count 
			= realloc(encode->slice_rawdata_count,
				  (slice_num + NUM_SLICES) * sizeof(int));
		encode->slice_header_index 
			= realloc(encode->slice_header_index,
					  (slice_num + NUM_SLICES) * sizeof(int));
		memset(encode->slice_rawdata_index + slice_num, 0,
		    sizeof(int) * NUM_SLICES);
		memset(encode->slice_rawdata_count + slice_num, 0,
		    sizeof(int) * NUM_SLICES);
		memset(encode->slice_header_index + slice_num, 0,
		    sizeof(int) * NUM_SLICES);

		encode->max_slice_num += NUM_SLICES;
		if ((encode->slice_rawdata_index == NULL) ||
		    (encode->slice_header_index == NULL)  ||
		    (encode->slice_rawdata_count == NULL)) {
		    vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
		}
	}

	return vaStatus;
}

static VAStatus
rockchip_encoder_render_packed_header_buffer
(VADriverContextP ctx, struct object_context *obj_context,
 struct object_buffer *obj_buffer)
{
	struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);
	struct encode_state *encode = &obj_context->codec_state.encode;
	struct object_config *obj_config = CONFIG(obj_context->config_id);

	VAStatus vaStatus;

	if (encode->last_packed_header_type == 0) {
	    WARN_ONCE("the packed header data is passed without type!\n");
	    vaStatus = VA_STATUS_ERROR_INVALID_PARAMETER;
	    return vaStatus;
	}

	if (encode->last_packed_header_type == VAEncPackedHeaderRawData ||
	    encode->last_packed_header_type == VAEncPackedHeaderSlice) 
	{
		vaStatus 
			= ROCKCHIP_RENDER_ENCODE_BUFFER(packed_header_data_ext);

		/* When the PACKED_SLICE_HEADER flag is passed, it will use
		 * the packed_slice_header as the delimeter to decide how
		 * the packed rawdata is inserted for the given slice.
		 * Otherwise it will use the VAEncSequenceParameterBuffer
		 * as the delimeter
		 */
		if (encode->packed_header_flag & VA_ENC_PACKED_HEADER_SLICE) 
		{
			/* store the first index of the packed header data for current slice */
			if (encode->slice_rawdata_index[encode->slice_index] == 0) 
			{
			    encode->slice_rawdata_index[encode->slice_index] =
				SLICE_PACKED_DATA_INDEX_TYPE 
				| (encode->num_packed_header_data_ext - 1);
			}

			encode->slice_rawdata_count[encode->slice_index]++;

			if (encode->last_packed_header_type == VAEncPackedHeaderSlice) 
			{
				/* find one packed slice_header delimeter. And the following
				 * packed data is for the next slice
				 */
				encode->slice_header_index[encode->slice_index] =
				    SLICE_PACKED_DATA_INDEX_TYPE 
				    | (encode->num_packed_header_data_ext - 1);
				encode->slice_index++;
				/* Reallocate the buffer to record the index/count of
				 * packed_data for one slice.
				 */
				if (encode->slice_index == encode->max_slice_num) {
				    int slice_num = encode->max_slice_num;

				    encode->slice_rawdata_index 
					    = realloc(encode->slice_rawdata_index,
						 (slice_num + NUM_SLICES) * sizeof(int));
				    encode->slice_rawdata_count 
					    = realloc(encode->slice_rawdata_count,
						  (slice_num + NUM_SLICES) * sizeof(int));
				    encode->slice_header_index 
					    = realloc(encode->slice_header_index,
						  (slice_num + NUM_SLICES) * sizeof(int));

				    memset(encode->slice_rawdata_index + slice_num, 0,
				       sizeof(int) * NUM_SLICES);
				    memset(encode->slice_rawdata_count + slice_num, 0,
				       sizeof(int) * NUM_SLICES);
				    memset(encode->slice_header_index + slice_num, 0,
				       sizeof(int) * NUM_SLICES);

				    encode->max_slice_num += NUM_SLICES;
				}
			}
		} else {
			if (vaStatus == VA_STATUS_SUCCESS) {
			    /* store the first index of the packed header data for current slice */
			    if (encode->slice_rawdata_index[encode->slice_index] == 0) {
				encode->slice_rawdata_index[encode->slice_index] =
				SLICE_PACKED_DATA_INDEX_TYPE | (encode->num_packed_header_data_ext - 1);
			    }
			    encode->slice_rawdata_count[encode->slice_index]++;
			    if (encode->last_packed_header_type == VAEncPackedHeaderSlice) {
				if (encode->slice_header_index[encode->slice_index] == 0) {
				encode->slice_header_index[encode->slice_index] =
				    SLICE_PACKED_DATA_INDEX_TYPE | (encode->num_packed_header_data_ext - 1);
				} else {
				WARN_ONCE("Multi slice header data is passed for"
				      " slice %d!\n", encode->slice_index);
				}
			    }
			}
		}
	} else {
		ASSERT_RET(encode->last_packed_header_type == VAEncPackedHeaderSequence ||
		    encode->last_packed_header_type == VAEncPackedHeaderPicture ||
		    encode->last_packed_header_type == VAEncPackedHeaderSlice ||
		   (((encode->last_packed_header_type & VAEncPackedHeaderMiscMask) 
		     == VAEncPackedHeaderMiscMask) 
		    && ((encode->last_packed_header_type 
				    & (~VAEncPackedHeaderMiscMask)) != 0)),
		    VA_STATUS_ERROR_ENCODING_ERROR);

		if((obj_config->profile == VAProfileHEVCMain) &&
		    (encode->last_packed_header_type 
		     == VAEncPackedHeaderSequence)) 
		{
			vaStatus = rockchip_encoder_render_packed_header_data_buffer
				(ctx, obj_context, obj_buffer, 
				 va_enc_packed_type_to_idx
				 (encode->last_packed_header_type) 
				 + encode->vps_sps_seq_index);
			encode->vps_sps_seq_index = (encode->vps_sps_seq_index + 1) 
				% ROCKCHIP_SEQ_PACKED_HEADER_END;
		}else{
		    vaStatus = rockchip_encoder_render_packed_header_data_buffer
			    (ctx, obj_context, obj_buffer, va_enc_packed_type_to_idx
			     (encode->last_packed_header_type));

		}
	}
	encode->last_packed_header_type = 0;

	return vaStatus;
}

VAStatus
rockchip_encoder_render_picture(VADriverContextP ctx, VAContextID context,
VABufferID * buffers, int num_buffers)
{
	struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);
	struct object_context *obj_context = CONTEXT(context);
	struct object_config *obj_config;
	struct encode_state *encode;
	VAStatus vaStatus = VA_STATUS_ERROR_UNKNOWN;

	ASSERT_RET(obj_context, VA_STATUS_ERROR_INVALID_CONTEXT);
	obj_config = CONFIG(obj_context->config_id);
	ASSERT_RET(obj_config, VA_STATUS_ERROR_INVALID_CONFIG);

	encode = &obj_context->codec_state.encode;
	/* verify that we got valid buffer references */
	for (uint32_t i = 0; i < num_buffers; i++) {
		struct object_buffer *obj_buffer = BUFFER(buffers[i]);

		if (NULL == obj_buffer)
			return VA_STATUS_ERROR_INVALID_BUFFER;

		switch (obj_buffer->type) {
		case VAQMatrixBufferType:
			vaStatus =
				ROCKCHIP_RENDER_ENCODE_BUFFER(qmatrix);
			break;
		case VAIQMatrixBufferType:
			vaStatus =
				ROCKCHIP_RENDER_ENCODE_BUFFER(iqmatrix);
			break;
		case VAEncSequenceParameterBufferType:
			vaStatus = ROCKCHIP_RENDER_ENCODE_BUFFER
				(sequence_parameter);
			break;
		case VAEncPictureParameterBufferType:
			vaStatus = ROCKCHIP_RENDER_ENCODE_BUFFER
				(picture_parameter);
			break;
		case VAHuffmanTableBufferType:
			vaStatus = ROCKCHIP_RENDER_ENCODE_BUFFER(huffman_table);
			break;
		case VAEncSliceParameterBufferType:
			vaStatus = rockchip_encoder_render_slice_parameter_buffer
				(ctx, obj_context, obj_buffer);

			if (VA_STATUS_ERROR_ALLOCATION_FAILED == vaStatus)
				return vaStatus;
			break;
		case VAEncPackedHeaderParameterBufferType:
		{
			VAEncPackedHeaderParameterBuffer *param 
				= (VAEncPackedHeaderParameterBuffer *)
				obj_buffer->buffer_store->buffer;
			encode->last_packed_header_type = param->type;

			if ((param->type == VAEncPackedHeaderRawData) ||
			    (param->type == VAEncPackedHeaderSlice)) {
				vaStatus = ROCKCHIP_RENDER_ENCODE_BUFFER
					(packed_header_params_ext);
			} else if((obj_config->profile == VAProfileHEVCMain) &&
			    (encode->last_packed_header_type == VAEncPackedHeaderSequence)) 
			{
			    vaStatus = rockchip_encoder_render_packed_header_parameter_buffer
				    (ctx, obj_context, obj_buffer, 
				     va_enc_packed_type_to_idx
				     	(encode->last_packed_header_type) 
				     + encode->vps_sps_seq_index);
			} else {
			    vaStatus = rockchip_encoder_render_packed_header_parameter_buffer
				    (ctx, obj_context, obj_buffer, 
				     va_enc_packed_type_to_idx
				     (encode->last_packed_header_type));
			}
			break;
		}

		case VAEncPackedHeaderDataBufferType:
			vaStatus = rockchip_encoder_render_packed_header_buffer
				(ctx, obj_context, obj_buffer);
			break;
		case VAEncMiscParameterBufferType:
			vaStatus = rockchip_encoder_render_misc_parameter_buffer
				(ctx, obj_context, obj_buffer);
			break;
		case VAEncMacroblockMapBufferType:
			vaStatus = ROCKCHIP_RENDER_ENCODE_BUFFER(encmb_map);
			break;

		default:
			vaStatus = VA_STATUS_ERROR_UNSUPPORTED_BUFFERTYPE;
			break;
		}
	}

	return vaStatus;
}

VAStatus
rk_v4l2_assign_surface_bo(VADriverContextP ctx,
	struct object_surface *obj_surface)
{
	struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);
	struct object_context *obj_context = NULL;
	struct rk_v4l2_buffer *buffer;

	if (NULL == obj_surface || NULL == ctx)
		return VA_STATUS_ERROR_INVALID_PARAMETER;

	obj_context = CONTEXT(rk_data->current_context_id);

	if (NULL == obj_context)
		return VA_STATUS_ERROR_INVALID_CONTEXT;

	switch (obj_context->codec_type) {
	case CODEC_ENC:
	{
		struct rk_enc_v4l2_context *video_ctx =
			(struct rk_enc_v4l2_context*)obj_context->hw_context;

		buffer = rk_v4l2_get_input_buffer(video_ctx->v4l2_ctx);
		if (NULL == buffer)
			return VA_STATUS_ERROR_OPERATION_FAILED;

		obj_surface->bo = buffer;
		for (uint32_t i = 0; i < buffer->length; i++) {
			obj_surface->size += buffer->plane[i].length;
		}
		return VA_STATUS_SUCCESS;
	}
	break;
	case CODEC_DEC:
	{
	/*
	 * If the surface is not assigned a v4l2 buffer object in
	 * Rockchip_EndPicture(), it means no result for it.
	 */
#if 0
		struct rk_dec_v4l2_context *video_ctx =
			(struct rk_dec_v4l2_context*)obj_context->hw_context;

		buffer = rk_v4l2_get_output_buffer(video_ctx->v4l2_ctx);
		if (NULL == buffer)
			return VA_STATUS_ERROR_OPERATION_FAILED;

		obj_surface->bo = buffer;
		/* Zero can't pass the detect of gstreamer */
		for (uint32_t i = 0; i < buffer->length; i++) {
			obj_surface->size += buffer->plane[i].length;
		}
#endif
	}
	break;
	default:
		return VA_STATUS_ERROR_UNIMPLEMENTED;
		break;
	}
}


