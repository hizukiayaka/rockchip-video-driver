/*
 * Copyright (c) 2015 - 2016 Rockchip Corporation. All Rights Reserved.
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
 */

#ifndef _ROCKCHIP_DRIVER_H_
#define _ROCKCHIP_DRIVER_H_

#include <va/va.h>
#include "object_heap.h"

#define ROCKCHIP_MAX_PROFILES			11
#define ROCKCHIP_MAX_ENTRYPOINTS		5
#define ROCKCHIP_MAX_CONFIG_ATTRIBUTES		10
#define ROCKCHIP_MAX_IMAGE_FORMATS		1
#define ROCKCHIP_MAX_SUBPIC_FORMATS		4
#define ROCKCHIP_MAX_DISPLAY_ATTRIBUTES		4
#define ROCKCHIP_STR_VENDOR			"Rockchip Driver 1.0"

#define INIT_DRIVER_DATA	struct rockchip_driver_data * const driver_data = \
					(struct rockchip_driver_data *) ctx->pDriverData;

#define CONFIG(id)  ((object_config_p) object_heap_lookup( &driver_data->config_heap, id ))
#define CONTEXT(id) ((object_context_p) object_heap_lookup( &driver_data->context_heap, id ))
#define SURFACE(id) ((object_surface_p) object_heap_lookup( &driver_data->surface_heap, id ))
#define BUFFER(id)  ((object_buffer_p) object_heap_lookup( &driver_data->buffer_heap, id ))
#define IMAGE(id)   ((object_image_p) object_heap_lookup( &driver_data->image_heap, id))

#define CONFIG_ID_OFFSET		0x01000000
#define CONTEXT_ID_OFFSET		0x02000000
#define SURFACE_ID_OFFSET		0x04000000
#define BUFFER_ID_OFFSET		0x08000000
#define IMAGE_ID_OFFSET			0x10000000

#define NEW_CONFIG_ID() object_heap_allocate(&driver_data->config_heap);
#define NEW_CONTEXT_ID() object_heap_allocate(&driver_data->context_heap);
#define NEW_SURFACE_ID() object_heap_allocate(&driver_data->surface_heap);
#define NEW_BUFFER_ID() object_heap_allocate(&driver_data->buffer_heap);
#define NEW_IMAGE_ID() object_heap_allocate(&driver_data->image_heap);

struct rockchip_driver_data {
    struct object_heap	config_heap;
    struct object_heap	context_heap;
    struct object_heap	surface_heap;
    struct object_heap	buffer_heap;
    struct object_heap	image_heap;
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
    int picture_width;
    int picture_height;
    int num_render_targets;
    int flags;
    VASurfaceID *render_targets;
};

struct object_surface {
    struct object_base base;
    VASurfaceID surface_id;
    int orig_width;
    int orig_height;
    int fourcc;
};

struct object_buffer {
    struct object_base base;
    void *buffer_data;
    int max_num_elements;
    int num_elements;
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
