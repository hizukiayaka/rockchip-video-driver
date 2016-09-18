#include "v4l2_utils.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include "rockchip_debug.h"

#define SYS_PATH		"/sys/class/video4linux/"
#define DEV_PATH		"/dev/"

static bool 
rk_v4l2_open(struct rk_v4l2_object *ctx, const char *device_path)
{
	int fd = open(device_path, O_RDWR /*| O_NONBLOCK*/ );

	if (fd <= 0) {
		return false;
	}

	ctx->video_fd = fd;

	return true;
}

static bool 
rk_v4l2_open_by_name(struct rk_v4l2_object *ctx, const char *name)
{
	DIR *dir;
	struct dirent *ent;
	bool ret = false;

	if ((dir = opendir(SYS_PATH)) != NULL) {
		while ((ent = readdir(dir)) != NULL) {
			FILE *fp;
			char path[64];
			char dev_name[64];

			snprintf(path, 64, SYS_PATH "%s/name",
				 ent->d_name);
			fp = fopen(path, "r");
			if (!fp)
				continue;
			if (!fgets(dev_name, 32, fp))
				dev_name[0] = '\0';
			fclose(fp);

			if (!strstr(dev_name, name))
				continue;

			snprintf(path, sizeof(path), DEV_PATH "%s",
				 ent->d_name);

			ret = rk_v4l2_open(ctx, path);
			break;
		}
		closedir(dir);
	}
	return ret;
}

static void rk_v4l2_input_release(struct rk_v4l2_object *ctx)
{
	struct v4l2_requestbuffers breq = { 0, 
		V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_MEMORY_MMAP };

	ioctl(ctx->video_fd, VIDIOC_REQBUFS, &breq);
}

static void rk_v4l2_output_release(struct rk_v4l2_object *ctx)
{
	struct v4l2_requestbuffers breq = { 0, 
		V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_MEMORY_MMAP };

	ioctl(ctx->video_fd, VIDIOC_REQBUFS, &breq);
}

static int32_t rk_v4l2_input_allocate
(void *data, uint32_t count)
{
	struct rk_v4l2_object *ctx = (struct rk_v4l2_object *)data;
	struct v4l2_requestbuffers breq = { count, 
		V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, V4L2_MEMORY_MMAP };
	struct v4l2_buffer buffer;
	struct v4l2_exportbuffer expbuf;
	struct v4l2_format *format = &ctx->input_format;
	struct v4l2_plane planes[RK_VIDEO_MAX_PLANES];

	rk_v4l2_input_release(ctx);

	if (ioctl(ctx->video_fd, VIDIOC_REQBUFS, &breq) < 0) {
		rk_info_msg("Allocate failed\n");
		return 0;
	}

	if (breq.count < 1) {
		rk_info_msg("Not enough memory to allocate buffers");
		return 0;
	}

	ctx->input_buffer = calloc(breq.count, sizeof(struct rk_v4l2_buffer));
	if (NULL == ctx->input_buffer)
	{
		rk_error_msg("Failed to allocate space for input buffer meta data\n");
		rk_v4l2_input_release(ctx);
		return 0;
	}
	ctx->num_input_buffers = breq.count;

	memset(&expbuf, 0, sizeof(expbuf));
	memset(&buffer, 0, sizeof(buffer));
	memset(&planes, 0, sizeof(planes));

	buffer.type = format->type;
	buffer.memory = V4L2_MEMORY_MMAP;
	buffer.length = format->fmt.pix_mp.num_planes;
	buffer.m.planes = planes;

	expbuf.type = format->type;
	expbuf.flags = O_CLOEXEC | O_RDWR;

	for (int32_t i = 0; i < breq.count; i++) {
		buffer.index = i;

		 if (ioctl(ctx->video_fd, VIDIOC_QUERYBUF, &buffer) < 0) {
			 rk_error_msg("Query of input buffer failed\n");
			 return 0;
		 }

		expbuf.index = i;
		for (int32_t j = 0; j < format->fmt.pix_mp.num_planes; j++) {
			void *ptr = NULL;
			expbuf.plane = j;

			if (ioctl(ctx->video_fd, VIDIOC_EXPBUF, &expbuf) < 0) 
			{
				rk_error_msg
					("Export of output buffer failed\n");
				return i;
			}

			ptr = mmap(NULL, buffer.m.planes[j].length,
					 PROT_READ | PROT_WRITE,
					 MAP_SHARED,
					 expbuf.fd,
					 0);

			if (ptr == MAP_FAILED) {
				rk_error_msg("Failed to map input buffer");
			}
			ctx->input_buffer[i].plane[j].length =
				 buffer.m.planes[j].length;
			ctx->input_buffer[i].plane[j].data = ptr;
			ctx->input_buffer[i].plane[j].dma_fd = expbuf.fd;

		}
		ctx->input_buffer[i].state = BUFFER_FREE;
		ctx->input_buffer[i].index = i;
		ctx->input_buffer[i].length = format->fmt.pix_mp.num_planes;

	}
	ctx->has_free_input_buffers = breq.count;

	return breq.count;
}

