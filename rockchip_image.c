#include "rockchip_image.h"

static inline void
memcpy_pic(uint8_t * dst, uint32_t dst_stride,
	   const uint8_t * src, uint32_t src_stride,
	   uint32_t len, uint32_t height)
{
	ASSERT(NULL != dst);
	ASSERT(NULL != src);
	for (uint32_t i = 0; i < height; i++) {
		memcpy(dst, src, len);
		dst += dst_stride;
		src += src_stride;
	}
}

VAStatus
get_image_i420_sw(struct object_image *obj_image, uint8_t * image_data,
	       struct object_surface *obj_surface,
	       const VARectangle * rect)
{
	uint8_t *dst[3], *src[3];
	const int Y = 0;
	const int U =
	    obj_image->image.format.fourcc == obj_surface->fourcc ? 1 : 2;
	const int V =
	    obj_image->image.format.fourcc == obj_surface->fourcc ? 2 : 1;

	VAStatus va_status = VA_STATUS_SUCCESS;

	ASSERT_RET(obj_surface->fourcc, VA_STATUS_ERROR_INVALID_SURFACE);

	/* Dest VA image has either I420 or YV12 format.
	   Source VA surface alway has I420 format */
	dst[Y] = image_data + obj_image->image.offsets[Y];
	src[0] = (uint8_t *) obj_surface->bo->plane[0].data;
	dst[U] = image_data + obj_image->image.offsets[U];
	src[1] = src[0] + obj_surface->width * obj_surface->height;
	dst[V] = image_data + obj_image->image.offsets[V];
	src[2] = src[1] + 
		(obj_surface->width / 2) * (obj_surface->height / 2);
	/* Y plane */
	dst[Y] += rect->y * obj_image->image.pitches[Y] + rect->x;
	src[0] += rect->y * obj_surface->width + rect->x;
	memcpy_pic(dst[Y], obj_image->image.pitches[Y],
		   src[0], obj_surface->width, rect->width, rect->height);

	/* U plane */
	dst[U] +=
	    (rect->y / 2) * obj_image->image.pitches[U] + rect->x / 2;
	src[1] += (rect->y / 2) * obj_surface->orig_width / 2 + rect->x / 2;
	memcpy_pic(dst[U], obj_image->image.pitches[U],
		   src[1], obj_surface->width / 2,
		   rect->width / 2, rect->height / 2);

	/* V plane */
	dst[V] +=
	    (rect->y / 2) * obj_image->image.pitches[V] + rect->x / 2;
	src[2] += (rect->y / 2) * obj_surface->width / 2 + rect->x / 2;
	memcpy_pic(dst[V], obj_image->image.pitches[V],
		   src[2], obj_surface->width / 2,
		   rect->width / 2, rect->height / 2);

	return va_status;
}

VAStatus
get_image_nv12_sw(struct object_image *obj_image, uint8_t * image_data,
	       struct object_surface *obj_surface,
	       const VARectangle * rect)
{
	uint8_t *dst[2], *src[2];
	VAStatus va_status = VA_STATUS_SUCCESS;

	if (!obj_surface)
		return VA_STATUS_ERROR_INVALID_SURFACE;

	assert(obj_surface->fourcc);

	/* Both dest VA image and source surface have NV12 format */
	dst[0] = image_data + obj_image->image.offsets[0];
	src[0] = (uint8_t *) obj_surface->bo->plane[0].data;
	dst[1] = image_data + obj_image->image.offsets[1];
	src[1] = src[0] + obj_surface->width * obj_surface->height;

	/* Y plane */
	dst[0] += rect->y * obj_image->image.pitches[0] + rect->x;
	src[0] += rect->y * obj_surface->width + rect->x;
	memcpy_pic(dst[0], obj_image->image.pitches[0],
		   src[0], obj_surface->width, rect->width, rect->height);

	/* UV plane */
	dst[1] +=
	    (rect->y / 2) * obj_image->image.pitches[1] + (rect->x & -2);
	src[1] += (rect->y / 2) * obj_surface->width + (rect->x & -2);
	memcpy_pic(dst[1], obj_image->image.pitches[1],
		   src[1], obj_surface->width,
		   rect->width, rect->height / 2);

	return va_status;
}
