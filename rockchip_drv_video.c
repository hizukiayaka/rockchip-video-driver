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

#include <stdarg.h>
#include <va/va.h>
#include <va/va_backend.h>

#include "config.h"
#include "rockchip_driver.h"
#include "rockchip_device_info.h"
#include "rockchip_backend.h"
#include "rockchip_image.h"
#include "rockchip_debug.h"
#ifdef HAVE_VA_EGL
#include "rockchip_x11_gles.h"
#endif
#ifdef HAVE_VA_DRM
#include "rockchip_drm_vop.h"
#endif

#define CONFIG_ID_OFFSET		0x01000000
#define CONTEXT_ID_OFFSET		0x02000000
#define SURFACE_ID_OFFSET		0x04000000
#define BUFFER_ID_OFFSET		0x08000000
#define IMAGE_ID_OFFSET			0x0a000000
#define SUBPIC_ID_OFFSET                0x10000000

/* Check whether we are rendering to X11 (VA/X11 or EGL) */
#define IS_VA_X11(ctx) \
	(((ctx)->display_type & VA_DISPLAY_MAJOR_MASK) == VA_DISPLAY_X11)
#define IS_VA_DRM(ctx) \
	(((ctx)->display_type & VA_DISPLAY_MAJOR_MASK) == VA_DISPLAY_DRM)

enum {
    ROCKCHIP_SURFACETYPE_RGBA = 1,
    ROCKCHIP_SURFACETYPE_YUV,
    ROCKCHIP_SURFACETYPE_INDEXED,
};

static const VADisplayAttribute rockchip_display_attributes[] = {
	/* None of the display attribute is supported. */
};

static VAStatus 
rockchip_MapBuffer(VADriverContextP ctx, VABufferID buf_id, void **pbuf); 

static VAStatus
rockchip_UnmapBuffer(VADriverContextP ctx, VABufferID buf_id);

static VAStatus 
rockchip_CreateBuffer(VADriverContextP ctx, VAContextID context, 
VABufferType type, unsigned int size, unsigned int num_elements,
void *data, VABufferID *buf_id);

static VAStatus rockchip_DestroyBuffer(
VADriverContextP ctx,
VABufferID buffer_id);

static VAStatus 
rockchip_DestroyImage(VADriverContextP ctx, VAImageID image);

/* List of supported image formats */
typedef struct {
    unsigned int        type;
    VAImageFormat       va_format;
} rockchip_image_format_map_t;

static const rockchip_image_format_map_t
rockchip_image_formats_map[ROCKCHIP_MAX_IMAGE_FORMATS + 1] = {
	{ ROCKCHIP_SURFACETYPE_YUV,
	 { VA_FOURCC_NV12, VA_LSB_FIRST, 12, } },
	{ ROCKCHIP_SURFACETYPE_YUV,
	 { VA_FOURCC_I420, VA_LSB_FIRST, 12, } },
	{ ROCKCHIP_SURFACETYPE_YUV,
	 { VA_FOURCC_YV12, VA_LSB_FIRST, 12, } },
	{},
};

/* Checks whether the surface is in busy state */
static bool
is_surface_busy(struct rockchip_driver_data *rk_data,
			struct object_surface *obj_surface)
{
		assert(obj_surface != NULL);

		if (obj_surface->locked_image_id != VA_INVALID_ID)
				return true;
		if (obj_surface->derived_image_id != VA_INVALID_ID)
				return true;

		return false;
}

static VAStatus rockchip_QueryConfigProfiles(
		VADriverContextP ctx,
		VAProfile *profile_list,	/* out */
		int *num_profiles			/* out */
	)
{
    struct rockchip_driver_data * const rk_data = rockchip_driver_data(ctx);
    int i = 0;

    if (HAS_MPEG2_DECODING(rk_data) ||
        HAS_MPEG2_ENCODING(rk_data)) {
        profile_list[i++] = VAProfileMPEG2Simple;
        profile_list[i++] = VAProfileMPEG2Main;
    }

    if (HAS_H264_DECODING(rk_data) ||
        HAS_H264_ENCODING(rk_data)) {
        profile_list[i++] = VAProfileH264Baseline;
        profile_list[i++] = VAProfileH264ConstrainedBaseline;
        profile_list[i++] = VAProfileH264Main;
        profile_list[i++] = VAProfileH264High;
    }

    if (HAS_VC1_DECODING(rk_data)) {
        profile_list[i++] = VAProfileVC1Simple;
        profile_list[i++] = VAProfileVC1Main;
        profile_list[i++] = VAProfileVC1Advanced;
    }

    if (HAS_JPEG_DECODING(rk_data) ||
        HAS_JPEG_ENCODING(rk_data)) {
        profile_list[i++] = VAProfileJPEGBaseline;
    }

    if (HAS_VP8_DECODING(rk_data) ||
        HAS_VP8_ENCODING(rk_data)) {
        profile_list[i++] = VAProfileVP8Version0_3;
    }

#if VA_CHECK_VERSION(0,37,0)
    if (HAS_HEVC_DECODING(rk_data)||
        HAS_HEVC_ENCODING(rk_data)) {
        profile_list[i++] = VAProfileHEVCMain;
    }
#endif

#if VA_CHECK_VERSION(0,38,0)
    if(HAS_VP9_DECODING_PROFILE(rk_data, VAProfileVP9Profile0) ||
        HAS_VP9_ENCODING(rk_data)) {
        profile_list[i++] = VAProfileVP9Profile0;
    }

    if(HAS_VP9_DECODING_PROFILE(rk_data, VAProfileVP9Profile2)) {
        profile_list[i++] = VAProfileVP9Profile2;
    }
#endif

    /* If the assert fails then ROCKCHIP_MAX_PROFILES needs to be bigger */
    ASSERT(i <= ROCKCHIP_MAX_PROFILES);
    *num_profiles = i;

    return VA_STATUS_SUCCESS;
}

static VAStatus rockchip_QueryConfigEntrypoints(
		VADriverContextP ctx,
		VAProfile profile,
		VAEntrypoint  *entrypoint_list,	/* out */
		int *num_entrypoints		/* out */
	)
{
    struct rockchip_driver_data * const rk_data = rockchip_driver_data(ctx);
    int n = 0;

    switch (profile) {
        case VAProfileMPEG2Simple:
        case VAProfileMPEG2Main:
	    if (HAS_MPEG2_DECODING(rk_data))
                entrypoint_list[n++] = VAEntrypointVLD;
	    if (HAS_MPEG2_ENCODING(rk_data)) 
                entrypoint_list[n++] = VAEntrypointEncSlice;

       	    break;

        case VAProfileH264Baseline:
	case VAProfileH264ConstrainedBaseline:
        case VAProfileH264Main:
        case VAProfileH264High:
	    if (HAS_H264_DECODING(rk_data))
                entrypoint_list[n++] = VAEntrypointVLD;
	    if (HAS_H264_ENCODING(rk_data)) 
                entrypoint_list[n++] = VAEntrypointEncSlice;

            break;

	case VAProfileVP8Version0_3:
	    if (HAS_VP8_DECODING(rk_data))
                entrypoint_list[n++] = VAEntrypointVLD;

	    break;

#if VA_CHECK_VERSION(0,37,0)
	case VAProfileHEVCMain:
	    if (HAS_HEVC_DECODING(rk_data))
		    entrypoint_list[n++] = VAEntrypointVLD;
	    if (HAS_HEVC_ENCODING(rk_data))
		    entrypoint_list[n++] = VAEntrypointEncSlice;

	    break;
#endif

#if VA_CHECK_VERSION(0,38,0)
	case VAProfileVP9Profile0:
	case VAProfileVP9Profile2:
	    if(HAS_VP9_DECODING_PROFILE(rk_data, profile))
		    entrypoint_list[n++] = VAEntrypointVLD;
	    if (HAS_VP9_ENCODING(rk_data))
		    entrypoint_list[n++] = VAEntrypointEncSlice;

	    break;
#endif
	case VAProfileJPEGBaseline:
	    if (HAS_JPEG_DECODING(rk_data))
		    entrypoint_list[n++] = VAEntrypointVLD;
	    if (HAS_JPEG_ENCODING(rk_data))
		    entrypoint_list[n++] = VAEntrypointEncPicture;
	    break;

        default:
            break;
    }

    /* If the assert fails then ROCKCHIP_MAX_ENTRYPOINTS needs to be bigger */
    ASSERT_RET(n <= ROCKCHIP_MAX_ENTRYPOINTS, 
		   VA_STATUS_ERROR_OPERATION_FAILED);
    *num_entrypoints = n;

    return n > 0 ? VA_STATUS_SUCCESS : VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
}

