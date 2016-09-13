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
#include <sys/ioctl.h>
#include <va/va.h>
#include <va/va_backend.h>
#include "rockchip_driver.h"
#include "rockchip_decoder_v4l2.h"
#include "rockchip_debug.h"
#include "v4l2_utils.h"
#include "h264d.h"

typedef struct
{
	VAProfile va_profile;
	uint32_t format;
} RkV4L2FormatMap;

const static const RkV4L2FormatMap rk_v4l2_formats[] = {
	{VAProfileH264Baseline, V4L2_PIX_FMT_H264_SLICE},
	{VAProfileH264ConstrainedBaseline, V4L2_PIX_FMT_H264_SLICE},
	{VAProfileH264Main, V4L2_PIX_FMT_H264_SLICE},
	{VAProfileH264High, V4L2_PIX_FMT_H264_SLICE},
	{}
};

static uint32_t
get_v4l2_codec(VAProfile profile)
{
	const RkV4L2FormatMap *m;
	for (m = rk_v4l2_formats; m->va_profile; m++)
		if (m->va_profile == profile)
			return m->format;
	return 0;
}

static void
rk_dec_release(struct rk_dec_v4l2_context *ctx)
{
	int32_t index;
	struct rk_v4l2_buffer buffer;

	do {
		index = h264d_get_unrefed_picture(ctx->wrapper_pdrvctx);
		if (index >= 0) {
			buffer.index = index;
			ctx->v4l2_ctx->ops.qbuf_output(ctx->v4l2_ctx, &buffer);
		}
	}while(index >= 0);
}

static struct rk_v4l2_buffer *
rk_dec_procsss_avc_object
(struct rk_dec_v4l2_context *ctx,
 VAPictureParameterBufferH264 *pic_param, 
 VASliceParameterBufferH264 *slice_param,
 VASliceParameterBufferH264 *next_slice_param,
 uint8_t *slice_data)
{
	bool is_frame = false;
	uint32_t num_ctrls;
	uint32_t ctrl_ids[5];
	uint32_t payload_sizes[5];
	struct v4l2_ctrl_h264_sps *sps;
	struct v4l2_ctrl_h264_pps *pps;
	struct v4l2_ext_controls ext_ctrls;
	struct rk_v4l2_buffer *inbuf, *outbuf;
	uint8_t *ptr, *ptr2, *nal_ptr;
	uint8_t start_code_prefix[3] = {0x00, 0x00, 0x01};

	void *payloads[5];

	inbuf = rk_v4l2_get_input_buffer(ctx->v4l2_ctx);
	/* Not get validate buffer */
	if (NULL == inbuf)
		return NULL;
	/* FIXME overflow risk here */
	ptr = inbuf->plane[0].data + inbuf->plane[0].bytesused;
	nal_ptr = slice_data + slice_param->slice_data_offset;

	if (memcmp(nal_ptr, start_code_prefix, sizeof(start_code_prefix)) != 0) 
	{
		memcpy(ptr, &start_code_prefix, sizeof(start_code_prefix));
		ptr2 = ptr + sizeof(start_code_prefix);
		inbuf->plane[0].bytesused += sizeof(start_code_prefix);
	}
	else {
		ptr2 = ptr;
	}

	memcpy(ptr2, nal_ptr, slice_param->slice_data_size);
	inbuf->plane[0].bytesused += slice_param->slice_data_size;

	/* Process a nal a times
	 * If it return true, it could be a complete frame to be decode.
	 * But in my design it won't be trun unless the last buffer */
	is_frame = h264d_prepare_data_raw(ctx->wrapper_pdrvctx, 
		ptr, slice_param->slice_data_size + sizeof(start_code_prefix),
		&num_ctrls, ctrl_ids, payloads, payload_sizes);

	/* Not the last slice */
#if 0
	if (!is_frame) {
		if (NULL == next_slice_param)
			inbuf->plane[0].bytesused = 0;
		return -1;
	}
#else
	if (NULL != next_slice_param)
		NULL;
#endif

	sps = (struct v4l2_ctrl_h264_sps *)payloads[0];
	pps = (struct v4l2_ctrl_h264_pps *)payloads[1];

	pps->weighted_bipred_idc = 
		pic_param->pic_fields.bits.weighted_bipred_idc;
	pps->pic_init_qp_minus26 = pic_param->pic_init_qp_minus26;
	pps->chroma_qp_index_offset =
		pic_param->chroma_qp_index_offset;
	pps->second_chroma_qp_index_offset = 
		pic_param->second_chroma_qp_index_offset;

	pps->num_ref_idx_l0_default_active_minus1 = 
		slice_param->num_ref_idx_l0_active_minus1;
	pps->num_ref_idx_l1_default_active_minus1 = 
		slice_param->num_ref_idx_l1_active_minus1;

	sps->log2_max_frame_num_minus4 = 
		pic_param->seq_fields.bits.log2_max_frame_num_minus4;
	sps->log2_max_pic_order_cnt_lsb_minus4 =
		pic_param->seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4;
	sps->pic_order_cnt_type =
		pic_param->seq_fields.bits.pic_order_cnt_type;

	memset(&ext_ctrls, 0, sizeof(ext_ctrls));
	ext_ctrls.count = num_ctrls;
	ext_ctrls.controls = calloc(num_ctrls,
			sizeof(struct v4l2_ext_control));
	ext_ctrls.request = inbuf->index;

	for (uint8_t i = 0; i < num_ctrls; ++i) {
		ext_ctrls.controls[i].id = ctrl_ids[i];
		ext_ctrls.controls[i].ptr = payloads[i];
		ext_ctrls.controls[i].size = payload_sizes[i];
	}
	/* Set codec parameters need by VPU */
	ioctl(ctx->v4l2_ctx->video_fd, VIDIOC_S_EXT_CTRLS, &ext_ctrls);

	free(ext_ctrls.controls);
	/* Push codec data to driver */
	ctx->v4l2_ctx->ops.qbuf_input(ctx->v4l2_ctx, inbuf);

	/* Get decoded raw picture */
	if (0 == ctx->v4l2_ctx->ops.dqbuf_output(ctx->v4l2_ctx, &outbuf))
	{
		h264d_picture_ready(ctx->wrapper_pdrvctx, outbuf->index);
		/* Release the last time output buffer, the libvpu
		 * would determind which buffers are not the last
		 * buffer in capture then this function would release
		 * it and enqueue the CAPTURE */
		rk_dec_release(ctx);
	}
	/* release the input buffer */
	ctx->v4l2_ctx->ops.dqbuf_input(ctx->v4l2_ctx, &inbuf);

	return outbuf;
}

