#ifndef _COMMON_H_
#define _COMMON_H_

#define MAX_REFERENCE_FRAME 17
#define INVALID_TIMESTAMP  -1

/*
 * Reference Frame Queue
 * This queue holds all ref frames of current frame.
 * Drivers should use this queue to mv ref buffers,
 * so that the queue can be set to hw correctly.
 */
struct v4l2_refq {
	/* 
	 * Reference frame queue, this queue holds the
	 * frame id.
	 *
	 * VP8 has four types of reference frame:
	 * queue[0] - reconstruct frame
	 * queue[1] - reference frame
	 * queue[2] - golden frame
	 * queue[3] - alternative frame
	 *
	 * h264 TODO
	 */
	int32_t queue[MAX_REFERENCE_FRAME];
};

#endif