static int32_t rk_v4l2_output_allocate
(void *data, uint32_t count)
{
	struct rk_v4l2_object *ctx = (struct rk_v4l2_object *)data;
	struct v4l2_requestbuffers breq = { count, 
		V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, V4L2_MEMORY_MMAP };
	struct v4l2_exportbuffer expbuf;
	struct v4l2_format *format = &ctx->output_format;
	struct v4l2_buffer buffer;
	struct v4l2_plane planes[RK_VIDEO_MAX_PLANES];
	int32_t ret;

	rk_v4l2_output_release(ctx);

	ret = ioctl(ctx->video_fd, VIDIOC_REQBUFS, &breq);
	if (ret < 0) {
		rk_info_msg("Allocate failed\n");
		return 0;
	}

	if (breq.count < 1) {
		rk_info_msg("Not enough memory to allocate buffers");
		return 0;
	}

	ctx->output_buffer = calloc(breq.count, sizeof(struct rk_v4l2_buffer));
	if (NULL == ctx->output_buffer)
	{
		rk_error_msg("Failed to allocate space for output buffer meta data\n");
		rk_v4l2_output_release(ctx);
		return 0;
	}

	ctx->num_output_buffers = breq.count;

	memset(&expbuf, 0, sizeof(expbuf));
	memset(&buffer, 0, sizeof(buffer));
	memset(&planes, 0, sizeof(planes));

	buffer.type = format->type;
	buffer.memory = V4L2_MEMORY_MMAP;
	buffer.length = format->fmt.pix_mp.num_planes;
	buffer.m.planes = planes;

	expbuf.type = format->type;
	expbuf.flags = O_CLOEXEC | O_RDWR;

	for (int32_t i = 0; i < breq.count; i++) {
		buffer.index = i;

		 if (ioctl(ctx->video_fd, VIDIOC_QUERYBUF, &buffer) < 0) {
			 rk_error_msg("Query of output buffer failed\n");
			 return 0;
		 }

		expbuf.index = i;

		for (int32_t j = 0; j < format->fmt.pix_mp.num_planes; j++) {
			void *ptr = NULL;

			expbuf.plane = j;
			if (ioctl(ctx->video_fd, VIDIOC_EXPBUF, &expbuf) < 0) 
			{
				rk_error_msg
					("Export of output buffer failed\n");
				return i;
			}

			ptr = mmap(NULL, buffer.m.planes[j].length,
					 PROT_READ | PROT_WRITE,
					 MAP_SHARED,
					 expbuf.fd,
					 0);

			 if (ptr == MAP_FAILED) {
				 rk_error_msg("Failed to map output buffer\n");
			 }

			ctx->output_buffer[i].plane[j].length =
				 buffer.m.planes[j].length;
			ctx->output_buffer[i].plane[j].data = ptr;
			ctx->output_buffer[i].plane[j].dma_fd = expbuf.fd;
		}
		ctx->output_buffer[i].state = BUFFER_FREE;
		ctx->output_buffer[i].index = i;
		ctx->output_buffer[i].length = format->fmt.pix_mp.num_planes;
	}
	ctx->has_free_output_buffers = breq.count;

	return breq.count;
}

struct rk_v4l2_buffer *
rk_v4l2_get_input_buffer(struct rk_v4l2_object *ctx)
{
	for (uint32_t i = 0; i < ctx->num_input_buffers; i++) {
		if (BUFFER_FREE == ctx->input_buffer[i].state)
			return &ctx->input_buffer[i];
	}
	return NULL;
}

struct rk_v4l2_buffer *
rk_v4l2_get_output_buffer(struct rk_v4l2_object *ctx)
{
	for (uint32_t i = 0; i < ctx->num_output_buffers; i++) {
		if (BUFFER_FREE == ctx->output_buffer[i].state)
			return &ctx->output_buffer[i];
	}
	return NULL;
}

