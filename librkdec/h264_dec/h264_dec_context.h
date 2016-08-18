#ifndef _H264_DEC_CONTEXT_
#define _H264_DEC_CONTEXT_

#include <linux/types.h>
#include <linux/v4l2-controls.h>

/* picture type */
enum DPB_PIC_TYPE {
	PIC_TOP 		= 0,
	PIC_BOT 		= 1,
	PIC_FRAME		= 2,
};

/* picture status */
enum DPB_PIC_STATUS {

	/* valid but unreferenced status */
	PIC_UNUSED		= 0x0,
	/* valid but in frame gap, can not display */
	PIC_NON_EXISTING	= 0x1,
	/* valid and used as short term referenced */
	PIC_SHORT_TERM		= 0x2,
	/* valid and used as long  term referenced */
	PIC_LONG_TERM		= 0x3,
	/* invalid or undecoded status */
	PIC_EMPTY		= 0x0,
};

struct h264_dec_context {
	struct v4l2_ctrl_h264_sps *sps;
	struct v4l2_ctrl_h264_pps *pps;
	struct v4l2_ctrl_h264_scaling_matrix *scaling_matrix;
	struct v4l2_ctrl_h264_slice_param *slice_param;

	/* source stream buffer */

	/*
	   destination picture buffer, it contains three parts:
	   luma           - size: width * height
	   chroma         - size: width * height / 2
	   mv information - size: width * height / 2
	   total size will be width * height * 2
	*/

	struct v4l2_ctrl_h264_decode_param *dec_param;
};

#define H264D_NUM_CTRLS 5

#endif
