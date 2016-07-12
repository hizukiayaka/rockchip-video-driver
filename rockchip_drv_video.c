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

#include "config.h"
#include <va/va_backend.h>

#include "rockchip_driver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>

enum {
    ROCKCHIP_SURFACETYPE_YUV,
    ROCKCHIP_SURFACETYPE_INDEXED,
};

/* List of supported image formats */
typedef struct {
    unsigned int        type;
    VAImageFormat       va_format;
} rockchip_image_format_map_t;

static const rockchip_image_format_map_t
rockchip_image_formats_map[] = {
	{ ROCKCHIP_SURFACETYPE_YUV,
	 { VA_FOURCC_YV12, VA_LSB_FIRST, 12, } },
	{},

};

static void rockchip__error_message(const char *msg, ...)
{
    va_list args;

    fprintf(stderr, "rockchip_drv_video error: ");
    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);
}

static void rockchip__information_message(const char *msg, ...)
{
    va_list args;

    fprintf(stderr, "rockchip_drv_video: ");
    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);
}

VAStatus rockchip_QueryConfigProfiles(
		VADriverContextP ctx,
		VAProfile *profile_list,	/* out */
		int *num_profiles			/* out */
	)
{
    int i = 0;

    profile_list[i++] = VAProfileMPEG2Simple;
    profile_list[i++] = VAProfileMPEG2Main;
    profile_list[i++] = VAProfileMPEG4Simple;
    profile_list[i++] = VAProfileMPEG4AdvancedSimple;
    profile_list[i++] = VAProfileMPEG4Main;
    profile_list[i++] = VAProfileH264Baseline;
    profile_list[i++] = VAProfileH264Main;
    profile_list[i++] = VAProfileH264High;
    profile_list[i++] = VAProfileVC1Simple;
    profile_list[i++] = VAProfileVC1Main;
    profile_list[i++] = VAProfileVC1Advanced;

    /* If the assert fails then ROCKCHIP_MAX_PROFILES needs to be bigger */
    ASSERT(i <= ROCKCHIP_MAX_PROFILES);
    *num_profiles = i;

    return VA_STATUS_SUCCESS;
}

VAStatus rockchip_QueryConfigEntrypoints(
		VADriverContextP ctx,
		VAProfile profile,
		VAEntrypoint  *entrypoint_list,	/* out */
		int *num_entrypoints		/* out */
	)
{
    switch (profile) {
        case VAProfileMPEG2Simple:
        case VAProfileMPEG2Main:
                *num_entrypoints = 2;
                entrypoint_list[0] = VAEntrypointVLD;
                entrypoint_list[1] = VAEntrypointMoComp;
                break;

        case VAProfileMPEG4Simple:
        case VAProfileMPEG4AdvancedSimple:
        case VAProfileMPEG4Main:
                *num_entrypoints = 1;
                entrypoint_list[0] = VAEntrypointVLD;
                break;

        case VAProfileH264Baseline:
        case VAProfileH264Main:
        case VAProfileH264High:
                *num_entrypoints = 1;
                entrypoint_list[0] = VAEntrypointVLD;
                break;

        case VAProfileVC1Simple:
        case VAProfileVC1Main:
        case VAProfileVC1Advanced:
                *num_entrypoints = 1;
                entrypoint_list[0] = VAEntrypointVLD;
                break;

        default:
                *num_entrypoints = 0;
                break;
    }

    /* If the assert fails then ROCKCHIP_MAX_ENTRYPOINTS needs to be bigger */
    ASSERT(*num_entrypoints <= ROCKCHIP_MAX_ENTRYPOINTS);
    return VA_STATUS_SUCCESS;
}

VAStatus rockchip_GetConfigAttributes(
		VADriverContextP ctx,
		VAProfile profile,
		VAEntrypoint entrypoint,
		VAConfigAttrib *attrib_list,	/* in/out */
		int num_attribs
	)
{
    int i;

    /* Other attributes don't seem to be defined */
    /* What to do if we don't know the attribute? */
    for (i = 0; i < num_attribs; i++)
    {
        switch (attrib_list[i].type)
        {
          case VAConfigAttribRTFormat:
              attrib_list[i].value = VA_RT_FORMAT_YUV420;
              break;

          default:
              /* Do nothing */
              attrib_list[i].value = VA_ATTRIB_NOT_SUPPORTED;
              break;
        }
    }

    return VA_STATUS_SUCCESS;
}

static VAStatus rockchip__update_attribute(object_config_p obj_config, VAConfigAttrib *attrib)
{
    int i;
    /* Check existing attrbiutes */
    for(i = 0; obj_config->attrib_count < i; i++)
    {
        if (obj_config->attrib_list[i].type == attrib->type)
        {
            /* Update existing attribute */
            obj_config->attrib_list[i].value = attrib->value;
            return VA_STATUS_SUCCESS;
        }
    }
    if (obj_config->attrib_count < ROCKCHIP_MAX_CONFIG_ATTRIBUTES)
    {
        i = obj_config->attrib_count;
        obj_config->attrib_list[i].type = attrib->type;
        obj_config->attrib_list[i].value = attrib->value;
        obj_config->attrib_count++;
        return VA_STATUS_SUCCESS;
    }
    return VA_STATUS_ERROR_MAX_NUM_EXCEEDED;
}

