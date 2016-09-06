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

#include <va/va.h>
#include "common.h"
#include "rockchip_device_info.h"

/* Extra set of chroma formats supported for H.264 decoding (beyond YUV 4:2:0) */
#define EXTRA_H264_DEC_CHROMA_FORMATS \
    (VA_RT_FORMAT_YUV400)

/* Extra set of chroma formats supported for JPEG decoding (beyond YUV 4:2:0) */
#define EXTRA_JPEG_DEC_CHROMA_FORMATS \
	(VA_RT_FORMAT_YUV400 | VA_RT_FORMAT_YUV411 | VA_RT_FORMAT_YUV422 | \
	 VA_RT_FORMAT_YUV444)

/* Defines VA profile as a 32-bit unsigned integer mask */
#define VA_PROFILE_MASK(PROFILE) \
    (1U << VAProfile##PROFILE)

#define VP9_PROFILE_MASK(PROFILE) \
    (1U << PROFILE)

static void rk3288_max_resolution(struct rockchip_driver_data *rk_data,
		struct object_config *obj_config, int *w, int *h)
{
#if VA_CHECK_VERSION(0,37,0)
	if (obj_config->profile == VAProfileHEVCMain) {
		*w = 4096;
		*h = 2306;
		rk_data->codec_info->min_linear_wpitch = 64;
		rk_data->codec_info->min_linear_hpitch = 64;
	} else {
#endif
		*w = rk_data->codec_info->max_width;
		*h = rk_data->codec_info->max_height;
#if VA_CHECK_VERSION(0,37,0)
	}
#endif
}

static struct hw_codec_info rk3288_hw_codec_info = {
	.dec_hw_context_init = rk3288_dec_hw_context_init,
	.enc_hw_context_init = rk3288_enc_hw_context_init,
	/* TODO */
	//.render_init = rk3288_render_init,
	.max_resolution = rk3288_max_resolution,
	.max_resolution = NULL,

	/*
	 * If you use the CMA for DMA, remember to allocate at least
	 * 32Mbytes reserverd memory for 1920x1080 pixels in NV12
	 * */
	.max_width = 3840,
	.max_height = 2160,
	.min_linear_wpitch = 16,
	.min_linear_hpitch = 16,

	.h264_dec_chroma_formats = EXTRA_H264_DEC_CHROMA_FORMATS,

	.has_mpeg2_decoding = 0,
	.has_mpeg2_encoding = 0,
	.has_h264_decoding = 1,
	.has_h264_encoding = 0,
	.has_jpeg_decoding = 0,
	.has_jpeg_encoding = 1,
	.has_accelerated_getimage = 0,
	.has_accelerated_putimage = 0,
	.has_vp8_decoding = 0,
	.has_hevc_decoding = 0,
	.has_hevc10_decoding = 0,
};

struct hw_codec_info *rk_get_codec_info(int devid)
{
	switch (devid) {
		/* TODO */
	case 3288:
		return &rk3288_hw_codec_info;
	default:
		return NULL;
	}
}
