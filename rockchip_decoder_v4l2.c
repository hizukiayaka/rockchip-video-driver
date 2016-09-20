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
#include "h264d.h"
#include "rockchip_decoder_v4l2.h"
#include "rockchip_debug.h"
#include "rockchip_driver.h"
#include "v4l2_utils.h"

#define H264_PROFILE_BASELINE  66
#define H264_PROFILE_MAIN      77
#define H264_PROFILE_EXTENDED  88
#define H264_PROFILE_HIGH     100

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

static void
rk_h264_set_sps
(struct v4l2_ctrl_h264_sps *sps, VAPictureParameterBufferH264 *pic_param,
 VAProfile profile)
{
	switch (profile) {
	case VAProfileH264Baseline:
		sps->profile_idc = H264_PROFILE_BASELINE;
		sps->constraint_set_flags = 1;
		break;
	case VAProfileH264Main:
		sps->profile_idc = H264_PROFILE_MAIN;
		sps->constraint_set_flags = 0;
		break;
	case VAProfileH264High:
		sps->profile_idc = VAProfileH264High;
		sps->constraint_set_flags = 0;
		break;
	default:
		rk_info_msg("rk_h264_set_sps: unknown profile %d", profile);
		return;

		break;
	}

	/* Why constant variable ? */
	sps->level_idc = 40;
	sps->seq_parameter_set_id = 0;

	sps->chroma_format_idc =
		pic_param->seq_fields.bits.chroma_format_idc;
	sps->bit_depth_luma_minus8 =
		pic_param->bit_depth_luma_minus8;
	sps->bit_depth_chroma_minus8 =
		pic_param->bit_depth_chroma_minus8;
	sps->log2_max_frame_num_minus4 =
		pic_param->seq_fields.bits.log2_max_frame_num_minus4;
	sps->pic_order_cnt_type =
		pic_param->seq_fields.bits.pic_order_cnt_type;
	sps->log2_max_pic_order_cnt_lsb_minus4 =
		pic_param->seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4;
	/* Again, why constant variable ? */
	sps->offset_for_non_ref_pic = 0;
	sps->offset_for_top_to_bottom_field = 0;
	sps->num_ref_frames_in_pic_order_cnt_cycle = 0;

	sps->max_num_ref_frames = pic_param->num_ref_frames;
	sps->pic_width_in_mbs_minus1 = pic_param->picture_width_in_mbs_minus1;
	sps->pic_height_in_map_units_minus1 =
		pic_param->picture_height_in_mbs_minus1;

	/* Not be used by kernel driver
	sps->offset_for_ref_frame
	*/

	/* XXX It seems that we could omit this part */
	sps->flags |= pic_param->seq_fields.bits.delta_pic_order_always_zero_flag << 2;
	sps->flags |= pic_param->seq_fields.bits.gaps_in_frame_num_value_allowed_flag << 3;
	sps->flags |= pic_param->seq_fields.bits.frame_mbs_only_flag << 4;
	sps->flags |= pic_param->seq_fields.bits.mb_adaptive_frame_field_flag << 5;
	sps->flags |= pic_param->seq_fields.bits.direct_8x8_inference_flag << 6;
}

