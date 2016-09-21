#include <stdio.h>
#include <string.h>
#include <linux/videodev2.h>

#include "h264d.h"
#include "common.h"
#include "h264_stream.h"
#include "h264d_cabac.h"
#include "h264decapi.h"
#include "h264hwd_asic.h"
#include "h264hwd_dpb.h"
#include "pv_avcdec_api.h"

#define PIC_IS_ST_TERM(dpb) \
	(dpb.flags == V4L2_H264_DPB_ENTRY_FLAG_ACTIVE)

#define PIC_IS_LT_TERM(dpb) \
	(dpb.flags == (V4L2_H264_DPB_ENTRY_FLAG_ACTIVE | V4L2_H264_DPB_ENTRY_FLAG_LONG_TERM))

/* init & return priv ctx */
void *h264d_init(void)
{
	struct rk_avc_decoder *ctx = rk_avc_decoder_alloc_ctx();
	if(!ctx) {
		printf("rk_avc_decoder_alloc_ctx fail\n");
		return NULL;
	}
	if(ctx->ops->init(ctx) < 0) {
		printf("init avc_decoder fail\n");
		rk_avc_decoder_free_ctx(ctx);
		return NULL;
	}
	printf("h264d ctx created\n");
	return (void*)ctx;
}

/* prepare data for set ctrl, return ture if buffer is a frame */
bool h264d_prepare_data_raw(void *dec, void *buffer, size_t size,
		size_t *num_ctrls, uint32_t *ctrl_ids,
		void **payloads, uint32_t *payload_sizes)
{
	static int frame = 0;
	int ret = 0; 
	struct rk_avc_decoder *ctx = (struct rk_avc_decoder*) dec;
	bool isSpsOrPps = false;
	if (dec == NULL || buffer == NULL) {
		printf("Invalid input parameters\n");
		return false;
	}
	{
		uint8_t* data = (uint8_t*)buffer;
		int i=0;
		do {
		} while(i<size && data[i++]==0x0);
		if(i>=size)
			assert(0);//return false;
		assert(data[i-1]==1);
		if((data[i]&0x1f)!=1 && (data[i]&0x1f)!=5){
			isSpsOrPps = true;
		}
	}
	ret = ctx->ops->oneframe(ctx, (uint8_t*)buffer, size);
	if (ret < 0) {
		printf("h264d_prepare_data oneframe failed\n");
		return false;
	}
	if(isSpsOrPps){
		return false;
	}

	memcpy(&ctx->slice_params[ctx->dec_param.num_slices],
			&ctx->slice_param, sizeof(struct v4l2_ctrl_h264_slice_param));

	ctx->dec_param.num_slices ++;

	*num_ctrls = H264D_NUM_CTRLS;
	ctrl_ids[0] = V4L2_CID_MPEG_VIDEO_H264_SPS;
	ctrl_ids[1] = V4L2_CID_MPEG_VIDEO_H264_PPS;
	ctrl_ids[2] = V4L2_CID_MPEG_VIDEO_H264_SCALING_MATRIX;
	ctrl_ids[3] = V4L2_CID_MPEG_VIDEO_H264_SLICE_PARAM;
	ctrl_ids[4] = V4L2_CID_MPEG_VIDEO_H264_DECODE_PARAM;

	/* be careful of their life cycle */
	payloads[0] = (void*)&ctx->sps;
	payload_sizes[0] = sizeof(ctx->sps);
	payloads[1] = (void*)&ctx->pps;
	payload_sizes[1] = sizeof(ctx->pps);
	payloads[2] = (void*)&ctx->scaling_matrix;
	payload_sizes[2] = sizeof(ctx->scaling_matrix);
	payloads[3] = (void*)&ctx->slice_param;
	payload_sizes[3] = sizeof(ctx->slice_param);
	payloads[4] = (void*)&ctx->dec_param;
	payload_sizes[4] = sizeof(ctx->slice_param);

	return true;
}

/* check input stream */
static int check_input_stream(struct v4l2_buffer *buffer)
{
	if (buffer == NULL)
		return -1;

	if (buffer->m.planes[0].bytesused < 1 ||
			buffer->m.planes[0].m.userptr == 0)
		return -1;

	return 0;
}

/* prepare data for set ctrl, return ture if buffer is a frame */
bool h264d_prepare_data(void *dec, struct v4l2_buffer *buffer,
		size_t *num_ctrls, uint32_t *ctrl_ids,
		void **payloads, uint32_t *payload_sizes)
{
	static int frame = 0;
	int i = 0;
	bool ret = true;
	if (dec == NULL || payloads == NULL || buffer == NULL ||
			ctrl_ids == NULL || payload_sizes == NULL) {
		printf("Invalid input parameters\n");
		return false;
	}
	if(check_input_stream(buffer) < 0) {
		printf("Invalid input buffer\n");
		return false;
	}
	return h264d_prepare_data_raw(dec, (void *)buffer->m.planes[0].m.userptr,
			buffer->m.planes[0].bytesused, num_ctrls, ctrl_ids,
			payloads, payload_sizes);
}

void h264d_picture_ready(void *dec, int index)
{
	struct rk_avc_decoder *ctx = (struct rk_avc_decoder*)dec;

    ctx->ops->picture_ready(dec, index);
}

int h264d_get_picture(void *dec)
{
    struct rk_avc_decoder *ctx = (struct rk_avc_decoder*)dec;

    return ctx->ops->get_picture(dec);
}

int h264d_get_unrefed_picture(void *dec)
{
    struct rk_avc_decoder *ctx = (struct rk_avc_decoder*)dec;

    return ctx->ops->get_unrefed_picture(dec);
}

void 
h264d_update_param(void *dec, VAProfile profile,
int width, int height, VAPictureParameterBufferH264 *pic_param,
VASliceParameterBufferH264 *slice_param)
{
	struct rk_avc_decoder *ctx = (struct rk_avc_decoder*) dec;
	char header[256];
	int header_len;

	header_len = write_nal_unit(NAL_UNIT_TYPE_SPS, width, height,
			profile, pic_param, slice_param, header, 
			sizeof(header));
	header_len += write_nal_unit(NAL_UNIT_TYPE_PPS, width, height,
			profile, pic_param, slice_param, header + header_len,
			sizeof(header) - header_len);

	if (memcmp(ctx->header, header, sizeof(header))) {
		h264d_prepare_data_raw(ctx, header, header_len,
				NULL, NULL, NULL, NULL);

		memcpy(ctx->header, header, sizeof(header));
	}
}

/* delect priv ctx */
void h264d_deinit(void *dec)
{
	struct rk_avc_decoder *ctx = (struct rk_avc_decoder*) dec;
	if(ctx){
		ctx->ops->deinit(ctx);
		rk_avc_decoder_free_ctx(ctx);
	}
}