static VAStatus rockchip_GetConfigAttributes(
		VADriverContextP ctx,
		VAProfile profile,
		VAEntrypoint entrypoint,
		VAConfigAttrib *attrib_list,	/* in/out */
		int num_attribs
	)
{
    /* Other attributes don't seem to be defined */
    /* What to do if we don't know the attribute? */
	for (uint32_t i = 0; i < num_attribs; i++)
	{
		switch (attrib_list[i].type)
		{
		case VAConfigAttribRTFormat:
			attrib_list[i].value = VA_RT_FORMAT_YUV420;
			break;
		case VAConfigAttribEncPackedHeaders:
			if (entrypoint == VAEntrypointEncPicture) { 
				if (profile == VAProfileJPEGBaseline)
					attrib_list[i].value
						= VA_ENC_PACKED_HEADER_RAW_DATA;
			}
		case VAConfigAttribEncJPEG:
			if(entrypoint == VAEntrypointEncPicture) {
				VAConfigAttribValEncJPEG *configVal 
					= (VAConfigAttribValEncJPEG*)
					&(attrib_list[i].value);
				/* Huffman coding is not used */
				(configVal->bits).arithmatic_coding_mode = 1;
				/* Only Sequential DCT is supported */
				(configVal->bits).progressive_dct_mode = 0;
				/* Support both interleaved and non-interleaved */
				(configVal->bits).non_interleaved_mode = 1;
				/* Baseline DCT is non-differential */
				(configVal->bits).differential_mode = 0;
				/* Only 3 components supported */
				(configVal->bits).max_num_components = 3;
				/* Only 1 scan per frame */
				(configVal->bits).max_num_scans = 1;
				/* Max 3 huffman tables, May not be used ? */
				(configVal->bits).max_num_huffman_tables = 3;
				/* Max 3 quantization tables */
				(configVal->bits).max_num_quantization_tables = 3;
			}
			break;
          default:
              /* Do nothing */
              attrib_list[i].value = VA_ATTRIB_NOT_SUPPORTED;
              break;
        }
    }

    return VA_STATUS_SUCCESS;
}

static VAStatus
rockchip_update_attribute(object_config_p obj_config, VAConfigAttrib *attrib)
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

static VAStatus 
rockchip_CreateConfig(
	VADriverContextP ctx,
	VAProfile profile,
	VAEntrypoint entrypoint,
	VAConfigAttrib *attrib_list,
	int num_attribs,
	VAConfigID *config_id		/* out */
)
{
    struct rockchip_driver_data * const rk_data = rockchip_driver_data(ctx);
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
	case VAProfileH264ConstrainedBaseline:
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

	case VAProfileJPEGBaseline:
		if ((HAS_JPEG_DECODING(rk_data) && entrypoint == VAEntrypointVLD) ||
			(HAS_JPEG_ENCODING(rk_data) && entrypoint == VAEntrypointEncPicture)) 
		{
                    vaStatus = VA_STATUS_SUCCESS;
		} else {
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

    configID = object_heap_allocate(&rk_data->config_heap);
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

    for(i = 0; i < num_attribs; i++)
    {
        vaStatus = rockchip_update_attribute(obj_config, &(attrib_list[i]));
        if (VA_STATUS_SUCCESS != vaStatus)
        {
            break;
        }
    }

    /* Error recovery */
    if (VA_STATUS_SUCCESS != vaStatus)
    {
        object_heap_free(&rk_data->config_heap, (object_base_p) obj_config);
    }
    else
    {
        *config_id = configID;
    }

    return vaStatus;
}

static VAStatus 
rockchip_DestroyConfig(
	VADriverContextP ctx,
	VAConfigID config_id)
{
    struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);
    VAStatus vaStatus;
    object_config_p obj_config;

    obj_config = CONFIG(config_id);
    if (NULL == obj_config)
    {
        vaStatus = VA_STATUS_ERROR_INVALID_CONFIG;
        return vaStatus;
    }

    object_heap_free( &rk_data->config_heap, (object_base_p) obj_config);
    return VA_STATUS_SUCCESS;
}

static VAStatus rockchip_QueryConfigAttributes(
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

static VAStatus rockchip_CreateSurfaces2(
		VADriverContextP ctx,
		uint32_t format,
		uint32_t width,
		uint32_t height,
		VASurfaceID *surfaces, /* out */
		uint32_t num_surfaces,
		VASurfaceAttrib    *attrib_list,
		uint32_t num_attribs
	)
{
	struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);

	VASurfaceAttribExternalBuffers *memory_attibute = NULL;
	VAStatus va_status = VA_STATUS_SUCCESS;
	int32_t i;

	for (int32_t j = 0; j < num_attribs && attrib_list; j++) {
		if ((attrib_list[j].type == VASurfaceAttribMemoryType) &&
			(attrib_list[j].flags & VA_SURFACE_ATTRIB_SETTABLE))
		{
			ASSERT_RET(attrib_list[j].value.type ==
					VAGenericValueTypeInteger,
					VA_STATUS_ERROR_INVALID_PARAMETER);

			/* Only support V4L2 buffer now */
			if (attrib_list[j].value.value.i !=
					VA_SURFACE_ATTRIB_MEM_TYPE_V4L2)

				return VA_STATUS_ERROR_ALLOCATION_FAILED;
		}

		if ((attrib_list[j].type ==
			VASurfaceAttribExternalBufferDescriptor) &&
			(attrib_list[j].flags == VA_SURFACE_ATTRIB_SETTABLE))
		{
			ASSERT_RET(attrib_list[j].value.type ==
					VAGenericValueTypePointer,
					VA_STATUS_ERROR_INVALID_PARAMETER);
			/* FIXME use this value or drop it */
			memory_attibute = (VASurfaceAttribExternalBuffers *)
				attrib_list[j].value.value.p;
		}
	}

	/* We only support one format currently */
	if (VA_RT_FORMAT_YUV420 != format)
	{
		return VA_STATUS_ERROR_UNSUPPORTED_RT_FORMAT;
	}

	for (i = 0; i < num_surfaces; i++)
	{
		int32_t surfaceID = object_heap_allocate(&rk_data->surface_heap);

		struct object_surface *obj_surface = SURFACE(surfaceID);
		if (NULL == obj_surface)
		{
		    va_status = VA_STATUS_ERROR_ALLOCATION_FAILED;
		    break;
		}
		surfaces[i] = surfaceID;

		obj_surface->surface_id = surfaceID;
		/*
		 * FIXME set the surface format by hardware info, NV12 is just
		 * a format supported by the most of Rockchip hardware.
		 * */
		obj_surface->fourcc = VA_FOURCC_NV12;

		obj_surface->orig_width = width;
		obj_surface->orig_height = height;
		obj_surface->width = ALIGN(width,
				rk_data->codec_info->min_linear_wpitch);
		obj_surface->height = ALIGN(height,
				rk_data->codec_info->min_linear_hpitch);
		obj_surface->flags = SURFACE_REFERENCED;
		obj_surface->bo = NULL;
		obj_surface->size = 0;
		obj_surface->locked_image_id = VA_INVALID_ID;
		obj_surface->derived_image_id = VA_INVALID_ID;
	}

	/* Error recovery */
	if (VA_STATUS_SUCCESS != va_status)
	{
		/* surfaces[i-1] was the last successful allocation */
		for(; i--; )
		{
			struct object_surface *obj_surface =
				SURFACE(surfaces[i]);
			surfaces[i] = VA_INVALID_SURFACE;
			ASSERT(obj_surface);
			object_heap_free(&rk_data->surface_heap,
					(object_base_p) obj_surface);
	    }
	}

	return va_status;
}

static VAStatus rockchip_CreateSurfaces(
		VADriverContextP ctx,
		int width,
		int height,
		int format,
		int num_surfaces,
		VASurfaceID *surfaces		/* out */
	)
{
	return rockchip_CreateSurfaces2(ctx, format, width, height,
			surfaces, num_surfaces, NULL, 0);
}

