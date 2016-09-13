#ifndef _V4L2_MEMORY_H_
#define _V4L2_MEMORY_H_
#include "common.h"

#define RK_VIDEO_MAX_PLANES 3

enum {
	BUFFER_FREE,
	BUFFER_ENQUEUED,
	BUFFER_DEQUEUED,
};

struct rk_v4l2_buffer {
        struct {
		int32_t length;
		int32_t bytesused;
		void *data;
		int32_t dma_fd;
	} plane[RK_VIDEO_MAX_PLANES];
	int32_t index;
	int32_t state;
};

#endif