int32_t
rk_v4l2_buffer_total_bytesused(struct rk_v4l2_buffer *buffer)
{
	uint32_t ret = 0;

	for (uint32_t i = 0; i < buffer->length; i++) {
		ret += buffer->plane[i].bytesused;
	}

	return ret;
}

static int32_t rk_v4l2_qbuf_input
(void *data, struct rk_v4l2_buffer *buffer)
{
	struct rk_v4l2_object *ctx = (struct rk_v4l2_object *)data;
	struct v4l2_buffer qbuf;
	struct v4l2_plane planes[RK_VIDEO_MAX_PLANES];
	struct v4l2_format *format;

	format = &ctx->input_format;

	memset(&qbuf, 0, sizeof(qbuf));
	memset(planes, 0, sizeof(planes));

	qbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	qbuf.memory = V4L2_MEMORY_MMAP;
	qbuf.index = buffer->index;
	qbuf.length = format->fmt.pix_mp.num_planes;
	qbuf.m.planes = planes;
	qbuf.request = buffer->index;

	for(uint32_t i = 0; i < format->fmt.pix_mp.num_planes; i++){
		planes[i].bytesused = buffer->plane[i].bytesused;
		planes[i].length = buffer->plane[i].length;
	}

	if (ioctl(ctx->video_fd, VIDIOC_QBUF, &qbuf)) {
		rk_info_msg("Enqueuing of input buffer %d failed\n", 
				buffer->index);
		return -1;
	}

	buffer->state = BUFFER_ENQUEUED;

	return 0;
}

static int32_t rk_v4l2_qbuf_output
(void *data, struct rk_v4l2_buffer *buffer)
{
	struct rk_v4l2_object *ctx = (struct rk_v4l2_object *)data;
	struct v4l2_buffer qbuf;
	struct v4l2_plane planes[RK_VIDEO_MAX_PLANES];
	struct v4l2_format *format;

	format = &ctx->output_format;

	memset(&qbuf, 0, sizeof(qbuf));
	memset(planes, 0, sizeof(planes));

	qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	qbuf.memory = V4L2_MEMORY_MMAP;
	qbuf.index = buffer->index;
	qbuf.length = format->fmt.pix_mp.num_planes;
	qbuf.m.planes = planes;

	if (ioctl(ctx->video_fd, VIDIOC_QBUF, &qbuf) < 0) {
		rk_info_msg("Enqueuing of output buffer %d failed; prev state: %d",
				buffer->index, buffer->state);
		return -1;
	}

	buffer->state = BUFFER_ENQUEUED;

	return 0;
}

static int32_t rk_v4l2_dqbuf_input
(void *data, struct rk_v4l2_buffer **buffer)
{
	struct rk_v4l2_object *ctx = (struct rk_v4l2_object *)data;
	struct v4l2_buffer dqbuf;
	struct v4l2_plane planes[RK_VIDEO_MAX_PLANES];
	struct v4l2_format *format;

	format = &ctx->input_format;

	memset(&dqbuf, 0, sizeof(dqbuf));
	memset(planes, 0, sizeof(planes));

	dqbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	dqbuf.memory = V4L2_MEMORY_MMAP;
	dqbuf.length = format->fmt.pix_mp.num_planes;
	dqbuf.m.planes = planes;

	if (ioctl(ctx->video_fd, VIDIOC_DQBUF, &dqbuf) < 0) {
		rk_info_msg("dequeuing of input buffer failed: %d",
				errno);
		return -1;
	}

	*buffer = &ctx->input_buffer[dqbuf.index];
	/* FIXME */
	(*buffer)->plane[0].bytesused = 0;
	/* After dequeue, I think it won't be used anymore */
	(*buffer)->state = BUFFER_FREE;
	
	return 0;
}

