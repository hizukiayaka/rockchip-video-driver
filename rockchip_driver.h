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

#ifndef _ROCKCHIP_DRIVER_H_
#define _ROCKCHIP_DRIVER_H_

#include "common.h"
#include <va/va.h>
#include <va/va_backend.h>
#include <va/va_vpp.h>
#include "object_heap.h"

#define ALIGN(i, n)    (((i) + (n) - 1) & ~((n) - 1))
#define IS_ALIGNED(i, n) (((i) & ((n)-1)) == 0)
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define ARRAY_ELEMS(a) (sizeof(a) / sizeof((a)[0]))
#define CLAMP(min, max, a) ((a) < (min) ? (min) : ((a) > (max) ? (max) : (a)))
#define ALIGN_FLOOR(i, n) ((i) & ~((n) - 1))

#define ROCKCHIP_MAX_PROFILES			11
#define ROCKCHIP_MAX_ENTRYPOINTS		5
#define ROCKCHIP_MAX_CONFIG_ATTRIBUTES		10
#define ROCKCHIP_MAX_IMAGE_FORMATS		1
#define ROCKCHIP_MAX_SUBPIC_FORMATS		4
#define ROCKCHIP_MAX_DISPLAY_ATTRIBUTES		4
#define ROCKCHIP_STR_VENDOR			"Rockchip Driver 1.1"

#define INIT_DRIVER_DATA	struct rockchip_driver_data * const rk_data = \
					(struct rockchip_driver_data *) ctx->pDriverData;

#define CONFIG(id)  ((object_config_p) object_heap_lookup( &rk_data->config_heap, id ))
#define CONTEXT(id) ((object_context_p) object_heap_lookup( &rk_data->context_heap, id ))
#define SURFACE(id) ((object_surface_p) object_heap_lookup( &rk_data->surface_heap, id ))
#define BUFFER(id)  ((object_buffer_p) object_heap_lookup( &rk_data->buffer_heap, id ))
#define IMAGE(id)   ((object_image_p) object_heap_lookup( &rk_data->image_heap, id))

#define NEW_CONFIG_ID() object_heap_allocate(&rk_data->config_heap);
#define NEW_CONTEXT_ID() object_heap_allocate(&rk_data->context_heap);
#define NEW_SURFACE_ID() object_heap_allocate(&rk_data->surface_heap);
#define NEW_BUFFER_ID() object_heap_allocate(&rk_data->buffer_heap);
#define NEW_IMAGE_ID() object_heap_allocate(&rk_data->image_heap);

struct rockchip_driver_data {
	struct object_heap config_heap;
	struct object_heap context_heap;
	struct object_heap surface_heap;
	struct object_heap buffer_heap;
	struct object_heap image_heap;
	struct hw_codec_info *codec_info;

	char va_vendor[256];

	VADisplayAttribute *display_attributes;
	VAContextID current_context_id;
};

static inline struct rockchip_driver_data
    *rockchip_driver_data(VADriverContextP ctx)
{
	return (struct rockchip_driver_data *) (ctx->pDriverData);
}

#define CODEC_DEC       0
#define CODEC_ENC       1
#define CODEC_PROC      2

#define NUM_SLICES     10

struct codec_state_base {
	uint32_t chroma_formats;
};

struct decode_state {
	struct codec_state_base base;
	struct buffer_store *pic_param;
	struct buffer_store **slice_params;
	struct buffer_store *iq_matrix;
	struct buffer_store *bit_plane;
	struct buffer_store *huffman_table;
	struct buffer_store **slice_datas;
	struct buffer_store *probability_data;

	int max_slice_params;
	int num_slice_params;

	int max_slice_datas;
	int num_slice_datas;

	VASurfaceID current_render_target;
	struct object_surface *render_object;
	struct object_surface *reference_objects[16];	/* Up to 2 reference surfaces are valid for MPEG-2, */
};

union codec_state {
	struct codec_state_base base;
	struct decode_state decode;
	struct codec_state_base encode;
	struct codec_state_base proc;
};


struct object_config {
	struct object_base base;
	VAProfile profile;
	VAEntrypoint entrypoint;
	VAConfigAttrib attrib_list[ROCKCHIP_MAX_CONFIG_ATTRIBUTES];
	int attrib_count;
};


struct object_context {
	struct object_base base;
	VAContextID context_id;
	VAConfigID config_id;
	VASurfaceID current_render_target;
	VASurfaceID *render_targets;
	int num_render_targets;
	int picture_width;
	int picture_height;
	int flags;
	int codec_type;
	union codec_state codec_state;
	struct hw_context *hw_context;
};

struct object_surface {
	struct object_base base;
	VASurfaceID surface_id;
	int orig_width;
	int orig_height;
	int fourcc;

	uint8_t *buffer;
};

struct buffer_store;

struct object_buffer {
	struct object_base base;
	struct buffer_store *buffer_store;

	int max_num_elements;
	int num_elements;
	int size_element;

	VABufferType type;
};

struct object_image {
	struct object_base base;
	VAImage image;
	VASurfaceID derived_surface;
	unsigned int *palette;
};

typedef struct object_config *object_config_p;
typedef struct object_context *object_context_p;
typedef struct object_surface *object_surface_p;
typedef struct object_buffer *object_buffer_p;
typedef struct object_image *object_image_p;

struct buffer_store {
	uint8_t *buffer;
	int32_t ref_count;
	int32_t num_elements;
};

struct rockchip_filter {
	VAProcFilterType type;
	int ring;
};

struct hw_context {
	VAStatus(*run) (VADriverContextP ctx,
			VAProfile profile,
			union codec_state * codec_state,
			struct hw_context * hw_context);
	void (*destroy) (void *);
	 VAStatus(*get_status) (VADriverContextP ctx,
				struct hw_context * hw_context,
				void *buffer);
};

struct hw_codec_info {
	struct hw_context *(*dec_hw_context_init) (VADriverContextP,
						   struct object_config *);
	struct hw_context *(*enc_hw_context_init) (VADriverContextP,
						   struct object_config *);
	struct hw_context *(*proc_hw_context_init) (VADriverContextP,
						    struct object_config
						    *);
	 bool(*render_init) (VADriverContextP);
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

#endif
