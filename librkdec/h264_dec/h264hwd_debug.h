/*------------------------------------------------------------------------------
--                                                                            --
--       This software is confidential and proprietary and may be used        --
--        only as expressly authorized by a licensing agreement from          --
--                                                                            --
--                            Hantro Products Oy.                             --
--                                                                            --
--                   (C) COPYRIGHT 2006 HANTRO PRODUCTS OY                    --
--                            ALL RIGHTS RESERVED                             --
--                                                                            --
--                 The entire notice above must be reproduced                 --
--                  on all copies and should not be removed.                  --
--                                                                            --
--------------------------------------------------------------------------------
--
--  Abstract : Utility macros for debugging and tracing
--
--------------------------------------------------------------------------------
--
--  Version control information, please leave untouched.
--
--  $RCSfile: h264hwd_debug.h,v $
--  $Date: 2008/03/13 12:47:14 $
--  $Revision: 1.1 $
--
------------------------------------------------------------------------------*/

#ifndef __H264DEBUG_H__
#define __H264DEBUG_H__

/* macro for assertion, used only when _ASSERT_USED is defined */
#ifdef _ASSERT_USED
#ifndef ASSERT
#include <assert.h>
#define ASSERT(expr) assert(expr)
#endif
#else
#define ASSERT(expr)
#endif

#define DEBUG_PRINT(args) //printf args
#define ERROR_PRINT(msg) printf(msg)

#define DPBDEBUG		//printf
#define DPBOUTDEBUG		//printf//printf

#endif /* __H264DEBUG_H__ */
