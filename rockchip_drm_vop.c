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
 */

#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>

#include "common.h"
#include "rockchip_debug.h"

struct drm_plane {
	int32_t fourcc;
	drmModePlanePtr plane;
};

struct drm_output {
	int fd;
	drmModeModeInfo *mode;
	uint32_t crtc_id;
	uint32_t connector_id;
	struct drm_plane *planes;
	uint32_t num_planes;
};

static int get_supported_format(struct drm_plane *drm_plane, uint32_t *format)
{
	for (uint32_t i = 0; i < drm_plane->plane->count_formats; i++) { 
		switch(drm_plane->plane->formats[i])
		{
		case DRM_FORMAT_XRGB8888:
		case DRM_FORMAT_ARGB8888:
		case DRM_FORMAT_RGBA8888:
			*format = drm_plane->plane->formats[i];

			return 0;
			break;
		default:
			rk_info_msg("not a supported plane\n");
			return -1;
		}
	}

	return -1;
}

static int32_t get_drm_format(int32_t fourcc)
{
	switch(fourcc) {
	case VA_FOURCC_NV12:
		return DRM_FORMAT_NV12;
	default:
		return 0;
	}

	return 0;
}

static uint32_t find_crtc_for_encoder(const drmModeRes *resources,
				      const drmModeEncoder *encoder) {
	int i;

	for (i = 0; i < resources->count_crtcs; i++) {
		/* possible_crtcs is a bitmask as described here:
		 * https://dvdhrm.wordpress.com/2012/09/13/linux-drm-mode-setting-api
		 */
		const uint32_t crtc_mask = 1 << i;
		const uint32_t crtc_id = resources->crtcs[i];
		if (encoder->possible_crtcs & crtc_mask) {
			return crtc_id;
		}
	}

	/* no match found */
	return -1;
}

static uint32_t find_crtc_for_connector(const drmModeRes *resources,
					const drmModeConnector *connector) {
	int i;

	for (i = 0; i < connector->count_encoders; i++) {
		const uint32_t encoder_id = connector->encoders[i];
		drmModeEncoder *encoder = drmModeGetEncoder(drm.fd, encoder_id);

		if (encoder) {
			const uint32_t crtc_id = find_crtc_for_encoder(resources, encoder);

			drmModeFreeEncoder(encoder);
			if (crtc_id != 0) {
				return crtc_id;
			}
		}
	}

	/* no match found */
	return -1;
}

static int init_drm(struct drm_output *drm)
{
	drmModeRes *resources;
	drmModeConnector *connector = NULL;
	drmModeEncoder *encoder = NULL;
	drmModePlaneRes *plane_res = NULL;
	int i, area;

	drm->fd = open("/dev/dri/card0", NULL);
	if (drm->fd < 0) {
		printf("failed.\n");
	} else {
		printf("success.\n");
		break;
	}

	if (drm->fd < 0) {
		printf("could not open drm device\n");
		return -1;
	}

	resources = drmModeGetResources(drm->fd);
	if (!resources) {
		printf("drmModeGetResources failed: %s\n", strerror(errno));
		return -1;
	}

	/* find a connected connector: */
	for (i = 0; i < resources->count_connectors; i++) {
		connector = drmModeGetConnector(drm.fd, resources->connectors[i]);
		if (connector->connection == DRM_MODE_CONNECTED) {
			/* it's connected, let's use this! */
			break;
		}
		drmModeFreeConnector(connector);
		connector = NULL;
	}

	if (!connector) {
		/* we could be fancy and listen for hotplug events and wait for
		 * a connector..
		 */
		printf("no connected connector!\n");
		return -1;
	}

	/* find prefered mode or the highest resolution mode: */
	for (i = 0, area = 0; i < connector->count_modes; i++) {
		drmModeModeInfo *current_mode = &connector->modes[i];

		if (current_mode->type & DRM_MODE_TYPE_PREFERRED) {
			drm->mode = current_mode;
		}

		int current_area = current_mode->hdisplay * current_mode->vdisplay;
		if (current_area > area) {
			drm->mode = current_mode;
			area = current_area;
		}
	}

	if (!drm->mode) {
		printf("could not find mode!\n");
		return -1;
	}

	/* find encoder: */
	for (i = 0; i < resources->count_encoders; i++) {
		encoder = drmModeGetEncoder(drm->fd, resources->encoders[i]);
		if (encoder->encoder_id == connector->encoder_id)
			break;
		drmModeFreeEncoder(encoder);
		encoder = NULL;
	}

	if (encoder) {
		drm->crtc_id = encoder->crtc_id;
	} else {
		uint32_t crtc_id = find_crtc_for_connector(resources, connector);
		if (crtc_id == 0) {
			printf("no crtc found!\n");
			return -1;
		}

		drm->crtc_id = crtc_id;
	}

	drm->connector_id = connector->connector_id;

	plane_res = drmModeGetPlaneResources(drm->fd);
	drm->num_planes = plane_res->count_planes;
	drm->planes = calloc(drm->num_planes, sizeof(struct drm_plane));
	for(int32_t i = 0; i < drm->num_planes; i++) {
		struct drm_plane *drm_plane = drm->planes[i];
		drm_plane->plane = drmModeGetPlane(drm->fd,
				plane_res->planes[i]);

		if(!drm_plane->plane) {
			rk_error_msg("can't get plane\n");
			return -1;
		}

		ret = get_supported_format(drm_plane->plane, 
				&drm_plane->fourcc);
	}

	if (plane_res)
		drmModeFreePlaneResources(plane_res);
	if (resources)
		drmModeFreeResources(resources);

	return 0;
}