static void
rk_h264_set_pps
(struct v4l2_ctrl_h264_pps *pps, VAPictureParameterBufferH264 *pic_param,
 VASliceParameterBufferH264 *slice_param)
{

	/* Why constant variable ? */
	pps->pic_parameter_set_id = 0;
	pps->seq_parameter_set_id = 0;

	pps->num_slice_groups_minus1 =
		pic_param->num_slice_groups_minus1;
	pps->num_ref_idx_l0_default_active_minus1 =
		slice_param->num_ref_idx_l0_active_minus1;
	pps->num_ref_idx_l1_default_active_minus1 =
		slice_param->num_ref_idx_l1_active_minus1;
	pps->weighted_bipred_idc =
		pic_param->pic_fields.bits.weighted_bipred_idc;
	pps->pic_init_qp_minus26 = pic_param->pic_init_qp_minus26;
	pps->pic_init_qs_minus26 = pic_param->pic_init_qs_minus26;
	pps->chroma_qp_index_offset =
		pic_param->chroma_qp_index_offset;
	pps->second_chroma_qp_index_offset =
		pic_param->second_chroma_qp_index_offset;

	pps->flags |= pic_param->pic_fields.bits.entropy_coding_mode_flag << 0;
	pps->flags |= pic_param->pic_fields.bits.pic_order_present_flag << 1;
	pps->flags |= pic_param->pic_fields.bits.weighted_pred_flag << 2;
	pps->flags |= pic_param->pic_fields.bits.deblocking_filter_control_present_flag << 3;
	pps->flags |= pic_param->pic_fields.bits.constrained_intra_pred_flag << 4;
	pps->flags |= pic_param->pic_fields.bits.redundant_pic_cnt_present_flag << 5;
	pps->flags |= pic_param->pic_fields.bits.transform_8x8_mode_flag << 6;
	/* FIXME scalingMatrixPresentFlag
	 *pps->flags |= pic_param->pic_fields.bits. << 7;
	 */
}

static void
rk_h264_pre_slice_param
(struct v4l2_ctrl_h264_slice_param *dst_param,
 struct v4l2_ctrl_h264_slice_param *src_param)
{

	dst_param->idr_pic_id = src_param->idr_pic_id;
	dst_param->dec_ref_pic_marking_bit_size
		= src_param->dec_ref_pic_marking_bit_size;
	dst_param->pic_order_cnt_bit_size
		= src_param->pic_order_cnt_bit_size;
}

static void
rk_h264_set_slice_param
(struct v4l2_ctrl_h264_slice_param *param,
 VASliceParameterBufferH264 *slice_param,
 VAPictureParameterBufferH264 *pic_param)
{
	/* Not used by kernel driver
	param->size =
	parma->header_bit_size =
	param->pic_order_cnt_lsb =
	param->delta_pic_order_cnt_bottom =
	param->delta_pic_order_cnt0 =
	param->delta_pic_order_cnt1 =
	param->redundant_pic_cnt =
	param->slice_group_change_cycle
	param->ref_pic_list0
	*/

	param->first_mb_in_slice = slice_param->first_mb_in_slice;
	param->slice_type = slice_param->slice_type;

	param->pic_parameter_set_id = 0;
	param->colour_plane_id = 0;

	param->frame_num = pic_param->frame_num;
	/* FIXME fill me
	param->idr_pic_id =
	*/
#if 0
	param->pred_weight_table.luma_log2_weight_denom =
		slice_param->luma_log2_weight_denom;
	param->pred_weight_table.chroma_log2_weight_denom =
		slice_param->chroma_log2_weight_denom;

	memcpy(param->pred_weight_table.weight_factors[0].luma_weight,
			slice_param->luma_weight_l0,
			sizeof(slice_param->luma_weight_l0));
	memcpy(param->pred_weight_table.weight_factors[0].luma_offset,
			slice_param->luma_offset_l0,
			sizeof(slice_param->luma_offset_l0));

	memcpy(param->pred_weight_table.weight_factors[1].chroma_weight,
			slice_param->chroma_weight_l0,
			sizeof(slice_param->chroma_weight_l0));
	memcpy(param->pred_weight_table.weight_factors[1].chroma_offset,
			slice_param->chroma_offset_l0,
			sizeof(slice_param->chroma_offset_l0));
#endif
	/* FIXME fill me
	param->dec_ref_pic_marking_bit_size
	param->pic_order_cnt_bit_size
	*/
	param->cabac_init_idc =
		slice_param->cabac_init_idc;
	param->slice_qp_delta =
		slice_param->slice_qp_delta;
	param->slice_qs_delta = 0;
	param->disable_deblocking_filter_idc =
		slice_param->disable_deblocking_filter_idc;
	param->slice_alpha_c0_offset_div2 =
		slice_param->slice_alpha_c0_offset_div2;
	param->slice_beta_offset_div2 =
		slice_param->slice_beta_offset_div2;

	param->num_ref_idx_l0_active_minus1 =
		slice_param->num_ref_idx_l0_active_minus1;
	param->num_ref_idx_l1_active_minus1 =
		slice_param->num_ref_idx_l1_active_minus1;
}