static int32_t rk_v4l2_dqbuf_output
(void *data, struct rk_v4l2_buffer **buffer)
{
	struct rk_v4l2_object *ctx = (struct rk_v4l2_object *)data;
	struct v4l2_buffer dqbuf;
	struct v4l2_plane planes[RK_VIDEO_MAX_PLANES];
	struct v4l2_format *format;

	format = &ctx->output_format;

	memset(&dqbuf, 0, sizeof(dqbuf));
	memset(planes, 0, sizeof(planes));

	dqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dqbuf.memory = V4L2_MEMORY_MMAP;
	dqbuf.length = format->fmt.pix_mp.num_planes;
	dqbuf.m.planes = planes;

	if (ioctl(ctx->video_fd, VIDIOC_DQBUF, &dqbuf) < 0) {
		rk_info_msg("dequeuing of output buffer failed: %d",
				errno);
		return -1;
	}

	*buffer = &(ctx->output_buffer[dqbuf.index]);

	for(uint32_t i = 0; i < format->fmt.pix_mp.num_planes; i++) {
		(*buffer)->plane[i].bytesused = dqbuf.m.planes[i].bytesused;
	}

	(*buffer)->state = BUFFER_DEQUEUED;
	
	return 0;
}

static int32_t 
rk_v4l2_dec_set_codec(void *data, uint32_t codec_type) 
{
	struct rk_v4l2_object *ctx = (struct rk_v4l2_object *)data;
	struct v4l2_format format;

	memset(&format, 0, sizeof(format));

	format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	format.fmt.pix_mp.pixelformat = codec_type;
	format.fmt.pix_mp.plane_fmt[0].sizeimage = MAX_CODEC_BUFFER;
	format.fmt.pix_mp.num_planes = 1;
	ctx->input_format = format;

	return (ioctl(ctx->video_fd, VIDIOC_S_FMT, &format));
}

static int32_t 
rk_v4l2_dec_set_fmt(void *data, uint32_t reversed)
{
	struct rk_v4l2_object *ctx = (struct rk_v4l2_object *)data;
	struct v4l2_format format;

	memset(&format, 0, sizeof(format));

	format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
	format.fmt.pix_mp.width = ctx->input_size.w;
	format.fmt.pix_mp.height = ctx->input_size.h;
	format.fmt.pix_mp.num_planes = 1;
	ctx->output_format = format;

	return (ioctl(ctx->video_fd, VIDIOC_S_FMT, &format));
}

static int32_t 
rk_v4l2_enc_set_codec(void *data, uint32_t codec_type) 
{
	struct rk_v4l2_object *ctx = (struct rk_v4l2_object *)data;
	struct v4l2_format format;

	memset(&format, 0, sizeof(format));

	format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	format.fmt.pix_mp.pixelformat = codec_type;
	format.fmt.pix_mp.plane_fmt[0].sizeimage = MAX_CODEC_BUFFER;
	format.fmt.pix_mp.num_planes = 1;
	ctx->output_format = format;

	return (ioctl(ctx->video_fd, VIDIOC_S_FMT, &format));
}

static int32_t 
rk_v4l2_enc_set_fmt(void *data, uint32_t reversed)
{
	struct rk_v4l2_object *ctx = (struct rk_v4l2_object *)data;
	struct v4l2_format format;

	memset(&format, 0, sizeof(format));

	format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
	format.fmt.pix_mp.width = ctx->input_size.w;
	format.fmt.pix_mp.height = ctx->input_size.h;
	format.fmt.pix_mp.num_planes = 1;
	ctx->input_format = format;

	return (ioctl(ctx->video_fd, VIDIOC_S_FMT, &format));
}

bool rk_v4l2_streamon_all(struct rk_v4l2_object *ctx)
{
	int32_t type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	if (ioctl(ctx->video_fd, VIDIOC_STREAMON, &type) < 0)
		return false;
	else
		ctx->input_streamon = true;

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	if (ioctl(ctx->video_fd, VIDIOC_STREAMON, &type) < 0)
		return false;
	else
		ctx->output_streamon = true;

	return true;
};

static const char *rk_vpu_dec_list[] = {
	"rockchip-vpu-vdec",	
	"rockchip-vpu-dec",
	"rk3288-vpu-dec",
};

struct rk_v4l2_object *rk_v4l2_dec_create(char *vpu_path)
{
	struct rk_v4l2_object *ctx;

	ctx = malloc(sizeof(struct rk_v4l2_object));
	if (NULL == ctx)
		return NULL;

	ctx->video_fd = -1;

	if (NULL != vpu_path)
	{
		if (!rk_v4l2_open(ctx, (const char *)vpu_path))
			goto create_ctx_err;
	}
	else 
	{
		for (uint8_t i = 0; 
			i < (sizeof(rk_vpu_dec_list)/sizeof(int8_t *)); i++)
		{
			if (rk_v4l2_open_by_name(ctx, rk_vpu_dec_list[i]))
				break;

			if ((sizeof(rk_vpu_dec_list)/sizeof(uint8_t *)) == i)
				goto create_ctx_err;
		}
	}