#define MAX_CAPTURE_BUFFERS   22

static VAStatus
rk_dec_v4l2_avc_decode_picture
(VADriverContextP ctx,  union codec_state *codec_state, 
 struct hw_context *hw_context)
{
	struct rockchip_driver_data *rk_data = 
		rockchip_driver_data(ctx);
	struct rk_dec_v4l2_context *rk_v4l2_data =
		(struct rk_dec_v4l2_context *)hw_context;
	struct rk_v4l2_object *video_ctx = rk_v4l2_data->v4l2_ctx;
	struct rk_v4l2_buffer *outbuf;
	struct decode_state *decode_state = &codec_state->decode;

	uint8_t *slice_data;
	VAPictureParameterBufferH264 *pic_param = NULL;
	VASliceParameterBufferH264 *slice_param, *next_slice_param, 
				   *next_slice_group_param;

	struct object_context *obj_context;
	struct object_surface *obj_surface;

	assert(rk_v4l2_data);

	obj_context = CONTEXT(rk_data->current_context_id);
	ASSERT(obj_context);

	obj_surface =
	SURFACE(obj_context->codec_state.decode.current_render_target);
	ASSERT(obj_surface);

	assert(decode_state->pic_param && decode_state->pic_param->buffer);
	pic_param = (VAPictureParameterBufferH264 *)decode_state->pic_param->buffer;

	if (pic_param->pic_fields.bits.reference_pic_flag)
		obj_surface->flags |= SURFACE_REFERENCED;
	else
		obj_surface->flags &= ~SURFACE_REFERENCED;

	if (!video_ctx->input_streamon)
		rk_v4l2_streamon_all(video_ctx);

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

		h264d_update_param(rk_v4l2_data->wrapper_pdrvctx, 
			rk_v4l2_data->profile, obj_surface->width,
			obj_surface->height, pic_param, slice_param);

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
			outbuf = rk_dec_procsss_avc_object(rk_v4l2_data, pic_param,
					slice_param, next_slice_param, 
					slice_data);
			/* Get validate frame */
			if (outbuf)
			{
				obj_surface->bo = outbuf;
				obj_surface->size =
					rk_v4l2_buffer_total_bytesused(outbuf);
			}