VAStatus rockchip_CreateConfig(
		VADriverContextP ctx,
		VAProfile profile,
		VAEntrypoint entrypoint,
		VAConfigAttrib *attrib_list,
		int num_attribs,
		VAConfigID *config_id		/* out */
	)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus;
    int configID;
    object_config_p obj_config;
    int i;

    /* Validate profile & entrypoint */
    switch (profile) {
        case VAProfileMPEG2Simple:
        case VAProfileMPEG2Main:
                if ((VAEntrypointVLD == entrypoint) ||
                    (VAEntrypointMoComp == entrypoint))
                {
                    vaStatus = VA_STATUS_SUCCESS;
                }
                else
                {
                    vaStatus = VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT;
                }
                break;

        case VAProfileMPEG4Simple:
        case VAProfileMPEG4AdvancedSimple:
        case VAProfileMPEG4Main:
                if (VAEntrypointVLD == entrypoint)
                {
                    vaStatus = VA_STATUS_SUCCESS;
                }
                else
                {
                    vaStatus = VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT;
                }
                break;

        case VAProfileH264Baseline:
        case VAProfileH264Main:
        case VAProfileH264High:
                if (VAEntrypointVLD == entrypoint)
                {
                    vaStatus = VA_STATUS_SUCCESS;
                }
                else
                {
                    vaStatus = VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT;
                }
                break;

        case VAProfileVC1Simple:
        case VAProfileVC1Main:
        case VAProfileVC1Advanced:
                if (VAEntrypointVLD == entrypoint)
                {
                    vaStatus = VA_STATUS_SUCCESS;
                }
                else
                {
                    vaStatus = VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT;
                }
                break;

        default:
                vaStatus = VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
                break;
    }

    if (VA_STATUS_SUCCESS != vaStatus)
    {
        return vaStatus;
    }

    configID = object_heap_allocate( &driver_data->config_heap );
    obj_config = CONFIG(configID);
    if (NULL == obj_config)
    {
        vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
        return vaStatus;
    }

    obj_config->profile = profile;
    obj_config->entrypoint = entrypoint;
    obj_config->attrib_list[0].type = VAConfigAttribRTFormat;
    obj_config->attrib_list[0].value = VA_RT_FORMAT_YUV420;
    obj_config->attrib_count = 1;

    for(i = 0; i < num_attribs; i++)
    {
        vaStatus = rockchip__update_attribute(obj_config, &(attrib_list[i]));
        if (VA_STATUS_SUCCESS != vaStatus)
        {
            break;
        }
    }

    /* Error recovery */
    if (VA_STATUS_SUCCESS != vaStatus)
    {
        object_heap_free( &driver_data->config_heap, (object_base_p) obj_config);
    }
    else
    {
        *config_id = configID;
    }

    return vaStatus;
}

VAStatus rockchip_DestroyConfig(
		VADriverContextP ctx,
		VAConfigID config_id
	)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus;
    object_config_p obj_config;

    obj_config = CONFIG(config_id);
    if (NULL == obj_config)
    {
        vaStatus = VA_STATUS_ERROR_INVALID_CONFIG;
        return vaStatus;
    }

    object_heap_free( &driver_data->config_heap, (object_base_p) obj_config);
    return VA_STATUS_SUCCESS;
}

VAStatus rockchip_QueryConfigAttributes(
		VADriverContextP ctx,
		VAConfigID config_id,
		VAProfile *profile,		/* out */
		VAEntrypoint *entrypoint, 	/* out */
		VAConfigAttrib *attrib_list,	/* out */
		int *num_attribs		/* out */
	)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    object_config_p obj_config;
    int i;

    obj_config = CONFIG(config_id);
    ASSERT(obj_config);

    *profile = obj_config->profile;
    *entrypoint = obj_config->entrypoint;
    *num_attribs =  obj_config->attrib_count;
    for(i = 0; i < obj_config->attrib_count; i++)
    {
        attrib_list[i] = obj_config->attrib_list[i];
    }

    return vaStatus;
}

VAStatus rockchip_CreateSurfaces(
		VADriverContextP ctx,
		int width,
		int height,
		int format,
		int num_surfaces,
		VASurfaceID *surfaces		/* out */
	)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    int i;

    /* We only support one format */
    if (VA_RT_FORMAT_YUV420 != format)
    {
        return VA_STATUS_ERROR_UNSUPPORTED_RT_FORMAT;
    }

    for (i = 0; i < num_surfaces; i++)
    {
        int surfaceID = object_heap_allocate( &driver_data->surface_heap );
        object_surface_p obj_surface = SURFACE(surfaceID);
        if (NULL == obj_surface)
        {
            vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
            break;
        }
        obj_surface->surface_id = surfaceID;
        surfaces[i] = surfaceID;
    }

    /* Error recovery */
    if (VA_STATUS_SUCCESS != vaStatus)
    {
        /* surfaces[i-1] was the last successful allocation */
        for(; i--; )
        {
            object_surface_p obj_surface = SURFACE(surfaces[i]);
            surfaces[i] = VA_INVALID_SURFACE;
            ASSERT(obj_surface);
            object_heap_free( &driver_data->surface_heap, (object_base_p) obj_surface);
        }
    }

    return vaStatus;
}

