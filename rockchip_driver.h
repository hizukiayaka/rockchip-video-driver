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
#include <pthread.h>
#include "object_heap.h"
#include "rockchip_buffer.h"
#include "rockchip_codec_info.h"
#include "rockchip_fourcc.h"

#define ROCKCHIP_MAX_PROFILES			18
#define ROCKCHIP_MAX_ENTRYPOINTS		2
#define ROCKCHIP_MAX_CONFIG_ATTRIBUTES		10
#define ROCKCHIP_MAX_IMAGE_FORMATS		3
#define ROCKCHIP_MAX_SUBPIC_FORMATS		1
#define ROCKCHIP_MAX_DISPLAY_ATTRIBUTES		4
#define ROCKCHIP_STR_VENDOR			"Rockchip Driver 1.3"
#define ROCKCHIP_MAX_SURFACE_ATTRIBUTES             16

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

	void *x11_backend;
};

static inline struct rockchip_driver_data
    *rockchip_driver_data(VADriverContextP ctx)
{
	return (struct rockchip_driver_data *) (ctx->pDriverData);
}

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
	VASurfaceID *render_targets;
	int num_render_targets;
	int picture_width;
	int picture_height;
	int flags;
	int codec_type;
	union codec_state codec_state;
	/* this structure would be defined at rockchip_backend.h */
	struct hw_context *hw_context;
};

#define SURFACE_REFERENCED      (1 << 0)
#define SURFACE_DERIVED         (1 << 2)
#define SURFACE_ALL_MASK        ((SURFACE_REFERENCED) | \
		(SURFACE_DERIVED))

struct object_surface {
	struct object_base base;
	VASurfaceID surface_id;
	int32_t orig_width;
	int32_t orig_height;
	int flags;
	/* After align */
	int32_t width;
	int32_t height;
	int fourcc;

	void *buffer;
	int32_t size;
	int32_t dma_fd;
	VAImageID locked_image_id;
	VAImageID derived_image_id;
};

struct object_buffer {
	struct object_base base;
	struct buffer_store *buffer_store;

	int max_num_elements;
	int num_elements;
	int size_element;

	VABufferType type;

	/* Export state */
	int32_t dma_fd;
	unsigned int export_refcount;
	VABufferInfo export_state;

	VAContextID context_id;
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

#endif
