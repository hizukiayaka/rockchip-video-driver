#ifndef _ROCKCHIP_IMAGE_H_
#define _ROCKCHIP_IMAGE_H_
#include "common.h"
#include "rockchip_driver.h"
VAStatus
get_image_i420_sw(struct object_image *obj_image, uint8_t * image_data,
	       struct object_surface *obj_surface,
	       const VARectangle * rect);
VAStatus
get_image_nv12_sw(struct object_image *obj_image, uint8_t * image_data,
	       struct object_surface *obj_surface,
	       const VARectangle * rect);

#endif