static VAStatus rockchip_DestroySurfaces(
		VADriverContextP ctx,
		VASurfaceID *surface_list,
		int num_surfaces
	)
{
    struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);
    struct object_surface *obj_surface;

    for(int32_t i = num_surfaces; i > 0; i--)
    {
        obj_surface = SURFACE(surface_list[i - 1]);
        ASSERT_RET(obj_surface, VA_STATUS_ERROR_INVALID_SURFACE);

        object_heap_free(&rk_data->surface_heap, (object_base_p) obj_surface);
    }
    return VA_STATUS_SUCCESS;
}

static VAStatus rockchip_QueryImageFormats(
	VADriverContextP ctx,
	VAImageFormat *format_list,        /* out */
	int *num_formats           /* out */
)
{
    uint32_t n = 0;
    for (n = 0; rockchip_image_formats_map[n].va_format.fourcc != 0; n++)
    {
       const rockchip_image_format_map_t * const m = 
	       &rockchip_image_formats_map[n];
       if (format_list)
          format_list[n] = m->va_format;
    }

    if (num_formats)
        *num_formats = n;

    return VA_STATUS_SUCCESS;
}

static VAStatus rockchip_CreateImage(
	VADriverContextP ctx,
	VAImageFormat *format,
	int width,
	int height,
	VAImage *out_image     /* out */
)
{
	struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);
	struct object_image *obj_image;
	VAStatus va_status = VA_STATUS_ERROR_OPERATION_FAILED;
	VAImageID image_id;
	uint32_t size2, size, awidth, aheight;

	out_image->image_id = VA_INVALID_ID;
	out_image->buf      = VA_INVALID_ID;

	image_id = NEW_IMAGE_ID();
	if (image_id == VA_INVALID_ID)
		return VA_STATUS_ERROR_ALLOCATION_FAILED;

	obj_image = IMAGE(image_id);
	if (!obj_image)
		return VA_STATUS_ERROR_ALLOCATION_FAILED;
	obj_image->palette    = NULL;
	obj_image->derived_surface = VA_INVALID_ID;

	VAImage * const image = &obj_image->image;
	image->image_id       = image_id;
	image->buf            = VA_INVALID_ID;

	/* Align */
	awidth = ALIGN(width, rk_data->codec_info->min_linear_wpitch);
	if ((format->fourcc == VA_FOURCC_YV12) ||
		(format->fourcc == VA_FOURCC_I420)) 
	{
		awidth = ALIGN(width, 128);
	}
	aheight = ALIGN(height, rk_data->codec_info->min_linear_hpitch);
	aheight = height;

	size = awidth * aheight;
	size2 = (awidth / 2) * (aheight / 2);

	image->entry_bytes = 0;
	image->num_palette_entries = 0;
	memset(image->component_order, 0, sizeof(image->component_order));

	switch (format->fourcc) {
	case VA_FOURCC_YV12:
	case VA_FOURCC_I420:
		image->num_planes = 3;
		image->pitches[0] = awidth;
		image->offsets[0] = 0;
		image->pitches[1] = awidth / 2;
		image->offsets[1] = size;
		image->pitches[2] = awidth / 2;
		image->offsets[2] = size + size2;
		image->data_size  = size + 2 * size2;
		break;
	case VA_FOURCC_NV12:
		image->num_planes = 2;
		image->pitches[0] = awidth;
		image->offsets[0] = 0;
		image->pitches[1] = awidth;
		image->offsets[1] = size;
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
	if (!obj_buffer || !obj_buffer->buffer_store)
		return VA_STATUS_ERROR_ALLOCATION_FAILED;

	if (image->num_palette_entries > 0 && image->entry_bytes > 0) {
		obj_image->palette = malloc(image->num_palette_entries 
				* sizeof(*obj_image->palette));
		if (!obj_image->palette)
			goto error;
	}

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

static VAStatus rockchip_DeriveImage(
	VADriverContextP ctx,
	VASurfaceID surface,
	VAImage *out_image     /* out */
)
{
	struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);
	struct object_image *obj_image;
	struct object_surface *obj_surface;
	VAImageID image_id;
	VAStatus va_status = VA_STATUS_ERROR_OPERATION_FAILED;
	uint32_t size, size2;

	obj_surface = SURFACE(surface);
	if (NULL == obj_surface)
		return VA_STATUS_ERROR_INVALID_SURFACE;

	if (NULL == obj_surface->bo) {
		va_status = rk_v4l2_assign_surface_bo(ctx, obj_surface);
		if (va_status != VA_STATUS_SUCCESS)
			return va_status;
	}

	ASSERT_RET(obj_surface->fourcc, VA_STATUS_ERROR_INVALID_SURFACE);

	image_id = NEW_IMAGE_ID();

	if (VA_INVALID_ID == image_id)
		return VA_STATUS_ERROR_ALLOCATION_FAILED;

	obj_image = IMAGE(image_id);
	if (!obj_image)
		return VA_STATUS_ERROR_ALLOCATION_FAILED;

	obj_image->palette = NULL;
	obj_image->derived_surface = VA_INVALID_ID;

	VAImage * const image = &obj_image->image;
	memset(image, 0, sizeof(*image));
	image->image_id = image_id;
	image->buf = VA_INVALID_ID;
	image->num_palette_entries = 0;
	image->entry_bytes = 0;
	image->width = obj_surface->orig_width;
	image->height = obj_surface->orig_height;
	/* FIXME set data_size here */
	/* image->data_size = obj_surface->size; */

	image->format.fourcc = obj_surface->fourcc;
	image->format.byte_order = VA_LSB_FIRST;

	size = obj_surface->width * obj_surface->height;
	size2 = (obj_surface->width / 2) * (obj_surface->height / 2);

	switch (image->format.fourcc) {
	case VA_FOURCC_NV12:
		image->num_planes = 2;
		image->pitches[0] = obj_surface->width;
		image->offsets[0] = 0;
		image->pitches[1] = obj_surface->width;
		image->offsets[1] = obj_surface->width * obj_surface->height;
		image->data_size  = size + 2 * size2;
		break;
	default:
		goto error;
	}

	va_status = rockchip_allocate_refernce(ctx, VAImageBufferType, 
			&image->buf, obj_surface->bo, obj_surface->size);

	if (VA_STATUS_SUCCESS != va_status)
		goto error;


	struct object_buffer *obj_buffer = BUFFER(image->buf);
	if (!obj_buffer || !obj_buffer->buffer_store)
		return VA_STATUS_ERROR_ALLOCATION_FAILED;

	if (image->num_palette_entries > 0 && image->entry_bytes > 0) {
		obj_image->palette = malloc(image->num_palette_entries 
				* sizeof(*obj_image->palette));
		if (!obj_image->palette)
			goto error;
	}

	*out_image = *image;
	obj_surface->flags |= SURFACE_DERIVED;
	obj_surface->derived_image_id = image_id;
	obj_image->derived_surface = surface;

	return VA_STATUS_SUCCESS;

error:
	rockchip_DestroyImage(ctx, image_id);
	return va_status;
}

static VAStatus rockchip_DestroyImage(
	VADriverContextP ctx,
	VAImageID image
)
{
	struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);
	struct object_image *obj_image = IMAGE(image);
	struct object_surface *obj_surface;

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
	
	obj_surface = SURFACE(obj_image->derived_surface);

	if (obj_surface) {
		obj_surface->flags &= ~SURFACE_DERIVED;
		obj_surface->derived_image_id = VA_INVALID_ID;
	}

	object_heap_free(&rk_data->image_heap, 
		(struct object_base *)obj_image);

    return VA_STATUS_SUCCESS;
}

static VAStatus rockchip_SetImagePalette(
	VADriverContextP ctx,
	VAImageID image,
	unsigned char *palette
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

static VAStatus
rockchip_sw_getimage(VADriverContextP ctx, struct object_surface *obj_surface,
struct object_image *obj_image, const VARectangle *rect)
{
	struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);
	struct object_context *obj_context;
	VAStatus va_status;
	void *image_data = NULL;

	if (obj_surface->fourcc != obj_image->image.format.fourcc)
		return VA_STATUS_ERROR_INVALID_IMAGE_FORMAT;

	va_status = rockchip_MapBuffer(ctx, obj_image->image.buf, &image_data);
	if (va_status != VA_STATUS_SUCCESS) {
		rk_info_msg("Memory map error\n");
		return va_status;
	}

	obj_context = CONTEXT(rk_data->current_context_id);
	ASSERT(obj_context);

	switch (obj_image->image.format.fourcc) {
	case VA_FOURCC_NV12:
		get_image_nv12_sw(obj_image, image_data, obj_surface, rect);
		break;
	default:
		va_status = VA_STATUS_ERROR_OPERATION_FAILED;
		break;
	}

	va_status = rockchip_UnmapBuffer(ctx, obj_image->image.buf);

	return va_status;

    return VA_STATUS_SUCCESS;
}