VAStatus rockchip_DestroySurfaces(
		VADriverContextP ctx,
		VASurfaceID *surface_list,
		int num_surfaces
	)
{
    INIT_DRIVER_DATA
    int i;
    for(i = num_surfaces; i--; )
    {
        object_surface_p obj_surface = SURFACE(surface_list[i]);
        ASSERT(obj_surface);
        object_heap_free( &driver_data->surface_heap, (object_base_p) obj_surface);
    }
    return VA_STATUS_SUCCESS;
}

VAStatus rockchip_QueryImageFormats(
	VADriverContextP ctx,
	VAImageFormat *format_list,        /* out */
	int *num_formats           /* out */
)
{
    int n = 0;

	for (n = 0; rockchip_image_formats_map[n].va_format.fourcc != 0; n++)
	{
	    const rockchip_image_format_map_t * const m = &rockchip_image_formats_map[n];
		if (format_list)
		    format_list[n] = m->va_format;
	}

	if (num_formats)
	    *num_formats = n;

    return VA_STATUS_SUCCESS;
}

VAStatus rockchip_CreateImage(
	VADriverContextP ctx,
	VAImageFormat *format,
	int width,
	int height,
	VAImage *out_image     /* out */
)
{
    INIT_DRIVER_DATA
	struct object_image *obj_image;
	VAStatus va_status = VA_STATUS_ERROR_OPERATION_FAILED;
	VAImageID image_id;
	unsigned int size2, size;

	out_image->image_id = VA_INVALID_ID;
	out_image->buf      = VA_INVALID_ID;

	image_id = NEW_IMAGE_ID();
	if (image_id == VA_INVALID_ID)
		return VA_STATUS_ERROR_ALLOCATION_FAILED;

	obj_image = IMAGE(image_id);
	if (!obj_image)
		return VA_STATUS_ERROR_ALLOCATION_FAILED;
	obj_image->palette    = NULL;

	VAImage * const image = &obj_image->image;
	image->image_id       = image_id;
	image->buf            = VA_INVALID_ID;

	size = width * height;
	size2 = (width / 2) * (height / 2);

	switch (format->fourcc) {
	case VA_FOURCC_YV12:
		image->num_planes = 3;
		image->pitches[0] = width;
		image->offsets[0] = 0;
		image->pitches[1] = width / 2;
		image->offsets[1] = size;
		image->pitches[2] = width / 2;
		image->offsets[2] = size + size2;
		image->data_size  = size + 2 * size2;
		break;
	default:
		goto error;

	}

	va_status = rockchip_CreateBuffer(ctx, 0, VAImageBufferType,
				image->data_size, 1, NULL, &image->buf);
	if (va_status != VA_STATUS_SUCCESS)
			goto error;

	struct object_buffer *obj_buffer = BUFFER(image->buf);
	if (!obj_buffer)
		return VA_STATUS_ERROR_ALLOCATION_FAILED;

	image->image_id             = image_id;
	image->format               = *format;
	image->width                = width;
	image->height               = height;

	*out_image                  = *image;
    return VA_STATUS_SUCCESS;

error:
	rockchip_DestroyImage(ctx, image_id);
	return va_status;
}

