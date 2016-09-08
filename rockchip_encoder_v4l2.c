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
#include "rockchip_debug.h"
#include "rockchip_encoder_v4l2.h"
#include "v4l2_utils.h"

/* 
 * Zigzag scan order of the the Luma and Chroma components
 * Note: Jpeg Spec ISO/IEC 10918-1, Figure A.6 shows the zigzag order differently.
 * The Spec is trying to show the zigzag pattern with number positions. The below
 * table will use the pattern shown by A.6 and map the position of the elements in the array
 */
static const uint32_t zigzag_direct[64] = {
    0,   1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

/* 
 * Default Luminance quantization table
 * Source: Jpeg Spec ISO/IEC 10918-1, Annex K, Table K.1
 */
static const uint8_t jpeg_luma_quant[64] = {
    16, 11, 10, 16, 24,  40,  51,  61,
    12, 12, 14, 19, 26,  58,  60,  55,
    14, 13, 16, 24, 40,  57,  69,  56,
    14, 17, 22, 29, 51,  87,  80,  62,
    18, 22, 37, 56, 68,  109, 103, 77,
    24, 35, 55, 64, 81,  104, 113, 92,
    49, 64, 78, 87, 103, 121, 120, 101,
    72, 92, 95, 98, 112, 100, 103, 99    
};

/*
 * Default Chroma quantization table
 * Source: Jpeg Spec ISO/IEC 10918-1, Annex K, Table K.2
 */
static const uint8_t jpeg_chroma_quant[64] = {
    17, 18, 24, 47, 99, 99, 99, 99,
    18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99
};

/* Mjpeg quantization tables levels 0-10 */
static const uint8_t qtable_y[11][64] = {
	{
	 80, 56, 50, 80, 120, 200, 248, 248,
	 60, 60, 72, 96, 136, 248, 248, 248,
	 72, 68, 80, 120, 200, 248, 248, 248,
	 72, 88, 112, 152, 248, 248, 248, 248,
	 92, 112, 192, 248, 248, 248, 248, 248,
	 120, 176, 248, 248, 248, 248, 248, 248,
	 152, 248, 248, 248, 248, 248, 248, 248,
	 248, 248, 248, 248, 248, 248, 248, 248},

	{
	 40, 28, 25, 40, 60, 100, 128, 160,
	 30, 30, 36, 48, 68, 152, 152, 144,
	 36, 34, 40, 60, 100, 144, 176, 144,
	 36, 44, 56, 76, 128, 224, 200, 160,
	 46, 56, 96, 144, 176, 248, 248, 200,
	 60, 88, 144, 160, 208, 248, 248, 232,
	 124, 160, 200, 224, 248, 248, 248, 248,
	 184, 232, 240, 248, 248, 248, 248, 248},

	{
	 27, 18, 17, 27, 40, 68, 88, 104,
	 20, 20, 23, 32, 44, 96, 100, 92,
	 23, 22, 27, 40, 68, 96, 116, 96,
	 23, 28, 38, 48, 88, 144, 136, 104,
	 30, 38, 62, 96, 116, 184, 176, 128,
	 40, 58, 92, 108, 136, 176, 192, 160,
	 84, 108, 136, 144, 176, 208, 200, 168,
	 120, 160, 160, 168, 192, 168, 176, 168},

	{
	 20, 14, 13, 20, 30, 50, 64, 76,
	 15, 15, 18, 24, 34, 76, 76, 72,
	 18, 16, 20, 30, 50, 72, 88, 72,
	 18, 21, 28, 36, 64, 112, 100, 80,
	 23, 28, 46, 72, 88, 136, 136, 96,
	 30, 44, 72, 80, 104, 136, 144, 116,
	 62, 80, 100, 112, 136, 152, 152, 128,
	 92, 116, 120, 124, 144, 128, 136, 124},

	{
	 16, 11, 10, 16, 24, 40, 52, 62,
	 12, 12, 14, 19, 26, 58, 60, 56,
	 14, 13, 16, 24, 40, 58, 72, 56,
	 14, 17, 22, 29, 52, 88, 80, 62,
	 18, 22, 38, 56, 68, 112, 104, 80,
	 24, 36, 56, 64, 84, 104, 116, 92,
	 50, 64, 80, 88, 104, 124, 120, 104,
	 72, 92, 96, 100, 124, 100, 104, 100},

	{
	 13, 9, 8, 13, 19, 32, 42, 50,
	 10, 10, 11, 15, 21, 46, 48, 44,
	 11, 10, 13, 19, 32, 46, 56, 46,
	 11, 14, 18, 23, 42, 72, 64, 50,
	 14, 18, 30, 46, 54, 88, 84, 62,
	 19, 28, 44, 52, 68, 84, 92, 76,
	 40, 52, 62, 72, 84, 100, 96, 84,
	 58, 76, 76, 80, 100, 80, 84, 80},

	{
	 10, 7, 6, 10, 14, 24, 31, 38,
	 7, 7, 8, 11, 16, 36, 36, 34,
	 8, 8, 10, 14, 24, 34, 42, 34,
	 8, 10, 13, 17, 32, 52, 48, 38,
	 11, 13, 22, 34, 42, 68, 62, 46,
	 14, 21, 34, 38, 50, 62, 68, 56,
	 29, 38, 48, 52, 62, 76, 72, 62,
	 44, 56, 58, 60, 68, 60, 62, 60},

	{
	 6, 4, 4, 6, 10, 16, 20, 24,
	 5, 5, 6, 8, 10, 23, 24, 22,
	 6, 5, 6, 10, 16, 23, 28, 22,
	 6, 7, 9, 12, 20, 36, 32, 25,
	 7, 9, 15, 22, 27, 44, 42, 31,
	 10, 14, 22, 26, 32, 42, 46, 38,
	 20, 26, 31, 36, 42, 48, 48, 40,
	 29, 38, 38, 40, 46, 40, 42, 40},

	{
	 3, 2, 2, 3, 5, 8, 10, 12,
	 2, 2, 3, 4, 5, 12, 12, 11,
	 3, 3, 3, 5, 8, 11, 14, 11,
	 3, 3, 4, 6, 10, 17, 16, 12,
	 4, 4, 7, 11, 14, 22, 21, 15,
	 5, 7, 11, 13, 16, 21, 23, 18,
	 10, 13, 16, 17, 21, 24, 24, 20,
	 14, 18, 19, 20, 22, 20, 21, 20},

	{
	 1, 1, 1, 1, 2, 3, 3, 4,
	 1, 1, 1, 1, 2, 4, 4, 4,
	 1, 1, 1, 2, 3, 4, 5, 4,
	 1, 1, 1, 2, 3, 6, 5, 4,
	 1, 1, 2, 4, 5, 7, 7, 5,
	 2, 2, 4, 4, 5, 7, 8, 6,
	 3, 4, 5, 6, 7, 8, 8, 7,
	 5, 6, 6, 7, 7, 7, 7, 7},

	{
	 1, 1, 1, 1, 1, 1, 1, 1,
	 1, 1, 1, 1, 1, 1, 1, 1,
	 1, 1, 1, 1, 1, 1, 1, 1,
	 1, 1, 1, 1, 1, 1, 1, 1,
	 1, 1, 1, 1, 1, 1, 1, 1,
	 1, 1, 1, 1, 1, 1, 1, 1,
	 1, 1, 1, 1, 1, 1, 1, 1,
	 1, 1, 1, 1, 1, 1, 1, 1}
};

static const uint8_t qtable_c[11][64] = {
	{
	 88, 92, 120, 240, 248, 248, 248, 248,
	 92, 108, 136, 248, 248, 248, 248, 248,
	 120, 136, 248, 248, 248, 248, 248, 248,
	 240, 248, 248, 248, 248, 248, 248, 248,
	 248, 248, 248, 248, 248, 248, 248, 248,
	 248, 248, 248, 248, 248, 248, 248, 248,
	 248, 248, 248, 248, 248, 248, 248, 248,
	 248, 248, 248, 248, 248, 248, 248, 248},

	{
	 44, 46, 60, 120, 248, 248, 248, 248,
	 46, 54, 68, 168, 248, 248, 248, 248,
	 60, 66, 144, 248, 248, 248, 248, 248,
	 120, 168, 248, 248, 248, 248, 248, 248,
	 248, 248, 248, 248, 248, 248, 248, 248,
	 248, 248, 248, 248, 248, 248, 248, 248,
	 248, 248, 248, 248, 248, 248, 248, 248,
	 248, 248, 248, 248, 248, 248, 248, 248},

	{
	 28, 30, 40, 80, 168, 168, 168, 168,
	 30, 36, 44, 112, 168, 168, 168, 168,
	 40, 44, 96, 168, 168, 168, 168, 168,
	 80, 112, 168, 168, 168, 168, 168, 168,
	 168, 168, 168, 168, 168, 168, 168, 168,
	 168, 168, 168, 168, 168, 168, 168, 168,
	 168, 168, 168, 168, 168, 168, 168, 168,
	 168, 168, 168, 168, 168, 168, 168, 168},

	{
	 21, 23, 30, 60, 124, 124, 124, 124,
	 23, 26, 34, 84, 124, 124, 124, 124,
	 30, 34, 72, 124, 124, 124, 124, 124,
	 60, 84, 124, 124, 124, 124, 124, 124,
	 124, 124, 124, 124, 124, 124, 124, 124,
	 124, 124, 124, 124, 124, 124, 124, 124,
	 124, 124, 124, 124, 124, 124, 124, 124,
	 124, 124, 124, 124, 124, 124, 124, 124},

	{
	 17, 18, 24, 48, 100, 100, 100, 100,
	 18, 21, 26, 68, 100, 100, 100, 100,
	 24, 26, 56, 100, 100, 100, 100, 100,
	 48, 68, 100, 100, 100, 100, 100, 100,
	 100, 100, 100, 100, 100, 100, 100, 100,
	 100, 100, 100, 100, 100, 100, 100, 100,
	 100, 100, 100, 100, 100, 100, 100, 100,
	 100, 100, 100, 100, 100, 100, 100, 100},

	{
	 14, 14, 19, 38, 80, 80, 80, 80,
	 14, 17, 21, 54, 80, 80, 80, 80,
	 19, 21, 46, 80, 80, 80, 80, 80,
	 38, 54, 80, 80, 80, 80, 80, 80,
	 80, 80, 80, 80, 80, 80, 80, 80,
	 80, 80, 80, 80, 80, 80, 80, 80,
	 80, 80, 80, 80, 80, 80, 80, 80,
	 80, 80, 80, 80, 80, 80, 80, 80},

	{
	 10, 11, 14, 28, 60, 60, 60, 60,
	 11, 13, 16, 40, 60, 60, 60, 60,
	 14, 16, 34, 60, 60, 60, 60, 60,
	 28, 40, 60, 60, 60, 60, 60, 60,
	 60, 60, 60, 60, 60, 60, 60, 60,
	 60, 60, 60, 60, 60, 60, 60, 60,
	 60, 60, 60, 60, 60, 60, 60, 60,
	 60, 60, 60, 60, 60, 60, 60, 60},

	{
	 7, 7, 10, 19, 40, 40, 40, 40,
	 7, 8, 10, 26, 40, 40, 40, 40,
	 10, 10, 22, 40, 40, 40, 40, 40,
	 19, 26, 40, 40, 40, 40, 40, 40,
	 40, 40, 40, 40, 40, 40, 40, 40,
	 40, 40, 40, 40, 40, 40, 40, 40,
	 40, 40, 40, 40, 40, 40, 40, 40,
	 40, 40, 40, 40, 40, 40, 40, 40},

	{
	 3, 4, 5, 9, 20, 20, 20, 20,
	 4, 4, 5, 13, 20, 20, 20, 20,
	 5, 5, 11, 20, 20, 20, 20, 20,
	 9, 13, 20, 20, 20, 20, 20, 20,
	 20, 20, 20, 20, 20, 20, 20, 20,
	 20, 20, 20, 20, 20, 20, 20, 20,
	 20, 20, 20, 20, 20, 20, 20, 20,
	 20, 20, 20, 20, 20, 20, 20, 20},

	{
	 1, 1, 2, 3, 7, 7, 7, 7,
	 1, 1, 2, 4, 7, 7, 7, 7,
	 2, 2, 4, 7, 7, 7, 7, 7,
	 3, 4, 7, 7, 7, 7, 7, 7,
	 7, 7, 7, 7, 7, 7, 7, 7,
	 7, 7, 7, 7, 7, 7, 7, 7,
	 7, 7, 7, 7, 7, 7, 7, 7,
	 7, 7, 7, 7, 7, 7, 7, 7},

	{
	 1, 1, 1, 1, 1, 1, 1, 1,
	 1, 1, 1, 1, 1, 1, 1, 1,
	 1, 1, 1, 1, 1, 1, 1, 1,
	 1, 1, 1, 1, 1, 1, 1, 1,
	 1, 1, 1, 1, 1, 1, 1, 1,
	 1, 1, 1, 1, 1, 1, 1, 1,
	 1, 1, 1, 1, 1, 1, 1, 1,
	 1, 1, 1, 1, 1, 1, 1, 1}
};

typedef struct
{
	VAProfile va_profile;
	uint32_t format;
} RkV4L2FormatMap;


const static const RkV4L2FormatMap rk_v4l2_formats[] = {
	{VAProfileJPEGBaseline, V4L2_PIX_FMT_JPEG},
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
rk_enc_prepare_buffer
(VADriverContextP ctx, struct encode_state *encode_state, 
 struct rk_enc_v4l2_context * encode_context)
{
}

static void
rk_enc_jpeg_format_qual
(VADriverContextP ctx, struct encode_state *encode_state, 
 struct rk_enc_v4l2_context * encode_context)
{
	struct object_surface *obj_surface;
	VAEncPictureParameterBufferJPEG *pic_param;
	VAQMatrixBufferJPEG *qmatrix;
	struct rk_v4l2_buffer *inbuf;
	struct v4l2_ext_controls ext_ctrls;
	struct v4l2_ctrl_jpeg_qmatrix *v4l2_qmatrix;
	uint32_t quality = 0;
	uint32_t temp;

	assert(encode_state->pic_param_ext 
			&& encode_state->pic_param_ext->buffer);
	pic_param = (VAEncPictureParameterBufferJPEG *)
		encode_state->pic_param_ext->buffer;
	quality = pic_param->quality;
    
	obj_surface = encode_state->input_yuv_object;
	inbuf = obj_surface->bo;

	/* Never use the applicant send quantization tables */

	/*
	 * As per the design, normalization of the quality factor and scaling of the Quantization tables
	 * based on the quality factor needs to be done in the driver before sending the values to the HW.
	 * But note, the driver expects the scaled quantization tables (as per below logic) to be sent as
	 * packed header information. The packed header is written as the header of the jpeg file. This
	 * header information is used to decode the jpeg file. So, it is the app's responsibility to send
	 * the correct header information (See build_packed_jpeg_header_buffer() in jpegenc.c in LibVa on
	 * how to do this). QTables can be different for different applications. If no tables are provided,
	 * the default tables in the driver are used.
	 */

	/* Normalization of the quality factor */
	if (quality > 100)
		quality = 100;
	if (quality == 0)
		quality = 1;

	/* 
	 * Apply Quality factor and clip to range [1, 255] for luma and 
	 * chroma Quantization matrices
	 */
	v4l2_qmatrix = malloc(sizeof(*v4l2_qmatrix));
	if (NULL == v4l2_qmatrix)
		return;

	/* FIXME select the quantization tables based on quality */
	memcpy(v4l2_qmatrix->lum_quantiser_matrix,
			qtable_y[8],
			64 * (sizeof(uint8_t)));

	memcpy(v4l2_qmatrix->chroma_quantiser_matrix,
			qtable_c[8],
			64 * (sizeof(uint8_t)));

	ext_ctrls.count = 1;
	ext_ctrls.controls = malloc(sizeof(struct v4l2_ext_control));
	ext_ctrls.request = inbuf->index;
	if (NULL == ext_ctrls.controls)
		return;

	ext_ctrls.controls[0].id = V4L2_CID_JPEG_QMATRIX;
	ext_ctrls.controls[0].ptr = v4l2_qmatrix;
	ext_ctrls.controls[0].size = sizeof(*v4l2_qmatrix);

	/* Set codec parameters need by VPU */
	ioctl(encode_context->v4l2_ctx->video_fd, 
			VIDIOC_S_EXT_CTRLS, &ext_ctrls);

	free(ext_ctrls.controls);
	free(v4l2_qmatrix);
}

static void
rk_enc_push_buffer
(VADriverContextP ctx, struct encode_state *encode_state, 
 struct rk_enc_v4l2_context * encode_context)
{
	struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);

	struct object_surface *obj_surface;
	struct object_buffer *obj_buffer;
	struct rk_v4l2_buffer *inbuf, *outbuf;
	VACodedBufferSegment *coded_buffer_segment = NULL;
	VAEncPackedHeaderParameterBuffer *param = NULL;
	uint8_t *header_data = (uint8_t *)
		(*encode_state->packed_header_data_ext)->buffer;
	uint32_t length_in_bits;

	/* input YUV surface */
	obj_surface = encode_state->input_yuv_object;
	inbuf = obj_surface->bo;
	/* FIXME correct the byteused for each plane */
	inbuf->plane[0].bytesused = obj_surface->size;

	/* coded buffer */
	obj_buffer = encode_state->coded_buf_object;

	/* Push input buffer */
	encode_context->v4l2_ctx->ops.qbuf_input
		(encode_context->v4l2_ctx, inbuf);

	/* Pull output buffer */
	if (0 == encode_context->v4l2_ctx->ops.dqbuf_output
			(encode_context->v4l2_ctx, &outbuf))
	{
		uint32_t header_length = 0;
		uint32_t encoded_length = 0;

		ASSERT(VAEncCodedBufferType == obj_buffer->type);

		/* The header VA-API sent is enough to decode */
		param = (VAEncPackedHeaderParameterBuffer *)
			(*encode_state->packed_header_params_ext)->buffer;
		length_in_bits = param->bit_length;

		coded_buffer_segment = (VACodedBufferSegment *)
			obj_buffer->buffer_store->buffer;

		header_length = length_in_bits / 8;
		encoded_length = rk_v4l2_buffer_total_bytesused(outbuf);
		coded_buffer_segment->size = encoded_length + header_length;

		coded_buffer_segment->buf = malloc(coded_buffer_segment->size);

		if (NULL == coded_buffer_segment->buf) {
			rk_info_msg("coded buffer failed %d\n",
					coded_buffer_segment->size);
			return;
		}

		memcpy(coded_buffer_segment->buf, header_data, header_length);
		memcpy(coded_buffer_segment->buf + header_length,
				outbuf->plane[0].data,
				encoded_length);

		coded_buffer_segment->next = NULL;
		coded_buffer_segment->bit_offset = 0;
		coded_buffer_segment->status = 0;

		/*
		 * FIXME it may be used as reconstruction buffer for the
		 * other codecs
		 */
		encode_context->v4l2_ctx->ops.qbuf_output
		(encode_context->v4l2_ctx, outbuf);
	}
	/*
	 * FIXME may not release the input buffer here, it may be used as
	 * reference for the other codecs.
	 */
	encode_context->v4l2_ctx->ops.dqbuf_input
		(encode_context->v4l2_ctx, &inbuf);
}

static VAStatus
rk_enc_v4l2_jpeg_encode_prepare
(VADriverContextP ctx,  union codec_state *codec_state, 
 struct hw_context *hw_context)
{
	struct rockchip_driver_data *rk_data = 
		rockchip_driver_data(ctx);
	struct rk_enc_v4l2_context *encode_context =
		(struct rk_enc_v4l2_context *)hw_context;
	struct encode_state *encode_state = &codec_state->encode;

	struct object_surface *obj_surface;
	struct object_buffer *obj_buffer;

	VAEncPictureParameterBufferJPEG *pic_param = 
		(VAEncPictureParameterBufferJPEG *)
		encode_state->pic_param_ext->buffer;

	obj_buffer = BUFFER(pic_param->coded_buf);

	assert(obj_buffer && obj_buffer->buffer_store 
			&& obj_buffer->buffer_store->buffer);

	if (!obj_buffer || !obj_buffer->buffer_store 
			|| !obj_buffer->buffer_store->buffer)
		return VA_STATUS_ERROR_INVALID_PARAMETER;
	encode_state->coded_buf_object = obj_buffer;

	obj_surface = SURFACE(encode_state->current_render_target);
	assert(obj_surface);

	if (obj_surface->fourcc == VA_FOURCC_NV12) {
		encode_context->input_yuv_surface 
			= encode_state->current_render_target;
		encode_state->input_yuv_object = obj_surface;

		return VA_STATUS_SUCCESS;
	}

	return VA_STATUS_ERROR_INVALID_PARAMETER;
}

static VAStatus
rk_enc_v4l2_jpeg_encode_picture
(VADriverContextP ctx,  union codec_state *codec_state, 
 struct hw_context *hw_context)
{
	struct rockchip_driver_data *rk_data = 
		rockchip_driver_data(ctx);
	struct rk_enc_v4l2_context *encode_context =
		(struct rk_enc_v4l2_context *)hw_context;
	struct rk_v4l2_object *video_ctx = encode_context->v4l2_ctx;
	struct encode_state *encode_state = &codec_state->encode;

	VAEncSliceParameterBufferJPEG *slice_param;

	struct object_context *obj_context;
	struct object_surface *obj_surface;

	assert(encode_context);

	obj_context = CONTEXT(rk_data->current_context_id);
	ASSERT(obj_context);

	obj_surface = SURFACE(encode_state->current_render_target);
	ASSERT(obj_surface);

	if (!video_ctx->input_streamon)
		rk_v4l2_streamon_all(video_ctx);

	/* get the input buffer to push input data in the future step */
	rk_enc_prepare_buffer(ctx, encode_state, encode_context);

	/* do the slice level encoding here */
	rk_enc_jpeg_format_qual(ctx, encode_state, encode_context);

	/* 
	 * I dont think I need this for loop either. Just to be consistent 
	 * with other encoding logic... 
	 * */
	for(int32_t i = 0; i < encode_state->num_slice_params_ext; i++) {
		assert(encode_state->slice_params && 
				encode_state->slice_params_ext[i]->buffer);
		slice_param = (VAEncSliceParameterBufferJPEG *)
			encode_state->slice_params_ext[i]->buffer;
		
		for(int32_t j = 0; j 
			< encode_state->slice_params_ext[i]->num_elements; j++) 
		{
		
			for(int32_t component = 0;  component
				< slice_param->num_components; component++) {
			}
			
			slice_param++;
		}
	}

	/* Push the input buffer and  get encoded result */
	rk_enc_push_buffer(ctx, encode_state, encode_context);

	/* Insert head */

	return VA_STATUS_SUCCESS;
}

static VAStatus
rk_enc_v4l2_encode_picture
(VADriverContextP ctx, VAProfile profile, 
union codec_state *codec_state, struct hw_context *hw_context)
{
	VAStatus vaStatus;

	switch(profile) {
	case VAProfileJPEGBaseline:
		vaStatus = rk_enc_v4l2_jpeg_encode_prepare
			(ctx, codec_state, hw_context);

		if (vaStatus != VA_STATUS_SUCCESS)
			return vaStatus;

		return rk_enc_v4l2_jpeg_encode_picture
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
encoder_rk_v4l2_init
(VADriverContextP ctx, struct object_context *obj_context, 
 struct hw_context *hw_context)
{
	struct rockchip_driver_data *rk_data =
		rockchip_driver_data(ctx);

	struct rk_enc_v4l2_context *rk_v4l2_ctx =
		(struct rk_enc_v4l2_context *)hw_context;
	struct rk_v4l2_object *video_ctx;
	struct object_config *obj_config;

	uint32_t v4l2_codec_type;
	int32_t ret;

	if (NULL == rk_data || NULL == obj_context || NULL == rk_v4l2_ctx)
		return VA_STATUS_ERROR_INVALID_PARAMETER;
	
	obj_config = CONFIG(obj_context->config_id);
	ASSERT_RET(obj_config, VA_STATUS_ERROR_INVALID_CONFIG);

	rk_v4l2_ctx->profile = obj_config->profile;

	v4l2_codec_type = get_v4l2_codec(obj_config->profile);
	if (!v4l2_codec_type)
		return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;

	/* Create RK V4L2 Object */
	video_ctx = rk_v4l2_enc_create(NULL);
	if (NULL == video_ctx)
		return VA_STATUS_ERROR_ALLOCATION_FAILED;

	video_ctx->input_size.w = obj_context->picture_width;
	video_ctx->input_size.h = obj_context->picture_height;

	video_ctx->ops.set_codec(video_ctx, v4l2_codec_type);
	video_ctx->ops.set_format(video_ctx, 0);

	ret = video_ctx->ops.input_alloc(video_ctx, 4);
	ASSERT_RET(0 != ret, VA_STATUS_ERROR_ALLOCATION_FAILED);

	/* FIXME Keep correct nummber of the reference buffer */
	ret = video_ctx->ops.output_alloc(video_ctx, 1);
	ASSERT_RET(0 != ret, VA_STATUS_ERROR_ALLOCATION_FAILED);

	for (uint8_t i = 0; i < ret; i++)
		video_ctx->ops.qbuf_output(video_ctx,
				&video_ctx->output_buffer[i]);

	rk_v4l2_ctx->v4l2_ctx = video_ctx;

	return VA_STATUS_SUCCESS;
}

static void
encoder_v4l2_destroy_context(void *hw_ctx)
{
	struct rk_enc_v4l2_context *rk_v4l2_ctx =
		(struct rk_enc_v4l2_context *)hw_ctx;

	if (NULL == rk_v4l2_ctx)
		return;

	rk_v4l2_destroy(rk_v4l2_ctx->v4l2_ctx);
	free(rk_v4l2_ctx->v4l2_ctx);
}

struct hw_context *
encoder_v4l2_create_context()
{
	struct rk_enc_v4l2_context *rk_v4l2_ctx =
		malloc(sizeof(struct rk_enc_v4l2_context));

	if (NULL == rk_v4l2_ctx)
		return NULL;

	memset(rk_v4l2_ctx, 0, sizeof(*rk_v4l2_ctx));

	rk_v4l2_ctx->base.run = rk_enc_v4l2_encode_picture;
	rk_v4l2_ctx->base.destroy = encoder_v4l2_destroy_context;
	rk_v4l2_ctx->base.get_status = NULL;
	rk_v4l2_ctx->base.sync = NULL;

	return (struct hw_context *) rk_v4l2_ctx;
}