static VAStatus rockchip_GetImage(
	VADriverContextP ctx,
	VASurfaceID surface,
	int x,     /* coordinates of the upper left source pixel */
	int y,
	unsigned int width, /* width and height of the region */
	unsigned int height,
	VAImageID image
)
{
	struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);

	VARectangle rect;
	VAStatus va_status;
	
	struct object_context *obj_context = 
		CONTEXT(rk_data->current_context_id);
	struct object_surface * const obj_surface = SURFACE(surface);
	struct object_image * const obj_image = IMAGE(image);

	if (!obj_surface)
	    return VA_STATUS_ERROR_INVALID_SURFACE;
	if (!obj_image)
	    return VA_STATUS_ERROR_INVALID_IMAGE;
	/* don't get anything, keep previous data */
	if (!obj_surface->bo)
	   return VA_STATUS_SUCCESS;

	if(obj_context->hw_context->get_status) {
        	if (VASurfaceReady != obj_context->hw_context->get_status
				(ctx, surface))
			return VA_STATUS_ERROR_SURFACE_BUSY;
	}

	/* image check */
	if (x < 0 || y < 0)
		return VA_STATUS_ERROR_INVALID_PARAMETER;
	if (x + width > obj_surface->orig_width ||
		y + height > obj_surface->orig_height)
		return VA_STATUS_ERROR_INVALID_PARAMETER;
	if (x + width > obj_image->image.width ||
		y + height > obj_image->image.height)
		return VA_STATUS_ERROR_INVALID_PARAMETER;

	rect.x = x;
	rect.y = y;
	rect.width = width;
	rect.height = height;

	va_status = rockchip_sw_getimage(ctx, obj_surface, obj_image, &rect);

	return va_status;
}

static VAStatus rockchip_PutImage(
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
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}

