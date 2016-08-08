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
#include "rockchip_decoder_mpp.h"

typedef struct
{
	VAProfile va_profile;
	MppCodingType mpp_type;
} RkVaapiProfileMap;

static const RkVaapiProfileMap rk_vaapi_profiles[] = {
	{VAProfileMPEG2Simple, MPP_VIDEO_CodingMPEG2},
	{VAProfileMPEG2Main, MPP_VIDEO_CodingMPEG2},
	{VAProfileMPEG4Simple, MPP_VIDEO_CodingMPEG4},
	{VAProfileH264Baseline, MPP_VIDEO_CodingAVC},
	{VAProfileH264Main, MPP_VIDEO_CodingAVC},
	{VAProfileH264High, MPP_VIDEO_CodingAVC},
	{VAProfileVP8Version0_3, MPP_VIDEO_CodingVP8},
#if VAAPI_SUPPORT_HEVC
	{VAProfileHEVCMain, MPP_VIDEO_CodingHEVC},
#endif
	{}
};

static MppCodingType
get_vaapi_profile(VAProfile profile)
{
	const RkVaapiProfileMap *m;
	for (m = rk_vaapi_profiles; m->va_profile; m++)
		if (m->va_profile == profile)
			return m->mpp_type;
	return MPP_VIDEO_CodingUnused;
}

static void 
rk_dec_mpp_output_loop
(VADriverContextP ctx, struct rk_dec_mpp_context *rk_mpp_data)
{
	struct rockchip_driver_data *rk_data = 
		rockchip_driver_data(ctx);

	MPP_RET ret = MPP_OK;
	MppFrame frame = NULL;
	VABufferID buf_id;

	struct object_context *obj_context;
	struct object_surface *obj_surface;

	obj_context = CONTEXT(rk_data->current_context_id);
	ASSERT(obj_context);

	obj_surface =
	SURFACE(obj_context->codec_state.decode.current_render_target);
	ASSERT(obj_surface);

	do {
		ret = rk_mpp_data->mpi->decode_get_frame
			(rk_mpp_data->mctx, &frame);
		if (MPP_OK != ret) {
			break;
		};
		if (NULL != frame) {
			if (mpp_frame_get_info_change(frame)) {
				rk_mpp_data->mpi->control(rk_mpp_data->mctx, 
					MPP_DEC_SET_INFO_CHANGE_READY, NULL);
			}
			else {
				/* OK, remember to free me */
				rockchip_allocate_refernce
					(ctx, VAImageBufferType, &buf_id, 
					 (void *)frame, sizeof(MppFrame));
				rockchip_decoder_render_picture
					(ctx, rk_data->current_context_id, &buf_id, 1);
				obj_surface->buffer = 
					mpp_frame_get_buffer(frame);
				break;
			}
		}
	} while(1);
}

static VAStatus
rk_dec_mpp_decode_picture
(VADriverContextP ctx, VAProfile profile, 
union codec_state *codec_state, struct hw_context *hw_context)
{
	struct rockchip_driver_data *rk_data = 
		rockchip_driver_data(ctx);
	struct rk_dec_mpp_context *rk_mpp_data =
		(struct rk_dec_mpp_context *)hw_context;
	struct decode_state *decode_state = &codec_state->decode;

	VAStatus vaStatus;
	uint8_t *slice_data;
	MPP_RET ret = MPP_OK;
	MppPacket packet;
	VAPictureParameterBufferH264 *pic_param = NULL;
	VASliceParameterBufferH264 *slice_param, *next_slice_param, 
				   *next_slice_group_param;

	assert(rk_mpp_data);

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
			mpp_packet_init(&packet, slice_data, 
					slice_param->slice_data_size);
			ret = rk_mpp_data->mpi->decode_put_packet
				(rk_mpp_data->mctx, packet);
			/* Hardware job end here */
			slice_param++;
		}

	}
	rk_dec_mpp_output_loop(ctx, rk_mpp_data);

	return VA_STATUS_SUCCESS;
}

bool 
rk_mpp_init
(struct hw_context *hw_context, struct object_config *obj_config)
{
	struct rk_dec_mpp_context *rk_mpp_data =
		(struct rk_dec_mpp_context *)hw_context;

	MPP_RET ret = MPP_OK;
	MppCtx ctx = NULL;
	MppApi *mpi = NULL;
	MpiCmd mpi_cmd = MPP_CMD_BASE;
	MppCtxType mpp_ctx_type;
	MppCodingType mpp_codec_type;
	MppParam param = NULL;
	uint32_t spilt_on = 1;

	if (NULL == hw_context || NULL == obj_config)
	{
		return false;
	}
	
	ret = mpp_create(&ctx, &mpi);
	if (MPP_OK != ret) {
		return false;
	}

	mpp_codec_type = get_vaapi_profile(obj_config->profile);
	if (MPP_VIDEO_CodingUnused == mpp_codec_type)
		return false;

	if (VAEntrypointVLD == obj_config->entrypoint)
		mpp_ctx_type = MPP_CTX_DEC;

	ret = mpp_init(ctx, mpp_ctx_type, mpp_codec_type);
	if (MPP_OK != ret) {
		return false;
	}

	mpi_cmd = MPP_DEC_SET_PARSER_SPLIT_MODE;
	param = &spilt_on;
	ret = mpi->control(ctx, mpi_cmd, param);
	if (MPP_OK != ret) {
		return false;
	}

	rk_mpp_data->mctx = ctx;
	rk_mpp_data->mpi = mpi;

	return true;
}

void
rk_mpp_release_frame(void **data)
{
	MppFrame *frame = (MppFrame *)data;
	mpp_frame_deinit(frame);
}

struct hw_context *
decoder_mpp_create_context()
{
	struct rk_dec_mpp_context *rk_mpp_ctx =
		malloc(sizeof(struct rk_dec_mpp_context));

	if (NULL == rk_mpp_ctx)
		return NULL;

	rk_mpp_ctx->base.run = rk_dec_mpp_decode_picture;

	return (struct hw_context *) rk_mpp_ctx;
}
