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

#ifndef _ROCKCHIP_BACKEND_H_
#define _ROCKCHIP_BACKEND_H_

#include "common.h"
#include "rockchip_driver.h"
#include "rockchip_memory.h"

#define HAS_MPEG2_DECODING(ctx)  ((ctx)->codec_info->has_mpeg2_decoding)
#define HAS_MPEG2_ENCODING(ctx)  ((ctx)->codec_info->has_mpeg2_encoding)
#define HAS_H264_DECODING(ctx)  ((ctx)->codec_info->has_h264_decoding)
#define HAS_H264_ENCODING(ctx)  ((ctx)->codec_info->has_h264_encoding)
#define HAS_VC1_DECODING(ctx)   ((ctx)->codec_info->has_vc1_decoding)
#define HAS_JPEG_DECODING(ctx)  ((ctx)->codec_info->has_jpeg_decoding)
#define HAS_JPEG_ENCODING(ctx)  ((ctx)->codec_info->has_jpeg_encoding)
#define HAS_VP8_DECODING(ctx)   ((ctx)->codec_info->has_vp8_decoding)
#define HAS_VP8_ENCODING(ctx)   ((ctx)->codec_info->has_vp8_encoding)

#define HAS_HEVC_DECODING(ctx)          ((ctx)->codec_info->has_hevc_decoding)
#define HAS_HEVC_ENCODING(ctx)          ((ctx)->codec_info->has_hevc_encoding)
#define HAS_VP9_DECODING(ctx)          ((ctx)->codec_info->has_vp9_decoding)
#define HAS_VP9_ENCODING(ctx)          ((ctx)->codec_info->has_vp9_encoding)

#define HAS_VP9_DECODING_PROFILE(ctx, profile)                     \
	(HAS_VP9_DECODING(ctx) &&                                      \
	 ((ctx)->codec_info->vp9_dec_profiles & 			\
	  (1U << (profile - VAProfileVP9Profile0))))

struct rockchip_filter {
	VAProcFilterType type;
	int32_t ring;
};

struct hw_context {
	VAStatus(*run) (VADriverContextP ctx,
			VAProfile profile,
			union codec_state * codec_state,
			struct hw_context * hw_context);
	void (*destroy) (void *);
	VASurfaceStatus (*get_status) 
		(VADriverContextP ctx, VASurfaceID surface_id);
	bool (*sync) (VADriverContextP ctx, VASurfaceID render_target);
};

struct hw_codec_info {
	struct hw_context *(*dec_hw_context_init) (VADriverContextP,
						   struct object_config *);
	struct hw_context *(*enc_hw_context_init) (VADriverContextP,
						   struct object_config *);
	struct hw_context *(*proc_hw_context_init) (VADriverContextP,
						    struct object_config
						    *);
	bool (*render_init) (VADriverContextP);
	void (*preinit_hw_codec) (VADriverContextP,
				  struct hw_codec_info *);
	/**
	* Allows HW info to support per-codec max resolution.  If this functor is
	* not initialized, then @max_width and @max_height will be used as the
	* default maximum resolution for all codecs on this HW info.
	*/
	void (*max_resolution) (struct rockchip_driver_data *,
				struct object_config *, int *, int *);

	int max_width;
	int max_height;
	int min_linear_wpitch;
	int min_linear_hpitch;

	unsigned int vp9_dec_profiles;

	unsigned int h264_dec_chroma_formats;
	unsigned int jpeg_dec_chroma_formats;
	unsigned int jpeg_enc_chroma_formats;
	unsigned int hevc_dec_chroma_formats;
	unsigned int vp9_dec_chroma_formats;

	unsigned int has_mpeg2_decoding:1;
	unsigned int has_mpeg2_encoding:1;
	unsigned int has_h264_decoding:1;
	unsigned int has_h264_encoding:1;
	unsigned int has_vc1_decoding:1;
	unsigned int has_vc1_encoding:1;
	unsigned int has_jpeg_decoding:1;
	unsigned int has_jpeg_encoding:1;
	unsigned int has_accelerated_getimage:1;
	unsigned int has_accelerated_putimage:1;
	unsigned int has_tiled_surface:1;
	unsigned int has_vp8_decoding:1;
	unsigned int has_vp8_encoding:1;
	unsigned int has_h264_mvc_encoding:1;
	unsigned int has_hevc_decoding:1;
	unsigned int has_hevc_encoding:1;
	unsigned int has_hevc10_decoding:1;
	unsigned int has_vp9_decoding:1;
	unsigned int has_vp9_encoding:1;

	unsigned int num_filters;
	struct rockchip_filter filters[VAProcFilterCount];
};

struct hw_context *rk3288_dec_hw_context_init
(VADriverContextP ctx, struct object_config *obj_config);

struct hw_context *rk3288_enc_hw_context_init
(VADriverContextP ctx, struct object_config *obj_config);

VAStatus
rockchip_decoder_render_picture(VADriverContextP ctx, VAContextID context,
VABufferID * buffers, int num_buffers);

VAStatus
rockchip_encoder_render_picture(VADriverContextP ctx, VAContextID context,
VABufferID * buffers, int num_buffers);


#endif