static void
rk_h264_decode_set_freq_dep_quat
(struct v4l2_ctrl_h264_scaling_matrix *scaling_matrix,
 VAIQMatrixBufferH264 *iq_matrix)
{
	if (NULL == iq_matrix)
	{
		memset(scaling_matrix->scaling_list_4x4, 0,
				6 * 16 * sizeof(int8_t));
		memset(scaling_matrix->scaling_list_8x8, 0,
			2 * 64 * sizeof(int8_t));

		return;
	}
	memcpy(scaling_matrix->scaling_list_4x4,
			iq_matrix->ScalingList4x4,
			6 * 16 * sizeof(int8_t));
	memcpy(scaling_matrix->scaling_list_8x8,
			iq_matrix->ScalingList8x8,
			2 * 64 * sizeof(int8_t));
}
static void
rk_h264_pre_decode_param(struct v4l2_ctrl_h264_decode_param *dst_param,
		struct v4l2_ctrl_h264_decode_param *src_param)
{
	dst_param->idr_pic_flag = src_param->idr_pic_flag;

	memcpy(dst_param->ref_pic_list_p0, src_param->ref_pic_list_p0,
			32 * sizeof(uint8_t));
	memcpy(dst_param->ref_pic_list_b0, src_param->ref_pic_list_b0,
			32 * sizeof(uint8_t));
	memcpy(dst_param->ref_pic_list_b1, src_param->ref_pic_list_b1,
			32 * sizeof(uint8_t));

	for (uint8_t i = 0; i < 16; i++) {
		const struct v4l2_h264_dpb_entry *src_dpb =
			&src_param->dpb[i];

		struct v4l2_h264_dpb_entry *dst_dpb =
			&dst_param->dpb[i];

		dst_dpb->buf_index = src_dpb->buf_index;
		dst_dpb->frame_num = src_dpb->frame_num;
		dst_dpb->pic_num = src_dpb->pic_num;
		dst_dpb->flags = src_dpb->flags;
	}
}

static void
rk_h264_decode_param(struct v4l2_ctrl_h264_decode_param *param,
VAPictureParameterBufferH264 *pic_param)
{
	/* Not used by kernel
	 * num_slices
	 * nal_ref_idc
	 */
	/* FIXME more fields */
	/*
	 * parma->idr_pic_flag =
	 * parma->ref_pic_list_p0
	 * parma->ref_pic_list_b0
	 * parma->ref_pic_list_b1
	 */
	param->top_field_order_cnt =
		pic_param->CurrPic.TopFieldOrderCnt;
	param->bottom_field_order_cnt =
		pic_param->CurrPic.BottomFieldOrderCnt;

	for (uint8_t i = 0; i < 16; i++) {
		const VAPictureH264 * const va_pic =
			&pic_param->ReferenceFrames[i];
		struct v4l2_h264_dpb_entry *dpb =
			&param->dpb[i];

		/* FIXME more fields */
		/*
		 * dpb->buf_index =
		 * dpb->frame_num =
		 * dpb->pic_num =
		 * dpb->flags =
		 */
		dpb->top_field_order_cnt =
			va_pic->TopFieldOrderCnt;
		dpb->bottom_field_order_cnt =
			va_pic->BottomFieldOrderCnt;
	}
}