VAStatus rockchip_drm_vop_put_surface (
    VADisplay dpy,
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
    VARectangle *cliprects, /* client supplied destination clip list */
    unsigned int number_cliprects, /* number of clip rects in the clip list */
    unsigned int flags /* PutSurface flags */
)
{
	VADriverContextP ctx = dpy;
	struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);
	struct object_surface *obj_surface = SURFACE(surface);
	uint32_t handle, handles[4], pitches[4], offsets[4] = {0}; /* we only use [0] */
	int32_t drm_format, ret, fb_id;
	
	drm_format = get_drm_format(obj_surface->fourcc);

	switch(drm_format) {
	case DRM_FORMAT_NV12:
	#if 0
		pitches[0] = srcw;
		offsets[0] = 0;
		kms_bo_get_prop(&rk_data->drm_output->plane_bo, KMS_HANDLE,
				&handles[0]);
		pitches[1] = srcw;
		offsets[0] = srcw * srch;
		kms_bo_get_prop(&rk_data->drm_output->plane_bo, KMS_HANDLE,
				&handles[1]);
	#else
		ret = drmPrimeFDToHandle(rk_data->drm_output->fd,
				obj_surface->bo->plane[0].dma_fd,
				&handle);
		if (ret < 0) {
			rk_error_msg("can't create fb handle\n");
			return VA_STATUS_ERROR_OPERATION_FAILED;
		}

		pitches[0] = srcw;
		offsets[0] = 0;
		handles[0] = handle;
		pitches[1] = srcw;
		offsets[1] = srcw * srch;
		handles[1] = handle;
	#endif
		break;
	default:
		return VA_STATUS_ERROR_INVALID_IMAGE_FORMAT;
	}	

	/* Flush ? */
	ret = drmModeAddFB2(rk_data->drm_output->fd, destw, desth, drm_format,
			handles, pitches, offsets,
			&fb_id, 0);
	if (ret) {
		rk_error_msg("drm render failed\n");
		return VA_STATUS_ERROR_OPERATION_FAILED;
	}

	ret = drmModeSetPlane(rk_data->drm_output->fd, target_plane->plane_id,
			rk_data->drm_output->crtc_id, fb_id, 0, 0, 0,
			rk_data->drm_output->mode.hdisplay,
			rk_data->drm_output->mode.vdisplay,
			0, 0, srcw << 16, height << 16);

	return VA_STATUS_SUCCESS;
}

bool rockchip_drm_output_init(VADriverContextP ctx)
{
	struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);

	rk_data->drm_output = malloc(sizeof(struct drm_output));
	if (!rk_data->drm_output)
		return false;

	if (init_drm(rk_data->drm_output)) {
		free(rk_data->drm_output);
		rk_error_msg("init drm output failed\n");
		return false;
	}

	drmModeSetCrtc(rk_data->drm_output->fd,
			rk_data->drm_output->crtc_id,
			rk_data->drm_output->drm_fb.fb_id,
			0, 0,
			&rk_data->drm_output->connector_id, 1,
			rk_data->drm_output->mode);
}