static VAStatus rockchip_QuerySubpictureFormats(
	VADriverContextP ctx,
	VAImageFormat *format_list,        /* out */
	unsigned int *flags,       /* out */
	unsigned int *num_formats  /* out */
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

static VAStatus rockchip_CreateSubpicture(
	VADriverContextP ctx,
	VAImageID image,
	VASubpictureID *subpicture   /* out */
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

static VAStatus rockchip_DestroySubpicture(
	VADriverContextP ctx,
	VASubpictureID subpicture
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

static VAStatus rockchip_SetSubpictureImage(
        VADriverContextP ctx,
        VASubpictureID subpicture,
        VAImageID image
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

static VAStatus rockchip_SetSubpicturePalette(
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

static VAStatus rockchip_SetSubpictureChromakey(
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

static VAStatus rockchip_SetSubpictureGlobalAlpha(
	VADriverContextP ctx,
	VASubpictureID subpicture,
	float global_alpha 
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

static VAStatus rockchip_AssociateSubpicture(
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

static VAStatus rockchip_DeassociateSubpicture(
	VADriverContextP ctx,
	VASubpictureID subpicture,
	VASurfaceID *target_surfaces,
	int num_surfaces
)
{
    /* TODO */
    return VA_STATUS_SUCCESS;
}

static inline void
max_resolution(struct rockchip_driver_data *rk_data,
		struct object_config *obj_config,
		int *w,                                  /* out */
		int *h)                                  /* out */
{
	if (rk_data->codec_info->max_resolution) {
		rk_data->codec_info->max_resolution(rk_data, obj_config, w, h);
	} else {
		*w = rk_data->codec_info->max_width;
		*h = rk_data->codec_info->max_height;
	}
}

static VAStatus rockchip_CreateContext(
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
	struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);
	VAStatus vaStatus = VA_STATUS_SUCCESS;
	object_config_p obj_config;
	int32_t max_width, max_height;
	int32_t contextID;

	obj_config = CONFIG(config_id);
	if (NULL == obj_config)
	{
	    vaStatus = VA_STATUS_ERROR_INVALID_CONFIG;
	    return vaStatus;
	}

	max_resolution(rk_data, obj_config, &max_width, &max_height);
	if (picture_width > max_width ||
		picture_height > max_height) {
		vaStatus = VA_STATUS_ERROR_RESOLUTION_NOT_SUPPORTED;
		return vaStatus;
	}

	/* Validate flag */
	/* Validate picture dimensions */

	contextID = object_heap_allocate(&rk_data->context_heap);
	object_context_p obj_context = CONTEXT(contextID);
	if (NULL == obj_context)
	{
	    vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
	    return vaStatus;
	}

	obj_context->context_id  = contextID;
	*context = contextID;
	obj_context->config_id = config_id;
	obj_context->picture_width = picture_width;
	obj_context->picture_height = picture_height;
	obj_context->num_render_targets = num_render_targets;
	obj_context->render_targets = (VASurfaceID *) 
		calloc(num_render_targets, sizeof(VASurfaceID));
	obj_context->hw_context = NULL;
	obj_context->flags = flag;

	if (obj_context->render_targets == NULL)
	{
		vaStatus = VA_STATUS_ERROR_ALLOCATION_FAILED;
		return vaStatus;
	}

	for(uint8_t i = 0; i < num_render_targets; i++)
	{
		if (NULL == SURFACE(render_targets[i]))
	    	{
			vaStatus = VA_STATUS_ERROR_INVALID_SURFACE;
			break;
	    	}
		obj_context->render_targets[i] = render_targets[i];
	}

	if (VA_STATUS_SUCCESS != vaStatus)
		goto error;

	if ((VAEntrypointEncSlice == obj_config->entrypoint) ||
			(VAEntrypointEncPicture == obj_config->entrypoint)) {
		/* For encoder */
		obj_context->codec_type = CODEC_ENC;

		memset(&obj_context->codec_state.encode, 0,
				sizeof(obj_context->codec_state.encode));
		obj_context->codec_state.encode.current_render_target = VA_INVALID_ID;
		obj_context->codec_state.encode.max_slice_params = NUM_SLICES;
		obj_context->codec_state.encode.slice_params 
			= calloc(obj_context->codec_state.encode.max_slice_params,
			   sizeof(*obj_context->codec_state.encode.slice_params));
		obj_context->codec_state.encode.max_packed_header_params_ext 
			= NUM_SLICES;
		obj_context->codec_state.encode.packed_header_params_ext =
		    calloc(obj_context->codec_state.encode.max_packed_header_params_ext,
			   sizeof(struct buffer_store *));

		obj_context->codec_state.encode.max_packed_header_data_ext 
			= NUM_SLICES;
		obj_context->codec_state.encode.packed_header_data_ext =
		    calloc(obj_context->codec_state.encode.max_packed_header_data_ext,
			   sizeof(struct buffer_store *));

		obj_context->codec_state.encode.max_slice_num 
			= NUM_SLICES;
		obj_context->codec_state.encode.slice_rawdata_index =
		    calloc(obj_context->codec_state.encode.max_slice_num,
				    sizeof(int));
		obj_context->codec_state.encode.slice_rawdata_count =
		    calloc(obj_context->codec_state.encode.max_slice_num,
				    sizeof(int));

		obj_context->codec_state.encode.slice_header_index =
		    calloc(obj_context->codec_state.encode.max_slice_num,
				    sizeof(int));

		obj_context->codec_state.encode.vps_sps_seq_index = 0;

		obj_context->codec_state.encode.slice_index = 0;
		/* 
		 * use the default value. SPS/PPS/RAWDATA is passed from user
		 * while Slice_header data is generated by driver.
		 */
		obj_context->codec_state.encode.packed_header_flag =
				VA_ENC_PACKED_HEADER_SEQUENCE 
				| VA_ENC_PACKED_HEADER_PICTURE 
				| VA_ENC_PACKED_HEADER_RAW_DATA;

#if VA_CHECK_VERSION(0,38,0)
		/* it is not used for VP9 */
		if (obj_config->profile == VAProfileVP9Profile0)
			obj_context->codec_state.encode.packed_header_flag = 0;
#endif

		assert(rk_data->codec_info->enc_hw_context_init);
		obj_context->hw_context =
			rk_data->codec_info->enc_hw_context_init
			(ctx, obj_context);
	}
	else {
		/* For decoder */
		obj_context->codec_type = CODEC_DEC;
		memset(&obj_context->codec_state.decode, 0, 
				sizeof(obj_context->codec_state.decode));
		obj_context->codec_state.decode.current_render_target = -1;
		/* FIXME */
		obj_context->codec_state.decode.max_slice_datas = NUM_SLICES;
		obj_context->codec_state.decode.slice_datas = 
			calloc(obj_context->codec_state.decode.max_slice_datas,
			  sizeof(*obj_context->codec_state.decode.slice_datas));
		assert(rk_data->codec_info->dec_hw_context_init);
		/* FIXME */
		obj_context->hw_context = 
			rk_data->codec_info->dec_hw_context_init
			(ctx, obj_context);
	}

	rk_data->current_context_id = contextID;

error:
	if (VA_STATUS_SUCCESS != vaStatus)
	{
		obj_context->context_id = -1;
		obj_context->config_id = -1;
		free(obj_context->render_targets);
		obj_context->render_targets = NULL;
		obj_context->num_render_targets = 0;
		obj_context->flags = 0;
		object_heap_free( &rk_data->context_heap, 
				(object_base_p) obj_context);
	}
	return vaStatus;
}


static VAStatus rockchip_DestroyContext(
		VADriverContextP ctx,
		VAContextID context
	)
{
    struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);
    object_context_p obj_context = CONTEXT(context);
    ASSERT(obj_context);

    if (obj_context->hw_context) {
	    obj_context->hw_context->destroy(obj_context->hw_context);
            obj_context->hw_context = NULL; 
    }

    if (obj_context->codec_type == CODEC_DEC) {
	    assert(obj_context->codec_state.decode.num_slice_params 
			    <= obj_context->codec_state.decode.max_slice_params);
	    assert(obj_context->codec_state.decode.num_slice_datas 
			    <= obj_context->codec_state.decode.max_slice_datas);

	    rockchip_release_buffer_store(&obj_context->codec_state.
			    decode.pic_param);
	    rockchip_release_buffer_store(&obj_context->codec_state.
			    decode.iq_matrix);
	    for (uint32_t i = 0; 
		i < obj_context->codec_state.decode.num_slice_params; i++)
		    rockchip_release_buffer_store
			    (&obj_context->codec_state.decode.slice_params[i]);
	    for (uint32_t i = 0; 
		i < obj_context->codec_state.decode.num_slice_datas; i++)
		    rockchip_release_buffer_store
			    (&obj_context->codec_state.decode.slice_datas[i]);

	    free(obj_context->codec_state.decode.slice_params);
	    free(obj_context->codec_state.decode.slice_datas);
    }

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

    object_heap_free(&rk_data->context_heap, (object_base_p) obj_context);

    return VA_STATUS_SUCCESS;
}

static VAStatus rockchip_CreateBuffer(
		VADriverContextP ctx,
                VAContextID context,	/* in */
                VABufferType type,	/* in */
                unsigned int size,		/* in */
                unsigned int num_elements,	/* in */
                void *data,			/* in */
                VABufferID *buf_id		/* out */
)
{
    return rockchip_allocate_buffer
	    (ctx, context, type, size, num_elements, data, buf_id);
}


static VAStatus rockchip_BufferSetNumElements(
		VADriverContextP ctx,
		VABufferID buf_id,	/* in */
        unsigned int num_elements	/* in */
	)
{
    struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);

    VAStatus vaStatus = VA_STATUS_SUCCESS;
    object_buffer_p obj_buffer = BUFFER(buf_id);

    ASSERT_RET(obj_buffer, VA_STATUS_ERROR_INVALID_BUFFER);

    if ((num_elements < 0) || (num_elements > obj_buffer->max_num_elements))
    {
        vaStatus = VA_STATUS_ERROR_UNKNOWN;
    }
    if (VA_STATUS_SUCCESS == vaStatus)
    {
        obj_buffer->num_elements = num_elements;
	if (obj_buffer->buffer_store != NULL) {
		obj_buffer->buffer_store->num_elements = num_elements;
	}
    }

    return vaStatus;
}

static VAStatus rockchip_MapBuffer(
		VADriverContextP ctx,
		VABufferID buf_id,	/* in */
		void **pbuf         /* out */
	)
{
	struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);

	VAStatus va_status = VA_STATUS_ERROR_UNKNOWN;
	object_buffer_p obj_buffer = BUFFER(buf_id);
	struct object_context *obj_context;

	ASSERT_RET(obj_buffer && obj_buffer->buffer_store, 
					VA_STATUS_ERROR_INVALID_BUFFER);

	obj_context = CONTEXT(obj_buffer->context_id);

	ASSERT_RET(obj_buffer->buffer_store->bo 
		|| obj_buffer->buffer_store->buffer,
		VA_STATUS_ERROR_INVALID_BUFFER);
	ASSERT_RET(!(obj_buffer->buffer_store->bo 
		&& obj_buffer->buffer_store->buffer),
					VA_STATUS_ERROR_INVALID_BUFFER);

	if (obj_buffer->export_refcount > 0)
		return VA_STATUS_ERROR_INVALID_BUFFER;

	if (NULL == obj_buffer)
	{
		va_status = VA_STATUS_ERROR_INVALID_BUFFER;
		return va_status;
	}
    
	if (NULL != obj_buffer->buffer_store->bo) {
		if (NULL == obj_buffer->buffer_store->bo->plane[0].data)
			rk_error_msg("index: %d, used %d\n",
					obj_buffer->buffer_store->bo->index,
					obj_buffer->buffer_store->bo->plane[0].bytesused);

		*pbuf = obj_buffer->buffer_store->bo->plane[0].data;
		va_status = VA_STATUS_SUCCESS;

		/* FIXME support insert header directly */
		if (obj_buffer->type == VAEncCodedBufferType) {
		}

		return va_status;
	}

	if (NULL != obj_buffer->buffer_store->buffer)
	{
		*pbuf = obj_buffer->buffer_store->buffer;
		va_status = VA_STATUS_SUCCESS;
	}

	return va_status;
}

static VAStatus rockchip_UnmapBuffer(
		VADriverContextP ctx,
		VABufferID buf_id	/* in */
	)
{
	struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);
	struct object_buffer *obj_buffer = BUFFER(buf_id);
	VAStatus vaStatus = VA_STATUS_ERROR_UNKNOWN;

	if ((buf_id & OBJECT_HEAP_OFFSET_MASK) != BUFFER_ID_OFFSET)
		return VA_STATUS_ERROR_INVALID_BUFFER;
	ASSERT_RET(obj_buffer && obj_buffer->buffer_store,
			VA_STATUS_ERROR_INVALID_BUFFER);

	if (NULL != obj_buffer->buffer_store->bo) {
		/* had better do nothing here */
		vaStatus = VA_STATUS_SUCCESS;
	} else if (NULL != obj_buffer->buffer_store->buffer) {
		/* Do nothing */
		vaStatus = VA_STATUS_SUCCESS;
	}

	return vaStatus;
}

static void 
rockchip_destroy_buffer
(struct rockchip_driver_data *driver_data, object_buffer_p obj_buffer)
{
    if (NULL != obj_buffer->buffer_store)
    {
        rockchip_release_buffer_store(&obj_buffer->buffer_store);
    }

    object_heap_free( &driver_data->buffer_heap, (object_base_p) obj_buffer);
}

static VAStatus rockchip_DestroyBuffer(
		VADriverContextP ctx,
		VABufferID buffer_id
	)
{
    struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);

    struct object_buffer *obj_buffer = BUFFER(buffer_id);
    if(NULL == obj_buffer)
        return VA_STATUS_ERROR_INVALID_BUFFER;

    rockchip_destroy_buffer(rk_data, obj_buffer);

    return VA_STATUS_SUCCESS;
}

static VAStatus rockchip_BeginPicture(
		VADriverContextP ctx,
		VAContextID context,
		VASurfaceID render_target
	)
{
	struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);
	VAStatus vaStatus = VA_STATUS_SUCCESS;
	struct object_context *obj_context;
	struct object_surface *obj_surface;
	struct object_config *obj_config;

	obj_context = CONTEXT(context);
	ASSERT_RET(obj_context, VA_STATUS_ERROR_INVALID_CONTEXT);

	obj_surface = SURFACE(render_target);
	ASSERT_RET(obj_surface, VA_STATUS_ERROR_INVALID_SURFACE);

	obj_config = CONFIG(obj_context->config_id);
	ASSERT_RET(obj_config, VA_STATUS_ERROR_INVALID_CONFIG);

/* FIXME it should check it here, but not work for encoding*/
#if 0
	if (is_surface_busy(rk_data, obj_surface))
			return VA_STATUS_ERROR_SURFACE_BUSY;
#endif

	/* Encoder */
	if (CODEC_ENC == obj_context->codec_type) {
		rockchip_release_buffer_store
				(&obj_context->codec_state.encode.pic_param);
		for (uint32_t i = 0;
				i < obj_context->codec_state.encode.num_slice_params; i++) 
		{
				rockchip_release_buffer_store
						(&obj_context->codec_state.encode.slice_params[i]);
		}

		obj_context->codec_state.encode.num_slice_params = 0; 

		/* ext */
		rockchip_release_buffer_store
				(&obj_context->codec_state.encode.pic_param_ext);

		for (uint32_t i = 0; i < ARRAY_ELEMS(obj_context->codec_state.encode.packed_header_param); i++)
				rockchip_release_buffer_store
				(&obj_context->codec_state.encode.packed_header_param[i]);

		for (uint32_t i = 0; i < ARRAY_ELEMS(obj_context->codec_state.encode.packed_header_data); i++)
           		rockchip_release_buffer_store
						(&obj_context->codec_state.encode.packed_header_data[i]);

		for (uint32_t i = 0; i < obj_context->codec_state.encode.num_slice_params_ext; i++)
           		rockchip_release_buffer_store
						(&obj_context->codec_state.encode.slice_params_ext[i]);

			obj_context->codec_state.encode.num_slice_params_ext = 0;
			/*This is input new frame*/
			obj_context->codec_state.encode.current_render_target = render_target; 
			obj_context->codec_state.encode.last_packed_header_type = 0;
			memset(obj_context->codec_state.encode.slice_rawdata_index, 0,
				   sizeof(int) * obj_context->codec_state.encode.max_slice_num);
			memset(obj_context->codec_state.encode.slice_rawdata_count, 0,
				   sizeof(int) * obj_context->codec_state.encode.max_slice_num);
			memset(obj_context->codec_state.encode.slice_header_index, 0,
				   sizeof(int) * obj_context->codec_state.encode.max_slice_num);

			for (uint32_t i = 0; i < obj_context->codec_state.encode.num_packed_header_params_ext; i++)
				rockchip_release_buffer_store
						(&obj_context->codec_state.encode.packed_header_params_ext[i]);
			for (uint32_t i = 0; i < obj_context->codec_state.encode.num_packed_header_data_ext; i++)
				rockchip_release_buffer_store
						(&obj_context->codec_state.encode.packed_header_data_ext[i]);

			obj_context->codec_state.encode.num_packed_header_params_ext = 0;
			obj_context->codec_state.encode.num_packed_header_data_ext = 0;
			obj_context->codec_state.encode.slice_index = 0;
			obj_context->codec_state.encode.vps_sps_seq_index = 0;
			rockchip_release_buffer_store(&obj_context->codec_state.encode.encmb_map);

#if VA_CHECK_VERSION(0,38,0)
			if (obj_config->profile == VAProfileVP9Profile0) {
				for (uint32_t i = 0; i < ARRAY_ELEMS(obj_context->codec_state.encode.misc_param); i++)
					rockchip_release_buffer_store
							(&obj_context->codec_state.encode.misc_param[i]);

				rockchip_release_buffer_store
						(&obj_context->codec_state.encode.seq_param_ext);
			}
#endif
	}
    /* Decoder */
    if (CODEC_DEC == obj_context->codec_type) {
		/* render_target */
		obj_context->codec_state.decode.current_render_target = obj_surface->base.id;

		/* Cleanup the buffers from the last state */
		rockchip_release_buffer_store
				(&obj_context->codec_state.decode.pic_param);
		rockchip_release_buffer_store
				(&obj_context->codec_state.decode.iq_matrix);
		rockchip_release_buffer_store
				(&obj_context->codec_state.decode.bit_plane);
		rockchip_release_buffer_store
				(&obj_context->codec_state.decode.huffman_table);

		for (int32_t i = 0; i < obj_context->codec_state.decode.num_slice_params; i++) 
		{
			rockchip_release_buffer_store
					(&obj_context->codec_state.decode.slice_params[i]);
			rockchip_release_buffer_store
					(&obj_context->codec_state.decode.slice_datas[i]);
		}

		obj_context->codec_state.decode.num_slice_params = 0;
		obj_context->codec_state.decode.num_slice_datas = 0;

		/* You could do more hardware related cleanup or prepare here */
		obj_surface->bo = NULL;
    }

    return vaStatus;
}


static VAStatus rockchip_RenderPicture(
		VADriverContextP ctx,
		VAContextID context,
		VABufferID *buffers,
		int num_buffers
	)
{
    struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);

    struct object_context *obj_context;
    struct object_config *obj_config;

    VAStatus vaStatus = VA_STATUS_ERROR_UNKNOWN;

    obj_context = CONTEXT(context);
    ASSERT_RET(obj_context, VA_STATUS_ERROR_INVALID_CONTEXT);
    obj_config = CONFIG(obj_context->config_id);
    ASSERT_RET(obj_config, VA_STATUS_ERROR_INVALID_CONFIG);

    if (VAEntrypointVLD == obj_config->entrypoint) {
       vaStatus = rockchip_decoder_render_picture(ctx, context, 
		       buffers, num_buffers);
    }
	if ((VAEntrypointEncSlice == obj_config->entrypoint )
				|| (VAEntrypointEncPicture == obj_config->entrypoint))
	{
			vaStatus = rockchip_encoder_render_picture
					(ctx, context, buffers, num_buffers);
	}

    return vaStatus;
}

