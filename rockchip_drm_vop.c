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
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "common.h"
#include "rockchip_drm_vop.h"
#include "rockchip_debug.h"

struct drm_plane {
	int32_t fourcc;
	drmModePlanePtr plane;
};

struct drm_output {
	int32_t fd;
	int32_t ctrl_fd;
	uint32_t crtc_id;
	uint32_t connector_id;
	drmModeModeInfo *mode;
	struct drm_plane *drm_planes;
	uint32_t num_planes;
	struct drm_plane *target_plane;
};

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

static bool is_supported_format(struct drm_plane *drm_plane, uint32_t format)
{
	for (uint32_t i = 0; i < drm_plane->plane->count_formats; i++) {
		if (format == drm_plane->plane->formats[i])
			return true;
	}
	return false;
}

static uint32_t find_crtc_for_encoder(const drmModeRes *resources,
				      const drmModeEncoder *encoder) {
	for (uint32_t i = 0; i < resources->count_crtcs; i++) {
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

static uint32_t find_crtc_for_connector(struct drm_output *drm,
		const drmModeRes *resources,
		const drmModeConnector *connector) 
{
	for (uint32_t i = 0; i < connector->count_encoders; i++) {
		const uint32_t encoder_id = connector->encoders[i];
		drmModeEncoder *encoder = drmModeGetEncoder(drm->fd, encoder_id);

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
	drmModeCrtcPtr c;
	int32_t encoder_index, count_planes;

	drm->target_plane = NULL;

	drm->fd = open("/dev/dri/card0", O_RDWR);
	if (drm->fd < 0) {
		rk_error_msg("could not open drm device\n");
		return -1;
	}

	drm->ctrl_fd = open("/dev/dri/controlD64", O_RDWR);
	if (drm->ctrl_fd < 0) {
		rk_error_msg("could not open drm controller\n");
		return -1;
	}

	resources = drmModeGetResources(drm->fd);
	if (!resources) {
		printf("drmModeGetResources failed: %s\n", strerror(errno));
		return -1;
	}

	/* find a connected connector: */
	for (uint32_t i = 0; i < resources->count_connectors; i++) {
		connector = drmModeGetConnector(drm->fd, resources->connectors[i]);
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

	/*
	 * The mode selected here seems not match the screen, I have to use
	 * the mode from ctrc_id.
	 */
#if 0
	/* find prefered mode or the highest resolution mode: */
	for (uint32_t i = 0, area = 0; i < connector->count_modes; i++) {
		drmModeModeInfo *current_mode = &connector->modes[i];

		if (current_mode->type & DRM_MODE_TYPE_PREFERRED) {
			drm->mode = current_mode;
		}

		int32_t current_area = current_mode->hdisplay * current_mode->vdisplay;
		if (current_area > area) {
			drm->mode = current_mode;
			area = current_area;
		}
	}

	if (!drm->mode) {
		printf("could not find mode!\n");
		return -1;
	}
#endif

	/* find encoder: */
	for (uint32_t i = 0; i < resources->count_encoders; i++) {
		encoder = drmModeGetEncoder(drm->fd, resources->encoders[i]);
		if (encoder->encoder_id == connector->encoder_id) {
			encoder_index = i;
			break;
		}
		drmModeFreeEncoder(encoder);
		encoder = NULL;
	}

	if (encoder) {
		drm->crtc_id = encoder->crtc_id;
	} else {
		uint32_t crtc_id = find_crtc_for_connector
			(drm, resources, connector);
		if (crtc_id == 0) {
			printf("no crtc found!\n");
			return -1;
		}

		drm->crtc_id = crtc_id;
	}

	c = drmModeGetCrtc(drm->fd, drm->crtc_id);
	if (c && c->mode_valid) {
		/* FIXME Currently the contents of modeInfo would be incorrect */
		drm->mode = &c->mode;
	}
	else {
		rk_error_msg("no ctrl mode valid\n");
		return -1;
	}

	drmModeFreeCrtc(c);

	drm->connector_id = connector->connector_id;

	drmSetClientCap(drm->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

	plane_res = drmModeGetPlaneResources(drm->fd);
	drm->num_planes = 0;
	count_planes = plane_res->count_planes;
	drm->drm_planes = calloc(count_planes, sizeof(struct drm_plane));

	for(int32_t i = 0; i < count_planes; i++) {
		struct drm_plane *drm_plane = &drm->drm_planes[drm->num_planes];

		drmModePlanePtr p = drmModeGetPlane(drm->fd,
				plane_res->planes[i]);

		if (p && p->possible_crtcs == encoder_index) {
			drm_plane->plane = p;
			drm->num_planes++;
		} else {
			drmModeFreePlane(p);
		}
	}
	if (!drm->num_planes) {
		rk_error_msg("can't get a plane\n");
		goto err;
	}

	if (plane_res)
		drmModeFreePlaneResources(plane_res);
	if (resources)
		drmModeFreeResources(resources);
	return 0;
err:
	if (plane_res)
		drmModeFreePlaneResources(plane_res);
	if (resources)
		drmModeFreeResources(resources);

	return -1;
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
	struct drm_output *drm_output = rk_data->drm_output;
	static int32_t last_fb_id = 0;
#if 1
	drmModeCrtcPtr c;
#endif

	ASSERT_RET(drm_output, VA_STATUS_ERROR_INVALID_DISPLAY);
	
	drm_format = get_drm_format(obj_surface->fourcc);

	switch(drm_format) {
	case DRM_FORMAT_NV12:
		if (NULL == drm_output->target_plane)
		{
			for (uint32_t i = 0; i < drm_output->num_planes; i++)
			{
				if (is_supported_format
					(&drm_output->drm_planes[i],
					 DRM_FORMAT_NV12))
					drm_output->target_plane =
						&drm_output->drm_planes[i];
			}
		}
		ret = drmPrimeFDToHandle(drm_output->fd,
				obj_surface->bo->plane[0].dma_fd,
				&handle);
		if (ret < 0) {
			rk_error_msg("can't create fb handle %s\n",
					strerror(ret));
			return VA_STATUS_ERROR_OPERATION_FAILED;
		}

		pitches[0] = obj_surface->width;
		offsets[0] = 0;
		handles[0] = handle;
		pitches[1] = obj_surface->width;
		offsets[1] = obj_surface->width * obj_surface->height;
		handles[1] = handle;
		break;
	default:
		return VA_STATUS_ERROR_INVALID_IMAGE_FORMAT;
	}	

	/* Flush ? */
	ret = drmModeAddFB2(drm_output->fd, destw, desth, drm_format,
			handles, pitches, offsets,
			&fb_id, 0);
	if (ret) {
		rk_error_msg("add framebuffer failed %d\n", ret);
		return VA_STATUS_ERROR_OPERATION_FAILED;
	}

#if 1
	c = drmModeGetCrtc(drm_output->fd, drm_output->crtc_id);

	ret = drmModeSetPlane(drm_output->ctrl_fd,
			drm_output->target_plane->plane->plane_id,
			drm_output->crtc_id, fb_id, 0, destx, desty,
			c->width, c->height,
			srcx, srcy, destw << 16, desth << 16);
	if (ret) {
		rk_error_msg("set plane failed %d\n", ret);
	}
#else
	ret = drmModeSetPlane(drm_output->ctrl_fd,
			drm_output->target_plane->plane->plane_id,
			drm_output->crtc_id, fb_id, 0, destx, desty,
			drm_output->mode->hdisplay, drm_output->mode->vdisplay,
			srcx, srcy, destw << 16, desth << 16);
	if (ret) {
		rk_error_msg("set plane failed %d\n", ret);
	}

#endif
	if (last_fb_id) {
		drmModeRmFB(drm_output->ctrl_fd, last_fb_id);
	}
	last_fb_id = fb_id;
#if 1
	drmModeFreeCrtc(c);
#endif

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

	return true;
}

void rockchip_drm_output_destroy(VADriverContextP ctx)
{
	struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);
	struct drm_output *drm = rk_data->drm_output;

	if (drm->fd < 0) {
		close(drm->fd);
	}
	if (drm->ctrl_fd < 0) {
		close(drm->ctrl_fd);
	}

	for(int32_t i = 0; i < drm->num_planes; i++) {
		struct drm_plane *drm_plane = &drm->drm_planes[i];

		drmModeFreePlane(drm_plane->plane);
	}
	drmModeFreeModeInfo(drm->mode);

	free(drm);
	rk_data->drm_output = NULL;
}