			/* Hardware job end here */
			slice_param++;
		}

	}

	return VA_STATUS_SUCCESS;
}

static VAStatus
rk_dec_v4l2_decode_picture
(VADriverContextP ctx, VAProfile profile, 
union codec_state *codec_state, struct hw_context *hw_context)
{
	switch(profile) {
	case VAProfileH264Baseline:
	case VAProfileH264Main:
	case VAProfileH264High:
		return rk_dec_v4l2_avc_decode_picture
			(ctx, codec_state, hw_context);
		break;
	default:
		/* Unsupport profile */
		ASSERT(0);
		return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
		break;
	};
}

VAStatus
decoder_rk_v4l2_init
(VADriverContextP ctx, struct object_context *obj_context,
 struct hw_context *hw_context)
{
	struct rockchip_driver_data *rk_data = 
		rockchip_driver_data(ctx);

	struct rk_dec_v4l2_context *rk_v4l2_data =
		(struct rk_dec_v4l2_context *)hw_context;
	struct rk_v4l2_object *video_ctx;
	struct object_config *obj_config;

	uint32_t v4l2_codec_type;
	int32_t ret = 0;

	if (NULL == rk_data || NULL == obj_context || NULL == rk_v4l2_data)
		return VA_STATUS_ERROR_INVALID_PARAMETER;

	obj_config = CONFIG(obj_context->config_id);
	ASSERT_RET(obj_config, VA_STATUS_ERROR_INVALID_CONFIG);
	
	rk_v4l2_data->profile = obj_config->profile;

	v4l2_codec_type = get_v4l2_codec(obj_config->profile);
	if (!v4l2_codec_type)
		return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;

	/* Create RK V4L2 Object */
	video_ctx = rk_v4l2_dec_create(NULL);
	if (NULL == video_ctx)
		return VA_STATUS_ERROR_ALLOCATION_FAILED;

	video_ctx->input_size.w = obj_context->picture_width;
	video_ctx->input_size.h = obj_context->picture_height;

	video_ctx->ops.set_codec(video_ctx, v4l2_codec_type);
	video_ctx->ops.set_format(video_ctx, 0);

	ret = video_ctx->ops.input_alloc(video_ctx, 1);
	ASSERT_RET(0 != ret, VA_STATUS_ERROR_ALLOCATION_FAILED);

	/* Keep Reference buffer */
	ret = video_ctx->ops.output_alloc
		(video_ctx, MAX_CAPTURE_BUFFERS);
	ASSERT_RET(0 != ret, VA_STATUS_ERROR_ALLOCATION_FAILED);

	/* There could be more common for stramon
	 * Also why not qbuf first but not streamon */
	for (uint8_t i = 0; i < ret; i++)
		video_ctx->ops.qbuf_output(video_ctx,
				&video_ctx->output_buffer[i]);

	rk_v4l2_data->wrapper_pdrvctx = h264d_init();
	if (NULL == rk_v4l2_data->wrapper_pdrvctx) {
		rk_error_msg("vpu backend request wrapper failed\n");
		rk_v4l2_destroy(video_ctx);

		return VA_STATUS_ERROR_OPERATION_FAILED;
	}

	rk_v4l2_data->v4l2_ctx = video_ctx;

	return VA_STATUS_SUCCESS;
}

static void
decoder_v4l2_destroy_context(void *hw_ctx)
{
	struct rk_dec_v4l2_context *rk_v4l2_ctx =
		(struct rk_dec_v4l2_context *)hw_ctx;

	if (NULL == rk_v4l2_ctx)
		return;

	h264d_deinit(rk_v4l2_ctx->wrapper_pdrvctx);

	rk_v4l2_destroy(rk_v4l2_ctx->v4l2_ctx);
	free(rk_v4l2_ctx->v4l2_ctx);
}

struct hw_context *
decoder_v4l2_create_context()
{
	struct rk_dec_v4l2_context *rk_v4l2_ctx =
		malloc(sizeof(struct rk_dec_v4l2_context));

	if (NULL == rk_v4l2_ctx)
		return NULL;

	memset(rk_v4l2_ctx, 0, sizeof(*rk_v4l2_ctx));

	rk_v4l2_ctx->base.run = rk_dec_v4l2_decode_picture;
	rk_v4l2_ctx->base.destroy = decoder_v4l2_destroy_context;
	rk_v4l2_ctx->base.get_status = NULL;
	rk_v4l2_ctx->base.sync = NULL;

	return (struct hw_context *) rk_v4l2_ctx;
}