static VAStatus rockchip_EndPicture(
		VADriverContextP ctx,
		VAContextID context
	)
{
    struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);
    struct object_context *obj_context;
    struct object_config *obj_config;

    obj_context = CONTEXT(context);
    ASSERT(obj_context);

    obj_config = CONFIG(obj_context->config_id);

	if (CODEC_ENC == obj_context->codec_type)
	{
		ASSERT_RET(((VAEntrypointEncSlice == obj_config->entrypoint)
				|| (VAEntrypointEncPicture == obj_config->entrypoint)),
				VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT);

        if (obj_context->codec_state.encode.num_packed_header_params_ext
				!= obj_context->codec_state.encode.num_packed_header_data_ext) 
		{
            WARN_ONCE("the packed header/data is not paired for encoding!\n");
            return VA_STATUS_ERROR_INVALID_PARAMETER;
        }
        if (!(obj_context->codec_state.encode.pic_param ||
                obj_context->codec_state.encode.pic_param_ext)) {
            return VA_STATUS_ERROR_INVALID_PARAMETER;
        }
        if (!(obj_context->codec_state.encode.seq_param ||
                obj_context->codec_state.encode.seq_param_ext) &&
                (VAEntrypointEncPicture != obj_config->entrypoint)) {
            /* The seq_param is not mandatory for VP9 encoding */
            if (obj_config->profile != VAProfileVP9Profile0)
                return VA_STATUS_ERROR_INVALID_PARAMETER;
        }
        if ((obj_context->codec_state.encode.num_slice_params <=0) &&
                (obj_context->codec_state.encode.num_slice_params_ext <=0) &&
                ((obj_config->profile != VAProfileVP8Version0_3) &&
                 (obj_config->profile != VAProfileVP9Profile0))) {
            return VA_STATUS_ERROR_INVALID_PARAMETER;
        }

        if ((obj_context->codec_state.encode.packed_header_flag 
				& VA_ENC_PACKED_HEADER_SLICE) &&
            (obj_context->codec_state.encode.num_slice_params_ext
			 != obj_context->codec_state.encode.slice_index)) {
            WARN_ONCE("packed slice_header data is missing for some slice"
                      " under packed SLICE_HEADER mode\n");
            return VA_STATUS_ERROR_INVALID_PARAMETER;
        }
	}
	else if (CODEC_DEC == obj_context->codec_type)
	{
		/* Basic check here */
		if (obj_context->codec_state.decode.pic_param == NULL) {
			return VA_STATUS_ERROR_INVALID_PARAMETER;
		}
		if (obj_context->codec_state.decode.num_slice_params <=0) {
			return VA_STATUS_ERROR_INVALID_PARAMETER;
		}
		if (obj_context->codec_state.decode.num_slice_datas <=0) {
			return VA_STATUS_ERROR_INVALID_PARAMETER;
		}
		if (obj_context->codec_state.decode.num_slice_params !=
				obj_context->codec_state.decode.num_slice_datas) {
			return VA_STATUS_ERROR_INVALID_PARAMETER;
		}
    }

    /* Hardware relative code */
    ASSERT_RET(obj_context->hw_context->run, VA_STATUS_ERROR_OPERATION_FAILED);
    return obj_context->hw_context->run(ctx, obj_config->profile, 
		    &obj_context->codec_state, obj_context->hw_context);
}

