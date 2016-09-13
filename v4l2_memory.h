#ifndef _V4L2_MEMORY_H_
#define _V4L2_MEMORY_H_
#include <stdio.h>
#include <stdint.h>

#define RK_VIDEO_MAX_PLANES 3

enum {
	BUFFER_FREE,
	BUFFER_ENQUEUED,
	BUFFER_DEQUEUED,
};

struct rk_v4l2_buffer {
        struct {
		uint32_t length;
		uint32_t bytesused;
		void *data;
		int32_t dma_fd;
	} plane[RK_VIDEO_MAX_PLANES];
	uint32_t index;
	int32_t state;
	uint32_t length;
};

void v4l2_bo_reference(struct rk_v4l2_buffer *bo);

void v4l2_bo_unreference(struct rk_v4l2_buffer *bo);

#endif