	ctx->ops.input_alloc = rk_v4l2_input_allocate;
	ctx->ops.output_alloc = rk_v4l2_output_allocate;
	ctx->ops.set_codec = rk_v4l2_dec_set_codec;
	ctx->ops.set_format = rk_v4l2_dec_set_fmt;
	ctx->ops.qbuf_input = rk_v4l2_qbuf_input;
	ctx->ops.qbuf_output = rk_v4l2_qbuf_output;
	ctx->ops.dqbuf_input = rk_v4l2_dqbuf_input;
	ctx->ops.dqbuf_output = rk_v4l2_dqbuf_output;

	return ctx;
create_ctx_err:
	free(ctx);
	return NULL;
}

static const char *rk_vpu_enc_list[] = {
	"rockchip-vpu-venc",	
	"rockchip-vpu-enc",
	"rk3288-vpu-enc",
};

struct rk_v4l2_object *rk_v4l2_enc_create(char *vpu_path)
{
	struct rk_v4l2_object *ctx;

	ctx = malloc(sizeof(struct rk_v4l2_object));
	if (NULL == ctx)
		return NULL;

	ctx->video_fd = -1;

	if (NULL != vpu_path)
	{
		if (!rk_v4l2_open(ctx, (const char *)vpu_path))
			goto create_ctx_err;
	}
	else 
	{
		for (uint8_t i = 0; 
			i < (sizeof(rk_vpu_enc_list)/sizeof(int8_t *)); i++)
		{
			if (rk_v4l2_open_by_name(ctx, rk_vpu_enc_list[i]))
				break;

			if ((sizeof(rk_vpu_enc_list)/sizeof(uint8_t *)) == i)
				goto create_ctx_err;
		}
	}

	ctx->ops.input_alloc = rk_v4l2_input_allocate;
	ctx->ops.output_alloc = rk_v4l2_output_allocate;
	ctx->ops.set_codec = rk_v4l2_enc_set_codec;
	ctx->ops.set_format = rk_v4l2_enc_set_fmt;
	ctx->ops.qbuf_input = rk_v4l2_qbuf_input;
	ctx->ops.qbuf_output = rk_v4l2_qbuf_output;
	ctx->ops.dqbuf_input = rk_v4l2_dqbuf_input;
	ctx->ops.dqbuf_output = rk_v4l2_dqbuf_output;

	return ctx;
create_ctx_err:
	free(ctx);
	return NULL;
}

void rk_v4l2_destroy(struct rk_v4l2_object *ctx)
{
	/* The streamoff should be enough to release the allocate 
	 * v4l2 buffer */
	/* FIXME the streamoff order is different between
	 * encoder and decoder */
	struct v4l2_format *format;

	if (NULL == ctx)
		return;

	int type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	if (ctx->input_streamon)
		if (ioctl(ctx->video_fd, VIDIOC_STREAMOFF, &type) < 0)
			rk_info_msg("Streamoff failed on input");
	ctx->input_streamon = false;

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	if (ctx->output_streamon)
		if (ioctl(ctx->video_fd, VIDIOC_STREAMOFF, &type) < 0)
			rk_info_msg("Streamoff failed on output");
	ctx->output_streamon = false;

	format = &ctx->input_format;
	for (uint32_t i = 0; i < ctx->num_input_buffers; i++) {
		for (uint32_t j = 0; j < format->fmt.pix_mp.num_planes; j++)
			if (NULL != ctx->input_buffer[i].plane[j].data)
				munmap(ctx->input_buffer[i].plane[j].data,
					ctx->input_buffer[i].plane[j].length);
	}

	format = &ctx->output_format;
	for (uint32_t i = 0; i < ctx->num_output_buffers; i++) {
		for (uint32_t j = 0; j < format->fmt.pix_mp.num_planes; j++)
			if (NULL != ctx->output_buffer[i].plane[j].data)
				munmap(ctx->output_buffer[i].plane[j].data,
					ctx->output_buffer[i].plane[j].length);
	}

	if (NULL != ctx->input_buffer)
		free(ctx->input_buffer);
	if (NULL != ctx->output_buffer)
		free(ctx->output_buffer);

	close(ctx->video_fd);
	ctx->video_fd = 0;
}