static VAStatus rockchip_SyncSurface(
		VADriverContextP ctx,
		VASurfaceID render_target
	)
{
    struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    struct object_context *obj_context;
    struct object_surface *obj_surface;

    obj_context = CONTEXT(rk_data->current_context_id);
    ASSERT(obj_context);

    obj_surface = SURFACE(render_target);
    ASSERT(obj_surface);

    if (obj_context->hw_context->sync)
	    obj_context->hw_context->sync(ctx, render_target);

    /* TODO */
    return vaStatus;
}

static VAStatus rockchip_QuerySurfaceStatus(
		VADriverContextP ctx,
		VASurfaceID render_target,
		VASurfaceStatus *status	/* out */
	)
{
    struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    struct object_context *obj_context;
    struct object_surface *obj_surface;

    obj_context = CONTEXT(rk_data->current_context_id);
    ASSERT(obj_context);

    obj_surface = SURFACE(render_target);
    ASSERT(obj_surface);

    rk_info_msg("rockchip_QuerySurfaceStatus %d\n", render_target);
    /* TODO */
    if (obj_context->hw_context->get_status)
    	*status = obj_context->hw_context->get_status(ctx, render_target);
    else
		rk_info_msg("no hardware status could check %d\n", render_target);

    return vaStatus;
}

static VAStatus rockchip_PutSurface(
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
#ifdef HAVE_VA_EGL
	if (IS_VA_X11(ctx)) {
		return rockchip_x11_gles_PutSurface
			(ctx, surface, draw, srcx, srcy, srcw, srch,
			 destx, desty, destw, desth, cliprects,
			 number_cliprects, flags);
	}
#endif
#ifdef HAVE_VA_DRM
	/* FIXME */
#if 0
	if (IS_VA_DRM(ctx)) {
#else
	if (IS_VA_X11(ctx)) {
#endif
		return rockchip_drm_vop_put_surface
			(ctx, surface, draw, srcx, srcy, srcw, srch,
			 destx, desty, destw, desth, cliprects,
			 number_cliprects, flags);
	}
#endif

    return VA_STATUS_ERROR_UNIMPLEMENTED;
}

/* Not used by decoder now */
static VAStatus
rockchip_QuerySurfaceAttributes(VADriverContextP ctx,
		VAConfigID config,
		 VASurfaceAttrib *attrib_list,
		 unsigned int *num_attribs)
{
	struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);
	int i = 0;

	VAStatus vaStatus = VA_STATUS_SUCCESS;
	struct object_config *obj_config;
	VASurfaceAttrib *attribs = NULL;

	if (VA_INVALID_ID == config)
		return VA_STATUS_ERROR_INVALID_CONFIG;
	obj_config = CONFIG(config);
	if (NULL == obj_config)
		return VA_STATUS_ERROR_INVALID_CONFIG;
	if (!attrib_list && !num_attribs)
		return VA_STATUS_ERROR_INVALID_PARAMETER;
	if (NULL == attrib_list) {
		*num_attribs = ROCKCHIP_MAX_SURFACE_ATTRIBUTES;
		return VA_STATUS_SUCCESS;
	}

	attribs = malloc(ROCKCHIP_MAX_SURFACE_ATTRIBUTES *sizeof(*attribs));

	if (NULL == attribs)
		return VA_STATUS_ERROR_ALLOCATION_FAILED;
#if 0
	attribs[i].type = VASurfaceAttribMaxWidth;
	attribs[i].value.type = VAGenericValueTypeInteger;
	attribs[i].flags = VA_SURFACE_ATTRIB_GETTABLE;
	attribs[i].value.value.i = max_width;
	i++;

	attribs[i].type = VASurfaceAttribMaxHeight;
	attribs[i].value.type = VAGenericValueTypeInteger;
	attribs[i].flags = VA_SURFACE_ATTRIB_GETTABLE;
	attribs[i].value.value.i = max_height;
	i++;
#endif

	if (i > *num_attribs) {
		*num_attribs = i;
		free(attribs);
		return VA_STATUS_ERROR_MAX_NUM_EXCEEDED;
	}

	*num_attribs = i;
	memcpy(attrib_list, attribs, i * sizeof(*attribs));
	free(attribs);

	return vaStatus;
}

/* 
 * Query display attributes 
 * The caller must provide a "attr_list" array that can hold at
 * least vaMaxNumDisplayAttributes() entries. The actual number of attributes
 * returned in "attr_list" is returned in "num_attributes".
 */
static VAStatus rockchip_QueryDisplayAttributes (
		VADriverContextP ctx,
		VADisplayAttribute *attr_list,	/* out */
		int *num_attributes		/* out */
	)
{
	const int num_attribs = ARRAY_ELEMS(rockchip_display_attributes);

	if (attr_list && num_attribs > 0)
		memcpy(attr_list, rockchip_display_attributes,
				sizeof(rockchip_display_attributes));
	if (num_attributes)
		*num_attributes = num_attribs;

	return VA_STATUS_SUCCESS;
}

/* 
 * Get display attributes 
 * This function returns the current attribute values in "attr_list".
 * Only attributes returned with VA_DISPLAY_ATTRIB_GETTABLE set in the "flags" field
 * from vaQueryDisplayAttributes() can have their values retrieved.  
 */
static VAStatus rockchip_GetDisplayAttributes (
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
static VAStatus rockchip_SetDisplayAttributes (
		VADriverContextP ctx,
		VADisplayAttribute *attr_list,
		int num_attributes
	)
{
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}


static VAStatus rockchip_BufferInfo(
        VADriverContextP ctx,
        VABufferID buf_id,	/* in */
        VABufferType *type,	/* out */
        unsigned int *size,    	/* out */
        unsigned int *num_elements /* out */
    )
{
	struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);
	struct object_buffer *obj_buffer = NULL;

	obj_buffer = BUFFER(buf_id);

	ASSERT_RET(obj_buffer, VA_STATUS_ERROR_INVALID_BUFFER);

	*type = obj_buffer->type;
	*size = obj_buffer->size_element;
	*num_elements = obj_buffer->num_elements;

	return VA_STATUS_SUCCESS;
}

/** Acquires buffer handle for external API usage */
static VAStatus
rockchip_AcquireBufferHandle(VADriverContextP ctx, VABufferID buf_id,
		VABufferInfo *out_buf_info)
{
	struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);
	struct object_buffer * const obj_buffer = BUFFER(buf_id);

	if (NULL == obj_buffer || (0 > obj_buffer))
		return VA_STATUS_ERROR_INVALID_BUFFER;
	/* XXX: only VA surface|image like buffers are supported for now */
	if (obj_buffer->type != VAImageBufferType)
		return VA_STATUS_ERROR_UNSUPPORTED_BUFFERTYPE;
	if (NULL != out_buf_info)
		if (VA_SURFACE_ATTRIB_MEM_TYPE_V4L2 != out_buf_info->mem_type)
		return VA_STATUS_ERROR_UNSUPPORTED_MEMORY_TYPE;

	/* FIXME Synchronization */
	VABufferInfo * const buf_info = &obj_buffer->export_state;

	buf_info->handle =
		(intptr_t)obj_buffer->buffer_store->bo->plane[0].dma_fd;
	buf_info->type = obj_buffer->type;
	buf_info->mem_size =
		obj_buffer->num_elements * obj_buffer->size_element;

	obj_buffer->export_refcount++;

	*out_buf_info = obj_buffer->export_state;

	return VA_STATUS_SUCCESS;
}

/** Releases buffer handle after usage from external API */
static VAStatus
rockchip_ReleaseBufferHandle(VADriverContextP ctx, VABufferID buf_id)
{

	struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);
	struct object_buffer * const obj_buffer = BUFFER(buf_id);

	if (!obj_buffer)
		return VA_STATUS_ERROR_INVALID_BUFFER;

	if (obj_buffer->export_refcount == 0)
		return VA_STATUS_ERROR_INVALID_BUFFER;

	if (--obj_buffer->export_refcount == 0) {
		VABufferInfo * const buf_info = &obj_buffer->export_state;

		/* Do some harware relationed cleanup jobs here */
		buf_info->mem_type = 0;
	}

	return VA_STATUS_SUCCESS;
}