static struct rk_v4l2_buffer *
rk_dec_procsss_avc_object
(struct rk_dec_v4l2_context *ctx,
 VAPictureParameterBufferH264 *pic_param, 
 VASliceParameterBufferH264 *slice_param,
 VASliceParameterBufferH264 *next_slice_param,
 VAIQMatrixBufferH264 *iq_matrix,
 uint8_t *slice_data)
{
	bool is_frame = false;
	uint32_t num_ctrls;
	uint32_t ctrl_ids[5];
	uint32_t payload_sizes[5];

	struct v4l2_ctrl_h264_sps sps;
	struct v4l2_ctrl_h264_pps pps;
	struct v4l2_ctrl_h264_scaling_matrix scaling_matrix;
	struct v4l2_ctrl_h264_slice_param v4l2_slice_param;
	struct v4l2_ctrl_h264_decode_param dec_param;

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
			ptr, slice_param->slice_data_size 
			+ sizeof(start_code_prefix),
			&num_ctrls, ctrl_ids, payloads, payload_sizes);

	if (NULL != next_slice_param)
		return NULL;

	memset(&sps, 0, sizeof(sps));
	memset(&pps, 0, sizeof(pps));
	memset(&scaling_matrix, 0, sizeof(scaling_matrix));
	memset(&v4l2_slice_param, 0, sizeof(v4l2_slice_param));
	memset(&dec_param, 0, sizeof(dec_param));

	rk_h264_set_sps(&sps, pic_param, ctx->profile);
	rk_h264_set_pps(&pps, pic_param, slice_param);

	rk_h264_decode_set_freq_dep_quat(&scaling_matrix, iq_matrix);
#if 1
	rk_h264_pre_slice_param(&v4l2_slice_param, payloads[3]);
	rk_h264_set_slice_param(&v4l2_slice_param, slice_param, pic_param);
#endif

	/* Half work seems wrong order decoding */
	rk_h264_pre_decode_param(&dec_param,  payloads[4]);
	rk_h264_decode_param(&dec_param, pic_param);

	memset(&ext_ctrls, 0, sizeof(ext_ctrls));
	ext_ctrls.count = num_ctrls;
	ext_ctrls.controls = calloc(num_ctrls,
			sizeof(struct v4l2_ext_control));
	ext_ctrls.request = inbuf->index;

	if (ext_ctrls.controls == NULL)
		return NULL;
#if 1
	ext_ctrls.controls[0].id = V4L2_CID_MPEG_VIDEO_H264_SPS;
	ext_ctrls.controls[0].ptr = &sps;
	ext_ctrls.controls[0].size = sizeof(struct v4l2_ctrl_h264_sps);

	ext_ctrls.controls[1].id = V4L2_CID_MPEG_VIDEO_H264_PPS;
	ext_ctrls.controls[1].ptr = &pps;
	ext_ctrls.controls[1].size = sizeof(pps);

	ext_ctrls.controls[2].id = V4L2_CID_MPEG_VIDEO_H264_SCALING_MATRIX;
	ext_ctrls.controls[2].ptr = &scaling_matrix;
	ext_ctrls.controls[2].size = sizeof(scaling_matrix);

	ext_ctrls.controls[3].id = V4L2_CID_MPEG_VIDEO_H264_SLICE_PARAM;
	ext_ctrls.controls[3].ptr = &v4l2_slice_param;
	ext_ctrls.controls[3].size = sizeof(v4l2_slice_param);

	ext_ctrls.controls[4].id = V4L2_CID_MPEG_VIDEO_H264_DECODE_PARAM;
	ext_ctrls.controls[4].ptr = &dec_param; 
	ext_ctrls.controls[4].size = sizeof(dec_param);
#else
	for (uint8_t i = 0; i < num_ctrls; ++i) {
		ext_ctrls.controls[i].id = ctrl_ids[i];
		ext_ctrls.controls[i].ptr = payloads[i];
		ext_ctrls.controls[i].size = payload_sizes[i];
	}
#endif
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
	VAIQMatrixBufferH264 *iq_matrix;

	struct object_context *obj_context;
	struct object_surface *obj_surface;

	assert(rk_v4l2_data);

	obj_context = CONTEXT(rk_data->current_context_id);
	ASSERT(obj_context);

	obj_surface =
	SURFACE(obj_context->codec_state.decode.current_render_target);
	ASSERT(obj_surface);

	if (decode_state->iq_matrix && decode_state->iq_matrix->buffer)
		iq_matrix = (VAIQMatrixBufferH264 *)
			decode_state->iq_matrix->buffer;
	else
		iq_matrix = NULL;

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
			outbuf = rk_dec_procsss_avc_object(rk_v4l2_data,
					pic_param, slice_param,
					next_slice_param, iq_matrix, slice_data);
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
