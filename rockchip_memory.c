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
#include "common.h"
#include "rockchip_driver.h"
#include "rockchip_memory.h"

void
rockchip_reference_buffer_store(struct buffer_store **ptr, 
		struct buffer_store *buffer_store)
{
	assert(*ptr == NULL);
	if (buffer_store) {
		buffer_store->ref_count++;
		*ptr = buffer_store;
	}
}

void rockchip_release_buffer_store(struct buffer_store **ptr)
{
	struct buffer_store *buffer_store = *ptr;
	if (NULL == buffer_store)
		return;

	assert(buffer_store->buffer);
	buffer_store->ref_count--;

	if (0 == buffer_store->ref_count) {
		buffer_store->buffer = NULL;
		free(buffer_store);
	}

	*ptr = NULL;
}

VAStatus 
rockchip_allocate_buffer(VADriverContextP ctx, VAContextID context, 
VABufferType type, unsigned int size, unsigned int num_elements, 
void *data, VABufferID * buf_id) 
{
	struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);
	VAStatus vaStatus = VA_STATUS_ERROR_UNKNOWN;
	struct object_buffer *obj_buffer;
	struct buffer_store *buffer_store = NULL;
	int bufferID;

	/* Validate type */
	switch (type) {
	/* Raw Image */
	case VAImageBufferType:
	/* Decoder */
	case VAPictureParameterBufferType:
	case VAIQMatrixBufferType:
	case VAQMatrixBufferType:
	case VABitPlaneBufferType:
	case VASliceGroupMapBufferType:
	case VASliceParameterBufferType:
	case VASliceDataBufferType:
	case VAMacroblockParameterBufferType:
	case VAResidualDataBufferType:
	case VADeblockingParameterBufferType:
#if 0
/* Not supported yet */
	/* Encoder */
	case VAEncCodedBufferType:
	case VAEncSequenceParameterBufferType:
	case VAEncPictureParameterBufferType:
	case VAEncPackedHeaderParameterBufferType:
	case VAEncPackedHeaderDataBufferType:
	case VAEncMiscParameterBufferType:
	case VAEncMacroblockMapBufferType:
	/* Don't know */
	case VAProcPipelineParameterBufferType:
	case VAProcFilterParameterBufferType:
	case VAHuffmanTableBufferType:
	case VAProbabilityBufferType:
#endif
		/* Ok */
		break;
	default:
		return VA_STATUS_ERROR_UNSUPPORTED_BUFFERTYPE;
	}

	bufferID = object_heap_allocate(&rk_data->buffer_heap);
	obj_buffer = BUFFER(bufferID);
	if (NULL == obj_buffer) {
		return VA_STATUS_ERROR_ALLOCATION_FAILED;
	}

	obj_buffer->max_num_elements = num_elements;
	obj_buffer->num_elements = num_elements;
	obj_buffer->size_element = size;
	obj_buffer->type = type;
	obj_buffer->buffer_store = NULL;

	buffer_store = calloc(1, sizeof(struct buffer_store));
	assert(buffer_store);
	buffer_store->ref_count = 1;

	/* FIXME you need improve performance here, using something like DRI */
	int msize = size;

	buffer_store->buffer = malloc(msize * num_elements);

	if (data) {
		assert(buffer_store->buffer);
		memcpy(buffer_store->buffer, data, size * num_elements);
	}

	buffer_store->num_elements = obj_buffer->num_elements;
	rockchip_reference_buffer_store(&obj_buffer->buffer_store,
					buffer_store);
	rockchip_release_buffer_store(&buffer_store);
	*buf_id = bufferID;
	/* FIXME the status should be update somewhere */
	vaStatus = VA_STATUS_SUCCESS;

	return vaStatus;
}

VAStatus 
rockchip_allocate_refernce(VADriverContextP ctx, VABufferType type,  
VABufferID *buf_id, void *data, unsigned int size)
{
	struct rockchip_driver_data *rk_data = rockchip_driver_data(ctx);
	VAStatus vaStatus = VA_STATUS_ERROR_UNKNOWN;
	struct object_buffer *obj_buffer;
	struct buffer_store *buffer_store = NULL;
	int bufferID;

	/* Validate type */
	switch (type) {
	/* Raw Image */
	case VAImageBufferType:
		break;
	default:
		return VA_STATUS_ERROR_UNSUPPORTED_BUFFERTYPE;
	}

	bufferID = object_heap_allocate(&rk_data->buffer_heap);
	obj_buffer = BUFFER(bufferID);
	if (NULL == obj_buffer) {
		return VA_STATUS_ERROR_ALLOCATION_FAILED;
	}

	obj_buffer->max_num_elements = 1;
	obj_buffer->num_elements = 1;
	obj_buffer->size_element = size;
	obj_buffer->type = type;
	obj_buffer->buffer_store = NULL;

	buffer_store = calloc(1, sizeof(struct buffer_store));
	assert(buffer_store);
	buffer_store->ref_count = 1;

	if (data)
		buffer_store->buffer = data;
	else
		buffer_store->buffer = NULL;

	buffer_store->num_elements = obj_buffer->num_elements;
	rockchip_reference_buffer_store(&obj_buffer->buffer_store,
					buffer_store);
	rockchip_release_buffer_store(&buffer_store);
	*buf_id = bufferID;
	/* FIXME the status should be update somewhere */
	vaStatus = VA_STATUS_SUCCESS;

	return vaStatus;
}