VAStatus rockchip_DeriveImage(
	VADriverContextP ctx,
	VASurfaceID surface,
	VAImage *image     /* out */
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

VAStatus rockchip_DestroyImage(
	VADriverContextP ctx,
	VAImageID image
)
{
    INIT_DRIVER_DATA
	struct object_image *obj_image = IMAGE(image);

	if (!obj_image)
		return VA_STATUS_SUCCESS;
	if (obj_image->image.buf != VA_INVALID_ID) {
			rockchip_DestroyBuffer(ctx, obj_image->image.buf);
			obj_image->image.buf = VA_INVALID_ID;
	}

	if (obj_image->palette) {
			free(obj_image->palette);
			obj_image->palette = NULL;
	}
	
	object_heap_free(&driver_data->image_heap, (struct object_base *)obj_image);

    return VA_STATUS_SUCCESS;
}

VAStatus rockchip_SetImagePalette(
	VADriverContextP ctx,
	VAImageID image,
	unsigned char *palette
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

#define SQUARE_SIZE 5
#define MIN(X,Y) ((X) < (Y) ? (X) : (Y))
static VAStatus
get_image_i420(struct object_image *obj_image, uint8_t *image_data,
               struct object_surface *obj_surface,
               const VARectangle *rect)
{
	uint8_t *dst[3];
	/* Always YVU 4:2:0 */
	const int Y = 0;
	const int V = 1;
	const int U = 2;
	int x, y;
	double rx, ry;
	static int t = 0;

	VAStatus va_status = VA_STATUS_SUCCESS;

	/* Dest VA image has either I420 or YV12 format. */
	dst[Y] = image_data + obj_image->image.offsets[Y];
	dst[U] = image_data + obj_image->image.offsets[U];
	dst[V] = image_data + obj_image->image.offsets[V];

	memset(dst[Y], 0, rect->width * rect->height);
	memset(dst[U], 128, (rect->width / 2) * (rect->height / 2));
	memset(dst[V], 128, (rect->width / 2) * (rect->height / 2));

	rx = cos(7 * t / 3.14 / 25 * 100 / rect->width);
	ry = sin(6 * t / 3.14 / 25 * 100 / rect->width);

	x = (rx + 1) / 2 * (rect->width - 2 * SQUARE_SIZE) + SQUARE_SIZE;
	y = (ry + 1) / 2 * (rect->height - 2 * SQUARE_SIZE) + SQUARE_SIZE;

	for(int i = MIN(SQUARE_SIZE, rect->width) - 1; i >= 0; i--)
		for(int j = MIN(SQUARE_SIZE, rect->height) - 1; j >= 0; --j)
			dst[Y][x + i + (y + j) * rect->width] = 255;

	t++;

	return va_status;
}

VAStatus rockchip_GetImage(
	VADriverContextP ctx,
	VASurfaceID surface,
	int x,     /* coordinates of the upper left source pixel */
	int y,
	unsigned int width, /* width and height of the region */
	unsigned int height,
	VAImageID image
)
{
	INIT_DRIVER_DATA

	VARectangle rect;
	VAStatus va_status;
	void *image_data = NULL;
	
	struct object_surface * const obj_surface = SURFACE(surface);
	struct object_image * const obj_image = IMAGE(image);

	if (!obj_surface)
			return VA_STATUS_ERROR_INVALID_SURFACE;
	if (!obj_image)
			return VA_STATUS_ERROR_INVALID_IMAGE;

	rect.x = x;
	rect.y = y;
	rect.width = width;
	rect.height = height;

	va_status = rockchip_MapBuffer(ctx, obj_image->image.buf, &image_data);
	va_status = get_image_i420(obj_image, image_data,
           obj_surface, &rect);

	va_status = rockchip_UnmapBuffer(ctx, obj_image->image.buf);

	return va_status;

    return VA_STATUS_SUCCESS;
}

VAStatus rockchip_PutImage(
	VADriverContextP ctx,
	VASurfaceID surface,
	VAImageID image,
	int src_x,
	int src_y,
	unsigned int src_width,
	unsigned int src_height,
	int dest_x,
	int dest_y,
	unsigned int dest_width,
	unsigned int dest_height
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

VAStatus rockchip_QuerySubpictureFormats(
	VADriverContextP ctx,
	VAImageFormat *format_list,        /* out */
	unsigned int *flags,       /* out */
	unsigned int *num_formats  /* out */
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

VAStatus rockchip_CreateSubpicture(
	VADriverContextP ctx,
	VAImageID image,
	VASubpictureID *subpicture   /* out */
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

VAStatus rockchip_DestroySubpicture(
	VADriverContextP ctx,
	VASubpictureID subpicture
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

VAStatus rockchip_SetSubpictureImage(
        VADriverContextP ctx,
        VASubpictureID subpicture,
        VAImageID image
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

VAStatus rockchip_SetSubpicturePalette(
	VADriverContextP ctx,
	VASubpictureID subpicture,
	/*
	 * pointer to an array holding the palette data.  The size of the array is
	 * num_palette_entries * entry_bytes in size.  The order of the components
	 * in the palette is described by the component_order in VASubpicture struct
	 */
	unsigned char *palette
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

VAStatus rockchip_SetSubpictureChromakey(
	VADriverContextP ctx,
	VASubpictureID subpicture,
	unsigned int chromakey_min,
	unsigned int chromakey_max,
	unsigned int chromakey_mask
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

VAStatus rockchip_SetSubpictureGlobalAlpha(
	VADriverContextP ctx,
	VASubpictureID subpicture,
	float global_alpha 
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}


VAStatus rockchip_AssociateSubpicture(
	VADriverContextP ctx,
	VASubpictureID subpicture,
	VASurfaceID *target_surfaces,
	int num_surfaces,
	short src_x, /* upper left offset in subpicture */
	short src_y,
	unsigned short src_width,
	unsigned short src_height,
	short dest_x, /* upper left offset in surface */
	short dest_y,
	unsigned short dest_width,
	unsigned short dest_height,
	/*
	 * whether to enable chroma-keying or global-alpha
	 * see VA_SUBPICTURE_XXX values
	 */
	unsigned int flags
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

VAStatus rockchip_DeassociateSubpicture(
	VADriverContextP ctx,
	VASubpictureID subpicture,
	VASurfaceID *target_surfaces,
	int num_surfaces
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

VAStatus rockchip_CreateContext(
		VADriverContextP ctx,
		VAConfigID config_id,
		int picture_width,
		int picture_height,
		int flag,
		VASurfaceID *render_targets,
		int num_render_targets,
		VAContextID *context		/* out */
	)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    object_config_p obj_config;
    int i;

    obj_config = CONFIG(config_id);
    if (NULL == obj_config)
    {
        vaStatus = VA_STATUS_ERROR_INVALID_CONFIG;
        return vaStatus;
    }

    /* Validate flag */
    /* Validate picture dimensions */

    int contextID = object_heap_allocate( &driver_data->context_heap );
    object_context_p obj_context = CONTEXT(contextID);
    if (NULL == obj_context)
    {
        vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
        return vaStatus;
    }

    obj_context->context_id  = contextID;
    *context = contextID;
    obj_context->current_render_target = -1;
    obj_context->config_id = config_id;
    obj_context->picture_width = picture_width;
    obj_context->picture_height = picture_height;
    obj_context->num_render_targets = num_render_targets;
    obj_context->render_targets = (VASurfaceID *) malloc(num_render_targets * sizeof(VASurfaceID));
    if (obj_context->render_targets == NULL)
    {
        vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
        return vaStatus;
    }
    
    for(i = 0; i < num_render_targets; i++)
    {
        if (NULL == SURFACE(render_targets[i]))
        {
            vaStatus = VA_STATUS_ERROR_INVALID_SURFACE;
            break;
        }
        obj_context->render_targets[i] = render_targets[i];
    }
    obj_context->flags = flag;

    /* Error recovery */
    if (VA_STATUS_SUCCESS != vaStatus)
    {
        obj_context->context_id = -1;
        obj_context->config_id = -1;
        free(obj_context->render_targets);
        obj_context->render_targets = NULL;
        obj_context->num_render_targets = 0;
        obj_context->flags = 0;
        object_heap_free( &driver_data->context_heap, (object_base_p) obj_context);
    }

    return vaStatus;
}


VAStatus rockchip_DestroyContext(
		VADriverContextP ctx,
		VAContextID context
	)
{
    INIT_DRIVER_DATA
    object_context_p obj_context = CONTEXT(context);
    ASSERT(obj_context);

    obj_context->context_id = -1;
    obj_context->config_id = -1;
    obj_context->picture_width = 0;
    obj_context->picture_height = 0;
    if (obj_context->render_targets)
    {
        free(obj_context->render_targets);
    }
    obj_context->render_targets = NULL;
    obj_context->num_render_targets = 0;
    obj_context->flags = 0;

    obj_context->current_render_target = -1;

    object_heap_free( &driver_data->context_heap, (object_base_p) obj_context);

    return VA_STATUS_SUCCESS;
}



static VAStatus rockchip__allocate_buffer(object_buffer_p obj_buffer, int size)
{
    VAStatus vaStatus = VA_STATUS_SUCCESS;

    obj_buffer->buffer_data = realloc(obj_buffer->buffer_data, size);
    if (NULL == obj_buffer->buffer_data)
    {
        vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
    }
    return vaStatus;
}

VAStatus rockchip_CreateBuffer(
		VADriverContextP ctx,
                VAContextID context,	/* in */
                VABufferType type,	/* in */
                unsigned int size,		/* in */
                unsigned int num_elements,	/* in */
                void *data,			/* in */
                VABufferID *buf_id		/* out */
)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    int bufferID;
    object_buffer_p obj_buffer;

    /* Validate type */
    switch (type)
    {
        case VAPictureParameterBufferType:
        case VAIQMatrixBufferType:
        case VABitPlaneBufferType:
        case VASliceGroupMapBufferType:
        case VASliceParameterBufferType:
        case VASliceDataBufferType:
        case VAMacroblockParameterBufferType:
        case VAResidualDataBufferType:
        case VADeblockingParameterBufferType:
        case VAImageBufferType:
            /* Ok */
            break;
        default:
            vaStatus = VA_STATUS_ERROR_UNSUPPORTED_BUFFERTYPE;
            return vaStatus;
    }

    bufferID = object_heap_allocate( &driver_data->buffer_heap );
    obj_buffer = BUFFER(bufferID);
    if (NULL == obj_buffer)
    {
        vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
        return vaStatus;
    }

    obj_buffer->buffer_data = NULL;

    vaStatus = rockchip__allocate_buffer(obj_buffer, size * num_elements);
    if (VA_STATUS_SUCCESS == vaStatus)
    {
        obj_buffer->max_num_elements = num_elements;
        obj_buffer->num_elements = num_elements;
        if (data)
        {
            memcpy(obj_buffer->buffer_data, data, size * num_elements);
        }
    }

    if (VA_STATUS_SUCCESS == vaStatus)
    {
        *buf_id = bufferID;
    }

    return vaStatus;
}


VAStatus rockchip_BufferSetNumElements(
		VADriverContextP ctx,
		VABufferID buf_id,	/* in */
        unsigned int num_elements	/* in */
	)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    object_buffer_p obj_buffer = BUFFER(buf_id);
    ASSERT(obj_buffer);

    if ((num_elements < 0) || (num_elements > obj_buffer->max_num_elements))
    {
        vaStatus = VA_STATUS_ERROR_UNKNOWN;
    }
    if (VA_STATUS_SUCCESS == vaStatus)
    {
        obj_buffer->num_elements = num_elements;
    }

    return vaStatus;
}

VAStatus rockchip_MapBuffer(
		VADriverContextP ctx,
		VABufferID buf_id,	/* in */
		void **pbuf         /* out */
	)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_ERROR_UNKNOWN;
    object_buffer_p obj_buffer = BUFFER(buf_id);
    ASSERT(obj_buffer);
    if (NULL == obj_buffer)
    {
        vaStatus = VA_STATUS_ERROR_INVALID_BUFFER;
        return vaStatus;
    }

    if (NULL != obj_buffer->buffer_data)
    {
        *pbuf = obj_buffer->buffer_data;
        vaStatus = VA_STATUS_SUCCESS;
    }
    return vaStatus;
}

VAStatus rockchip_UnmapBuffer(
		VADriverContextP ctx,
		VABufferID buf_id	/* in */
	)
{
    /* Do nothing */
    return VA_STATUS_SUCCESS;
}

static void rockchip__destroy_buffer(struct rockchip_driver_data *driver_data, object_buffer_p obj_buffer)
{
    if (NULL != obj_buffer->buffer_data)
    {
        free(obj_buffer->buffer_data);
        obj_buffer->buffer_data = NULL;
    }

    object_heap_free( &driver_data->buffer_heap, (object_base_p) obj_buffer);
}

VAStatus rockchip_DestroyBuffer(
		VADriverContextP ctx,
		VABufferID buffer_id
	)
{
    INIT_DRIVER_DATA
    object_buffer_p obj_buffer = BUFFER(buffer_id);
    if(NULL == obj_buffer)
        return VA_STATUS_ERROR_INVALID_BUFFER;

	rockchip__destroy_buffer(driver_data, obj_buffer);
    return VA_STATUS_SUCCESS;
}

VAStatus rockchip_BeginPicture(
		VADriverContextP ctx,
		VAContextID context,
		VASurfaceID render_target
	)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    object_context_p obj_context;
    object_surface_p obj_surface;

    obj_context = CONTEXT(context);
    ASSERT(obj_context);

    obj_surface = SURFACE(render_target);
    ASSERT(obj_surface);

    obj_context->current_render_target = obj_surface->base.id;

    return vaStatus;
}

VAStatus rockchip_RenderPicture(
		VADriverContextP ctx,
		VAContextID context,
		VABufferID *buffers,
		int num_buffers
	)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    object_context_p obj_context;
    object_surface_p obj_surface;
    int i;

    obj_context = CONTEXT(context);
    ASSERT(obj_context);

    obj_surface = SURFACE(obj_context->current_render_target);
    ASSERT(obj_surface);

    /* verify that we got valid buffer references */
    for(i = 0; i < num_buffers; i++)
    {
        object_buffer_p obj_buffer = BUFFER(buffers[i]);
        ASSERT(obj_buffer);
        if (NULL == obj_buffer)
        {
            vaStatus = VA_STATUS_ERROR_INVALID_BUFFER;
            break;
        }
    }
    
    /* Release buffers */
    for(i = 0; i < num_buffers; i++)
    {
        object_buffer_p obj_buffer = BUFFER(buffers[i]);
        ASSERT(obj_buffer);
        rockchip__destroy_buffer(driver_data, obj_buffer);
    }

    return vaStatus;
}

VAStatus rockchip_EndPicture(
		VADriverContextP ctx,
		VAContextID context
	)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    object_context_p obj_context;
    object_surface_p obj_surface;

    obj_context = CONTEXT(context);
    ASSERT(obj_context);

    obj_surface = SURFACE(obj_context->current_render_target);
    ASSERT(obj_surface);

    // For now, assume that we are done with rendering right away
    obj_context->current_render_target = -1;

    return vaStatus;
}


VAStatus rockchip_SyncSurface(
		VADriverContextP ctx,
		VASurfaceID render_target
	)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    object_surface_p obj_surface;

    obj_surface = SURFACE(render_target);
    ASSERT(obj_surface);

    return vaStatus;
}

VAStatus rockchip_QuerySurfaceStatus(
		VADriverContextP ctx,
		VASurfaceID render_target,
		VASurfaceStatus *status	/* out */
	)
{
    INIT_DRIVER_DATA
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    object_surface_p obj_surface;

    obj_surface = SURFACE(render_target);
    ASSERT(obj_surface);

    *status = VASurfaceReady;

    return vaStatus;
}

VAStatus rockchip_PutSurface(
   		VADriverContextP ctx,
		VASurfaceID surface,
		void *draw, /* X Drawable */
		short srcx,
		short srcy,
		unsigned short srcw,
		unsigned short srch,
		short destx,
		short desty,
		unsigned short destw,
		unsigned short desth,
		VARectangle *cliprects, /* client supplied clip list */
		unsigned int number_cliprects, /* number of clip rects in the clip list */
		unsigned int flags /* de-interlacing flags */
	)
{
    /* TODO */
    return VA_STATUS_ERROR_UNKNOWN;
}

/* 
 * Query display attributes 
 * The caller must provide a "attr_list" array that can hold at
 * least vaMaxNumDisplayAttributes() entries. The actual number of attributes
 * returned in "attr_list" is returned in "num_attributes".
 */
VAStatus rockchip_QueryDisplayAttributes (
		VADriverContextP ctx,
		VADisplayAttribute *attr_list,	/* out */
		int *num_attributes		/* out */
	)
{
    /* TODO */
    return VA_STATUS_ERROR_UNKNOWN;
}

/* 
 * Get display attributes 
 * This function returns the current attribute values in "attr_list".
 * Only attributes returned with VA_DISPLAY_ATTRIB_GETTABLE set in the "flags" field
 * from vaQueryDisplayAttributes() can have their values retrieved.  
 */
VAStatus rockchip_GetDisplayAttributes (
		VADriverContextP ctx,
		VADisplayAttribute *attr_list,	/* in/out */
		int num_attributes
	)
{
    /* TODO */
    return VA_STATUS_ERROR_UNKNOWN;
}

/* 
 * Set display attributes 
 * Only attributes returned with VA_DISPLAY_ATTRIB_SETTABLE set in the "flags" field
 * from vaQueryDisplayAttributes() can be set.  If the attribute is not settable or 
 * the value is out of range, the function returns VA_STATUS_ERROR_ATTR_NOT_SUPPORTED
 */
VAStatus rockchip_SetDisplayAttributes (
		VADriverContextP ctx,
		VADisplayAttribute *attr_list,
		int num_attributes
	)
{
    /* TODO */
    return VA_STATUS_ERROR_UNKNOWN;
}


VAStatus rockchip_BufferInfo(
        VADriverContextP ctx,
        VABufferID buf_id,	/* in */
        VABufferType *type,	/* out */
        unsigned int *size,    	/* out */
        unsigned int *num_elements /* out */
    )
{
    /* TODO */
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}

    

VAStatus rockchip_LockSurface(
		VADriverContextP ctx,
		VASurfaceID surface,
                unsigned int *fourcc, /* following are output argument */
                unsigned int *luma_stride,
                unsigned int *chroma_u_stride,
                unsigned int *chroma_v_stride,
                unsigned int *luma_offset,
                unsigned int *chroma_u_offset,
                unsigned int *chroma_v_offset,
                unsigned int *buffer_name,
		void **buffer
	)
{
    /* TODO */
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}

VAStatus rockchip_UnlockSurface(
		VADriverContextP ctx,
		VASurfaceID surface
	)
{
    /* TODO */
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}

VAStatus rockchip_Terminate( VADriverContextP ctx )
{
    INIT_DRIVER_DATA
    object_buffer_p obj_buffer;
    object_config_p obj_config;
    object_heap_iterator iter;

    /* Clean up left over buffers */
    obj_buffer = (object_buffer_p) object_heap_first( &driver_data->buffer_heap, &iter);
    while (obj_buffer)
    {
        rockchip__information_message("vaTerminate: bufferID %08x still allocated, destroying\n", obj_buffer->base.id);
        rockchip__destroy_buffer(driver_data, obj_buffer);
        obj_buffer = (object_buffer_p) object_heap_next( &driver_data->buffer_heap, &iter);
    }
    object_heap_destroy( &driver_data->buffer_heap );

    /* TODO cleanup */
    object_heap_destroy( &driver_data->surface_heap );

    /* TODO cleanup */
    object_heap_destroy( &driver_data->context_heap );

    /* Clean up configIDs */
    obj_config = (object_config_p) object_heap_first( &driver_data->config_heap, &iter);
    while (obj_config)
    {
        object_heap_free( &driver_data->config_heap, (object_base_p) obj_config);
        obj_config = (object_config_p) object_heap_next( &driver_data->config_heap, &iter);
    }
    object_heap_destroy( &driver_data->config_heap );

    free(ctx->pDriverData);
    ctx->pDriverData = NULL;

    return VA_STATUS_SUCCESS;
}

VAStatus VA_DRIVER_INIT_FUNC(  VADriverContextP ctx )
{
    struct VADriverVTable * const vtable = ctx->vtable;
    int result;
    struct rockchip_driver_data *driver_data;

    ctx->version_major = VA_MAJOR_VERSION;
    ctx->version_minor = VA_MINOR_VERSION;
    ctx->max_profiles = ROCKCHIP_MAX_PROFILES;
    ctx->max_entrypoints = ROCKCHIP_MAX_ENTRYPOINTS;
    ctx->max_attributes = ROCKCHIP_MAX_CONFIG_ATTRIBUTES;
    ctx->max_image_formats = ROCKCHIP_MAX_IMAGE_FORMATS;
    ctx->max_subpic_formats = ROCKCHIP_MAX_SUBPIC_FORMATS;
    ctx->max_display_attributes = ROCKCHIP_MAX_DISPLAY_ATTRIBUTES;
    ctx->str_vendor = ROCKCHIP_STR_VENDOR;

    vtable->vaTerminate = rockchip_Terminate;
    vtable->vaQueryConfigEntrypoints = rockchip_QueryConfigEntrypoints;
    vtable->vaQueryConfigProfiles = rockchip_QueryConfigProfiles;
    vtable->vaQueryConfigAttributes = rockchip_QueryConfigAttributes;
    vtable->vaCreateConfig = rockchip_CreateConfig;
    vtable->vaDestroyConfig = rockchip_DestroyConfig;
    vtable->vaGetConfigAttributes = rockchip_GetConfigAttributes;
    vtable->vaCreateSurfaces = rockchip_CreateSurfaces;
    vtable->vaDestroySurfaces = rockchip_DestroySurfaces;
    vtable->vaCreateContext = rockchip_CreateContext;
    vtable->vaDestroyContext = rockchip_DestroyContext;
    vtable->vaCreateBuffer = rockchip_CreateBuffer;
    vtable->vaBufferSetNumElements = rockchip_BufferSetNumElements;
    vtable->vaMapBuffer = rockchip_MapBuffer;
    vtable->vaUnmapBuffer = rockchip_UnmapBuffer;
    vtable->vaDestroyBuffer = rockchip_DestroyBuffer;
    vtable->vaBeginPicture = rockchip_BeginPicture;
    vtable->vaRenderPicture = rockchip_RenderPicture;
    vtable->vaEndPicture = rockchip_EndPicture;
    vtable->vaSyncSurface = rockchip_SyncSurface;
    vtable->vaQuerySurfaceStatus = rockchip_QuerySurfaceStatus;
    vtable->vaPutSurface = rockchip_PutSurface;
    vtable->vaQueryImageFormats = rockchip_QueryImageFormats;
    vtable->vaCreateImage = rockchip_CreateImage;
    vtable->vaDeriveImage = rockchip_DeriveImage;
    vtable->vaDestroyImage = rockchip_DestroyImage;
    vtable->vaSetImagePalette = rockchip_SetImagePalette;
    vtable->vaGetImage = rockchip_GetImage;
    vtable->vaPutImage = rockchip_PutImage;
    vtable->vaQuerySubpictureFormats = rockchip_QuerySubpictureFormats;
    vtable->vaCreateSubpicture = rockchip_CreateSubpicture;
    vtable->vaDestroySubpicture = rockchip_DestroySubpicture;
    vtable->vaSetSubpictureImage = rockchip_SetSubpictureImage;
    vtable->vaSetSubpictureChromakey = rockchip_SetSubpictureChromakey;
    vtable->vaSetSubpictureGlobalAlpha = rockchip_SetSubpictureGlobalAlpha;
    vtable->vaAssociateSubpicture = rockchip_AssociateSubpicture;
    vtable->vaDeassociateSubpicture = rockchip_DeassociateSubpicture;
    vtable->vaQueryDisplayAttributes = rockchip_QueryDisplayAttributes;
    vtable->vaGetDisplayAttributes = rockchip_GetDisplayAttributes;
    vtable->vaSetDisplayAttributes = rockchip_SetDisplayAttributes;
    vtable->vaLockSurface = rockchip_LockSurface;
    vtable->vaUnlockSurface = rockchip_UnlockSurface;
    vtable->vaBufferInfo = rockchip_BufferInfo;

    driver_data = (struct rockchip_driver_data *) malloc( sizeof(*driver_data) );
    ctx->pDriverData = (void *) driver_data;

    result = object_heap_init( &driver_data->config_heap, sizeof(struct object_config), CONFIG_ID_OFFSET );
    ASSERT( result == 0 );

    result = object_heap_init( &driver_data->context_heap, sizeof(struct object_context), CONTEXT_ID_OFFSET );
    ASSERT( result == 0 );

    result = object_heap_init( &driver_data->surface_heap, sizeof(struct object_surface), SURFACE_ID_OFFSET );
    ASSERT( result == 0 );

    result = object_heap_init( &driver_data->buffer_heap, sizeof(struct object_buffer), BUFFER_ID_OFFSET );
    ASSERT( result == 0 );

    result = object_heap_init( &driver_data->image_heap, sizeof(struct object_image), IMAGE_ID_OFFSET );
    ASSERT( result == 0 );


    return VA_STATUS_SUCCESS;
}