static VAStatus rockchip_LockSurface(
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

static VAStatus rockchip_UnlockSurface(
		VADriverContextP ctx,
		VASurfaceID surface
	)
{
    /* TODO */
    return VA_STATUS_ERROR_UNIMPLEMENTED;
}

static void rockchip_driver_data_terminate(VADriverContextP ctx)
{
    struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);
    struct object_buffer *obj_buffer;
    struct object_config *obj_config;
    object_heap_iterator iter;

    /* Clean up left over buffers */
    obj_buffer = (struct object_buffer *)
	    object_heap_first(&rk_data->buffer_heap, &iter);
    while (obj_buffer)
    {
        rk_info_msg("vaTerminate: bufferID %08x still allocated, destroying\n", 
		obj_buffer->base.id);
        rockchip_destroy_buffer(rk_data, obj_buffer);
        obj_buffer = (object_buffer_p) object_heap_next
		(&rk_data->buffer_heap, &iter);
    }
    object_heap_destroy(&rk_data->buffer_heap);

    /* TODO cleanup */
    object_heap_destroy(&rk_data->surface_heap);

    /* TODO cleanup */
    object_heap_destroy(&rk_data->context_heap);

    /* Clean up configIDs */
    obj_config = (struct object_config *)
	    object_heap_first(&rk_data->config_heap, &iter);
    while (obj_config)
    {
        object_heap_free
		(&rk_data->config_heap, (struct object_base *)obj_config);
        obj_config = (struct object_config *)
		object_heap_next(&rk_data->config_heap, &iter);
    }
    object_heap_destroy(&rk_data->config_heap);

    return VA_STATUS_SUCCESS;
}

static bool
rockchip_driver_data_init(VADriverContextP ctx)
{
	struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);

	/* FIXME using device id instead */
	rk_data->codec_info = rk_get_codec_info(3288);

	if (NULL == rk_data->codec_info)
		return false;

	if (object_heap_init(&rk_data->config_heap, 
		sizeof(struct object_config), CONFIG_ID_OFFSET))
	    goto err_config_heap;

	if (object_heap_init(&rk_data->context_heap, 
		sizeof(struct object_context), CONTEXT_ID_OFFSET))
	    goto err_context_heap;

	if (object_heap_init(&rk_data->surface_heap, 
		sizeof(struct object_surface), SURFACE_ID_OFFSET))
	    goto err_surface_heap;

	if (object_heap_init(&rk_data->buffer_heap, 
		sizeof(struct object_buffer), BUFFER_ID_OFFSET))
	    goto err_buffer_heap;

	if (object_heap_init(&rk_data->image_heap, 
		sizeof(struct object_image), IMAGE_ID_OFFSET))
	    goto err_image_heap;

	return true;

err_image_heap:
	object_heap_destroy(&rk_data->buffer_heap);
err_buffer_heap:
	object_heap_destroy(&rk_data->surface_heap);
err_surface_heap:
	object_heap_destroy(&rk_data->context_heap);
err_context_heap:
	object_heap_destroy(&rk_data->config_heap);
err_config_heap:

	return false;
}

struct {
	bool (*init)(VADriverContextP ctx);
	void (*terminate)(VADriverContextP ctx);
	int display_type;
} rockchip_sub_ops[] = {
	{
		rockchip_driver_data_init,
		rockchip_driver_data_terminate,
		0,
	},
#ifdef HAVE_VA_X11
	{
		rockchip_x11_gles_init,
		rockchip_x11_gles_destory,
		VA_DISPLAY_X11,
	},
#endif
#ifdef HAVE_VA_DRM
	{
		rockchip_drm_output_init,
		rockchip_drm_output_destroy,
		/* FIXME VA_DISPLAY_DRM, */
		0,
	},
#endif
};

static VAStatus
rockchip_init(VADriverContextP ctx)
{
	struct rockchip_driver_data *rk = rockchip_driver_data(ctx);

	uint32_t i;

	for (i = 0; i < ARRAY_ELEMS(rockchip_sub_ops); i++) 
	{
		if ((rockchip_sub_ops[i].display_type == 0 ||
		rockchip_sub_ops[i].display_type == 
		(ctx->display_type & VA_DISPLAY_MAJOR_MASK)) &&
		!rockchip_sub_ops[i].init(ctx))
			break;
	}

	if (i == ARRAY_ELEMS(rockchip_sub_ops)) {
		rk->current_context_id = VA_INVALID_ID;

		if (rk->codec_info && rk->codec_info->preinit_hw_codec)
			rk->codec_info->preinit_hw_codec(ctx, rk->codec_info);

		return VA_STATUS_SUCCESS;
	}
	else {
		i--;
		for (; i >= 0; i--) {
			if (rockchip_sub_ops[i].display_type == 0 ||
			rockchip_sub_ops[i].display_type == 
			(ctx->display_type & VA_DISPLAY_MAJOR_MASK))
			{
				rockchip_sub_ops[i].terminate(ctx);
			}

		}

		return VA_STATUS_ERROR_UNKNOWN;
	}
}

static VAStatus rockchip_terminate(VADriverContextP ctx)
{
    struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);

    if (rk_data) {
		for (uint32_t i = ARRAY_ELEMS(rockchip_sub_ops); i > 0; i--) 
		{
			if (rockchip_sub_ops[i -1].display_type == 0 
				|| rockchip_sub_ops[i].display_type 
				== (ctx->display_type & VA_DISPLAY_MAJOR_MASK))
			{
				rockchip_sub_ops[i].terminate(ctx);
			}

		}
		free(rk_data);
		ctx->pDriverData = NULL;
    }
    return VA_STATUS_SUCCESS;
}

VAStatus VA_DRIVER_INIT_FUNC(  VADriverContextP ctx )
{
    struct VADriverVTable * const vtable = ctx->vtable;
    int result;
    struct rockchip_driver_data *rk_data;

    ctx->version_major = VA_MAJOR_VERSION;
    ctx->version_minor = VA_MINOR_VERSION;
    ctx->max_profiles = ROCKCHIP_MAX_PROFILES;
    ctx->max_entrypoints = ROCKCHIP_MAX_ENTRYPOINTS;
    ctx->max_attributes = ROCKCHIP_MAX_CONFIG_ATTRIBUTES;
    ctx->max_image_formats = ROCKCHIP_MAX_IMAGE_FORMATS;
    ctx->max_subpic_formats = ROCKCHIP_MAX_SUBPIC_FORMATS;
    ctx->max_display_attributes = ROCKCHIP_MAX_DISPLAY_ATTRIBUTES;

    vtable->vaTerminate = rockchip_terminate;
    vtable->vaQueryConfigEntrypoints = rockchip_QueryConfigEntrypoints;
    vtable->vaQueryConfigProfiles = rockchip_QueryConfigProfiles;
    vtable->vaQueryConfigAttributes = rockchip_QueryConfigAttributes;
    vtable->vaCreateConfig = rockchip_CreateConfig;
    vtable->vaDestroyConfig = rockchip_DestroyConfig;
    vtable->vaGetConfigAttributes = rockchip_GetConfigAttributes;
    vtable->vaCreateSurfaces = rockchip_CreateSurfaces;
    vtable->vaDestroySurfaces = rockchip_DestroySurfaces;
    vtable->vaCreateSurfaces2 = rockchip_CreateSurfaces2;
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
    vtable->vaQuerySurfaceAttributes = rockchip_QuerySurfaceAttributes;
    /* TODO */

#if VA_CHECK_VERSION(0,36,0)
    vtable->vaAcquireBufferHandle = rockchip_AcquireBufferHandle;
    vtable->vaReleaseBufferHandle = rockchip_ReleaseBufferHandle;
#endif

    rk_data = (struct rockchip_driver_data *) malloc(sizeof(*rk_data) );
    if (NULL == rk_data) {
        ctx->pDriverData = NULL;

        return VA_STATUS_ERROR_ALLOCATION_FAILED;
    }
    rk_data->x11_backend = NULL;
    ctx->pDriverData = (void *) rk_data;

    result = rockchip_init(ctx);

    if (VA_STATUS_SUCCESS == result) {
        ctx->str_vendor = ROCKCHIP_STR_VENDOR;
    }
    else {
        free(rk_data);
	ctx->pDriverData = NULL;
   }

    return VA_STATUS_SUCCESS;
}
