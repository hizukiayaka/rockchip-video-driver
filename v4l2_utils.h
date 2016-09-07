#ifndef _V4L2_UTILS_H_
#define _V4L2_UTILS_H_
#include "common.h"
#include <linux/videodev2.h>

#define RK_VIDEO_MAX_PLANES 3
#define MAX_CODEC_BUFFER (4 * 1024 * 1024)

#define NUM_DEC_INPUT_PLANES  1
#define NUM_DEC_OUTPUT_PLANES 1

enum {
	BUFFER_FREE,
	BUFFER_ENQUEUED,
	BUFFER_DEQUEUED,
};

struct rk_v4l2_buffer {
        struct {
		int length;
		int bytesused;
		void *data;
		int32_t dma_fd;
	} plane[RK_VIDEO_MAX_PLANES];
	int index;
	int state;
};

struct rk_v4l2_ops {
	int32_t(*input_alloc) (void *, uint32_t);
	int32_t(*output_alloc) (void *, uint32_t);
	int32_t(*set_codec) (void *, int32_t); 
	int32_t(*set_format) (void *, int32_t); 
	int32_t(*qbuf_input) 
		(void *, struct rk_v4l2_buffer *);
	int32_t(*qbuf_output) 
		(void *, struct rk_v4l2_buffer *);
	int32_t(*dqbuf_input) 
		(void *, struct rk_v4l2_buffer *);
	int32_t(*dqbuf_output) 
		(void *, struct rk_v4l2_buffer *);
};

struct rk_v4l2_object {
	int32_t video_fd;

	int32_t num_input_buffers;
	int32_t num_output_buffers;;
	struct rk_v4l2_buffer *input_buffer;
	struct rk_v4l2_buffer *output_buffer;

	bool input_streamon, output_streamon;
	bool is_encoder;

	struct {
		int w;
		int h;
	} input_size;

	struct rk_v4l2_ops ops;

	struct v4l2_format input_format;
	struct v4l2_format output_format;
	int32_t has_free_input_buffers;
	int32_t has_free_output_buffers;
};
struct rk_v4l2_buffer *rk_v4l2_get_input_buffer(struct rk_v4l2_object *ctx);

bool rk_v4l2_streamon_all(struct rk_v4l2_object *ctx);
struct rk_v4l2_object *rk_v4l2_dec_create(int8_t *vpu_path);
void rk_v4l2_destroy(struct rk_v4l2_object *ctx);
#endif
