
#define ALOG_TAG "AvcDecapi"
//#include <utils/Log.h>

#include "dwl.h"

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <linux/fb.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
//#include "framemanager.h"
#include "h264hwd_container.h"
#include "h264decapi.h"
#include "pv_avcdec_api.h"
#include "regdrv.h"

//#include "vpu.h"
#include "vpu_mem.h"
enum AVCDEC_ERRORTYPE {
  NO_INIT = -1,
  NO_ERROR,
  NO_MEMORY,
  INVALID_OPERATION
};

#define DEFAULT_STREAM_SIZE     1024*256

static void rk_AvcDecoder_reset(struct rk_avc_decoder *dec);
static int32_t rk_AvcDecoder_deinit(struct rk_avc_decoder *dec);

static int32_t rk_AvcDecoder_init(struct rk_avc_decoder *dec)
{
  H264DecRet ret;

  if (dec->status) {
      printf("init but status is %d\n", dec->status);
      return dec->status;
  }

  ret = H264DecInit(dec->H264deccont, 0);
  if (ret != H264DEC_OK) {
      printf("H264DecInit failed ret %d\n", ret);
      rk_AvcDecoder_deinit(dec);
      return dec->status = INVALID_OPERATION;
  }

  // clear all buffer to unrefered
  dec->dpb_size = 20;
  memset(dec->dpb_status, 0, sizeof(dec->dpb_status));

  printf("init done status %x dpb_size %d\n", dec->status, dec->dpb_size);
  return NO_ERROR;
}

static int32_t rk_AvcDecoder_prepareStream(struct rk_avc_decoder *dec, uint8_t* aInputBuf, uint32_t aInBufSize)
{
    if (dec->status)
        return dec->status;

    if (NULL == dec->streamMem) {
        printf("streamMem is NULL\n");
        return dec->status = NO_MEMORY;
    }

    if (0 == dec->streamSize) {
        if (VPUMallocLinear(dec->streamMem, DEFAULT_STREAM_SIZE))
        {
            printf("VPUMallocLinear streamMem fail\n");
            return dec->status = NO_MEMORY;
        }
        dec->streamSize = DEFAULT_STREAM_SIZE;
    }

    if ((aInBufSize + 1024) > dec->streamSize) {
        VPUFreeLinear(dec->streamMem);
        if(VPUMallocLinear(dec->streamMem, (aInBufSize+1024))) {
            printf("VPUMallocLinear realloc fail \n");
            return dec->status = NO_MEMORY;
        }
        dec->streamSize = aInBufSize;
    }

    memcpy((void *)dec->streamMem->vir_addr, aInputBuf, aInBufSize);
    memset(((uint8_t *)dec->streamMem->vir_addr) + aInBufSize, 0, 1024);
    if (VPUMemClean(dec->streamMem)) {
        printf("avc VPUMemClean error");
        return dec->status = INVALID_OPERATION;
    }

    return 0;
}

static int32_t rk_AvcDecoder_oneframe(struct rk_avc_decoder *dec,
                               uint8_t* aInputBuf, uint32_t aInBufSize)
{
    H264DecRet ret;
    H264DecInput decInput;
    H264DecOutput decOutput;
    u32 error_count = 0;
	int i;
    if (dec->status) {
        printf("pv_on2avcdecoder_oneframe return status %d\n", dec->status);
        return dec->status;
    }

    if (rk_AvcDecoder_prepareStream(dec, aInputBuf, aInBufSize)) {
        printf("prepareStream failed ret %d\n", dec->status);
        return dec->status;
    }

    decInput.skipNonReference   = 0;
    decInput.streamBusAddress   = (u32)dec->streamMem->phy_addr;
    decInput.pStream            = (u8*)dec->streamMem->vir_addr;
    decInput.dataLen            = aInBufSize;
    decInput.picId              = 0;

#define ERR_RET(dec, err) do { ret = err; rk_AvcDecoder_reset(dec); goto RET; } while (0)
#define ERR_CONTINUE(dec, err) do { rk_AvcDecoder_reset(dec); error_count++; } while (0)
    /* main decoding loop */
    do
    {
        /* Picture ID is the picture number in decoding order */
        /* call API function to perform decoding */
	//printf("before H264DecDecode, activePps: %p\n", dec->H264deccont->storage.activePps);
        ret = H264DecDecode(dec,dec->H264deccont, &decInput, &decOutput);

	/*
	printf("after H264DecDecode, activePps: %p slice type %d\n",
			dec->H264deccont->storage.activePps
			,dec->H264deccont->storage.sliceHeader[0].sliceType);
	*/
	aInBufSize = decOutput.dataLeft;

        switch (ret) {
        case H264DEC_PIC_DECODED:
        case H264DEC_PENDING_FLUSH: {
        } break;

        case H264DEC_STREAM_NOT_SUPPORTED: {
            printf("ERROR: UNSUPPORTED STREAM!\n");
            ERR_CONTINUE(dec, -3);
        } break;

        case H264DEC_SIZE_TOO_LARGE: {
            printf("ERROR: not support too large frame!\n");
            ERR_RET(dec, H264DEC_PARAM_ERROR);
        } break;

        case H264DEC_HDRS_RDY: {
            H264DecInfo decInfo;
            /* Stream headers were successfully decoded
             * -> stream information is available for query now */

            if (H264DEC_OK != H264DecGetInfo(dec->H264deccont, &decInfo))
            {
                printf("ERROR in getting stream info!\n");
                //pv_on2avcdecoder_deinit();
                ERR_RET(dec, H264DEC_PARAM_ERROR);
            }

            printf("Width %d Height %d\n", decInfo.picWidth, decInfo.picHeight);
            printf("videoRange %d, matrixCoefficients %d\n",
                         decInfo.videoRange, decInfo.matrixCoefficients);
        } break;

        case H264DEC_ADVANCED_TOOLS: {
            assert(decOutput.dataLeft);
            printf("avc stream using advanced tools not supported\n");
            ERR_CONTINUE(dec, -2);
        } break;

        case H264DEC_OK:
            /* nothing to do, just call again */
        case H264DEC_STRM_PROCESSED:
        case H264DEC_NONREF_PIC_SKIPPED:
        case H264DEC_STRM_ERROR: {
        } break;

        case H264DEC_NUMSLICE_ERROR: {
            error_count++;
        } break;

        case H264DEC_HW_TIMEOUT: {
            printf("Timeout\n");
            ERR_RET(dec, H264DEC_HW_TIMEOUT);
        } break;

        default: {
            printf("FATAL ERROR: %d\n", ret);
            ERR_RET(dec, H264DEC_PARAM_ERROR);
        } break;
        }

        /* break out of do-while if maxNumPics reached (dataLen set to 0) */
        if ((decInput.dataLen == 0) || (error_count >= 2))
        {
            if (error_count)
                printf("error_count %d\n", error_count);
            decInput.pStream += (decInput.dataLen - decOutput.dataLeft);
            decInput.streamBusAddress += (decInput.dataLen - decOutput.dataLeft);
            decInput.dataLen = decOutput.dataLeft;
            break;
        }

        if ((ret == H264DEC_HDRS_RDY) || decOutput.dataLeft)
        {
            decInput.pStream += (decInput.dataLen - decOutput.dataLeft);
            decInput.streamBusAddress += (decInput.dataLen - decOutput.dataLeft);
            decInput.dataLen = decOutput.dataLeft;
            printf("(ret == H264DEC_HDRS_RDY) decInput.dataLen %d\n", decInput.dataLen);
        }

        /* keep decoding until if decoder only get its header */
    } while((ret == H264DEC_HDRS_RDY) || (ret == H264DEC_NUMSLICE_ERROR) || decOutput.dataLeft);
RET:
    aInBufSize = 0;
    return ret;
}

static int32_t rk_AvcDecoder_pictureReady(struct rk_avc_decoder *dec, int index)
{
    H264DecPictureReady(dec, dec->H264deccont, index);
    return 0;
}

static int32_t rk_AvcDecoder_getPicture(struct rk_avc_decoder *dec)
{
    return H264DecGetPicture(dec, dec->H264deccont);
}

static int rk_AvcDecoder_getUnrefedPicture(struct rk_avc_decoder *dec)
{
    return H264DecGetFreeDPBSlot(dec, dec->H264deccont);
}

static int32_t rk_AvcDecoder_deinit(struct rk_avc_decoder *dec)
{
    /* if output in display order is preferred, the decoder shall be forced
     * to output pictures remaining in decoded picture buffer. Use function
     * H264DecNextPicture() to obtain next picture in display order. Function
     * is called until no more images are ready for display. Second parameter
     * for the function is set to '1' to indicate that this is end of the
     * stream and all pictures shall be output */
    if(dec->streamMem->vir_addr != NULL)
    {
        VPUFreeLinear(dec->streamMem);
    }
    dec->streamSize = 0;
    /* release decoder instance */
    H264DecRelease(dec->H264deccont);

    printf("deinit DONE\n");

    return 0;
}

static void rk_AvcDecoder_reset(struct rk_avc_decoder *dec)
{
    H264DecPicture decPicture;

    if (dec->status)
        return ;

    H264DecReset(dec->H264deccont);
}

void rk_AvcDecoder_getsps(struct rk_avc_decoder *dec)
{
  int i;
    seqParamSet_t *actSPS = dec->H264deccont->storage.activeSps;

    dec->sps.profile_idc = actSPS->profileIdc;
    dec->sps.constraint_set_flags = actSPS->constrained_set0_flag;
    dec->sps.level_idc = actSPS->levelIdc;
    dec->sps.seq_parameter_set_id = actSPS->seqParameterSetId;
    dec->sps.chroma_format_idc = actSPS->chromaFormatIdc;
    dec->sps.bit_depth_luma_minus8 = 0;
    dec->sps.bit_depth_chroma_minus8 = 0;
    dec->sps.log2_max_frame_num_minus4 = actSPS->maxFrameNum;
    dec->sps.pic_order_cnt_type = actSPS->picOrderCntType;
    dec->sps.log2_max_pic_order_cnt_lsb_minus4 = actSPS->maxPicOrderCntLsb;
    dec->sps.offset_for_non_ref_pic = actSPS->offsetForNonRefPic;
    dec->sps.offset_for_top_to_bottom_field = actSPS->offsetForTopToBottomField;
    dec->sps.num_ref_frames_in_pic_order_cnt_cycle = actSPS->numRefFramesInPicOrderCntCycle;
    for (i=0; i<sizeof(actSPS->offsetForRefFrame)/sizeof(i32); i++) {
        dec->sps.offset_for_ref_frame[i] = actSPS->offsetForRefFrame[i];
    }
    dec->sps.max_num_ref_frames = actSPS->numRefFrames;
    dec->sps.pic_width_in_mbs_minus1 = actSPS->picWidthInMbs - 1;
    dec->sps.pic_height_in_map_units_minus1 = actSPS->picHeightInMbs - 1;

    dec->sps.flags = 0;
    //dec->sps.flags |= actSPS-> V4L2_H264_SPS_FLAG_SEPARATE_COLOUR_PLANE
    //dec->sps.flags |= actSPS-> V4L2_H264_SPS_FLAG_QPPRIME_Y_ZERO_TRANSFORM_BYPASS
    dec->sps.flags |= (actSPS->deltaPicOrderAlwaysZeroFlag & 1) << 2;
    dec->sps.flags |= (actSPS->gapsInFrameNumValueAllowedFlag & 1) << 3;
    dec->sps.flags |= (actSPS->frameMbsOnlyFlag & 1) << 4;
    dec->sps.flags |= (actSPS->mbAdaptiveFrameFieldFlag & 1) << 5;
    dec->sps.flags |= (actSPS->direct8x8InferenceFlag & 1) << 6;
}

void rk_AvcDecoder_getpps(struct rk_avc_decoder *dec)
{
    picParamSet_t *actPPS = dec->H264deccont->storage.activePps;

    dec->pps.pic_parameter_set_id   				= actPPS->picParameterSetId;
    dec->pps.seq_parameter_set_id 					= actPPS->seqParameterSetId;
    dec->pps.num_ref_idx_l0_default_active_minus1 	= actPPS->numRefIdxL0Active - 1;
    dec->pps.num_ref_idx_l1_default_active_minus1 	= actPPS->numRefIdxL1Active - 1;
    dec->pps.weighted_bipred_idc 					= actPPS->weightedBiPredIdc;
    dec->pps.pic_init_qp_minus26 					= actPPS->picInitQp - 26;
    dec->pps.pic_init_qs_minus26 					= 0;
    dec->pps.chroma_qp_index_offset 				= actPPS->chromaQpIndexOffset;
    dec->pps.second_chroma_qp_index_offset 			= actPPS->chromaQpIndexOffset2;
    dec->pps.flags 								= 0;
    dec->pps.flags 								|= actPPS->entropyCodingModeFlag << 0;
    dec->pps.flags 								|= actPPS->picOrderPresentFlag << 1;
    dec->pps.flags 								|= actPPS->weightedPredFlag << 2;
    dec->pps.flags 								|= actPPS->deblockingFilterControlPresentFlag << 3;
    dec->pps.flags 								|= actPPS->constrainedIntraPredFlag << 4;
    dec->pps.flags 								|= actPPS->redundantPicCntPresentFlag << 5;
    dec->pps.flags 								|= actPPS->transform8x8Flag << 6;
    dec->pps.flags 								|= actPPS->scalingMatrixPresentFlag << 7;
}

void rk_AvcDecoder_getSliceHeader(struct rk_avc_decoder *dec)
{
  sliceHeader_t *slicehdr = &dec->H264deccont->storage.sliceHeader[0];

  dec->slice_param.first_mb_in_slice 			= slicehdr->firstMbInSlice;
  dec->slice_param.slice_type 					= slicehdr->sliceType;
  dec->slice_param.pic_parameter_set_id 		= slicehdr->picParameterSetId;
  dec->slice_param.colour_plane_id 				= 0;
  dec->slice_param.frame_num 					= slicehdr->frameNum;
  dec->slice_param.idr_pic_id 					= slicehdr->idrPicId;
  dec->slice_param.pic_order_cnt_lsb 			= slicehdr->picOrderCntLsb;
  dec->slice_param.delta_pic_order_cnt_bottom 	= slicehdr->deltaPicOrderCntBottom;
  dec->slice_param.delta_pic_order_cnt0 		= slicehdr->deltaPicOrderCnt[0];
  dec->slice_param.delta_pic_order_cnt1 		= slicehdr->deltaPicOrderCnt[1];
  dec->slice_param.redundant_pic_cnt 			= slicehdr->redundantPicCnt;
  dec->slice_param.cabac_init_idc 				= slicehdr->cabacInitIdc;
  dec->slice_param.slice_qp_delta 				= slicehdr->sliceQpDelta;
  dec->slice_param.slice_qs_delta 				= 0;
  dec->slice_param.disable_deblocking_filter_idc = slicehdr->disableDeblockingFilterIdc;
  dec->slice_param.slice_alpha_c0_offset_div2 	= slicehdr->sliceAlphaC0Offset;
  dec->slice_param.slice_beta_offset_div2 		= slicehdr->sliceBetaOffset;
  dec->slice_param.slice_group_change_cycle 	= slicehdr->sliceGroupChangeCycle;
  dec->slice_param.num_ref_idx_l0_active_minus1 = slicehdr->numRefIdxL0Active - 1;
  dec->slice_param.num_ref_idx_l1_active_minus1 = slicehdr->numRefIdxL1Active - 1;

  dec->slice_param.flags = slicehdr->fieldPicFlag;
  dec->slice_param.flags |= slicehdr->bottomFieldFlag << 1;
  dec->slice_param.flags |= slicehdr->directSpatialMvPredFlag << 2;

  dec->slice_param.dec_ref_pic_marking_bit_size	= slicehdr->decRefPicMarking.strmLen;
  dec->slice_param.pic_order_cnt_bit_size = slicehdr->pocLengthHw;
}

void rk_AvcDecoder_getListInfo(struct rk_avc_decoder *dec)
{
  int i;
  dpbStorage_t *dpb = dec->H264deccont->storage.dpb;
  for(i = 0; i < 16; i++)
  {
    dec->dec_param.ref_pic_list_b0[i] 				= dpb->list0[i];//dpb->buffer;
    dec->dec_param.ref_pic_list_b1[i] 				= dpb->list1[i];//dpb->buffer;
    dec->dec_param.ref_pic_list_p0[i] 				= dpb->listP[i];//dpb->buffer;

  }
}

#define DPB_LONG_TERM(status) \
      (status[0] == PIC_LONG_TERM && status[1] == PIC_LONG_TERM)

#define DPB_SHORT_TERM(status) \
      (status[0] == PIC_SHORT_TERM && status[1] == PIC_SHORT_TERM)

#define DPB_ACTIVE(status) \
      (DPB_LONG_TERM(status) || DPB_SHORT_TERM(status))

#define DPB_ACTIVE_FLAG(status) \
      (DPB_ACTIVE(status) ? V4L2_H264_DPB_ENTRY_FLAG_ACTIVE : 0)

#define DPB_LONG_TERM_FLAG(status) \
      (DPB_LONG_TERM(status) ? V4L2_H264_DPB_ENTRY_FLAG_LONG_TERM : 0)

void rk_AvcDecoder_getDpbInfo(struct rk_avc_decoder *dec)
{
  dpbStorage_t *dpb = dec->H264deccont->storage.dpb;
  int i;
  if (dec->dec_param.top_field_order_cnt
      != dec->H264deccont->storage.poc->picOrderCnt[0])
      dec->dec_param.num_slices = 0;

  dec->dec_param.top_field_order_cnt =
	  dec->H264deccont->storage.poc->picOrderCnt[0];
  dec->dec_param.bottom_field_order_cnt =
	  dec->H264deccont->storage.poc->picOrderCnt[1];
  memset(&dec->dec_param.dpb, 0, sizeof(dec->dec_param.dpb));
  for(i = 0; i < 16; i++)
  {
    if(dpb->buffer[i]!=NULL)
    {
      dec->dec_param.dpb[i].buf_index = dpb->buffer[i]->data->buffer_index;
      dec->dec_param.dpb[i].frame_num = dpb->buffer[i]->frameNum;
      dec->dec_param.dpb[i].pic_num = dpb->buffer[i]->picNum;
      dec->dec_param.dpb[i].top_field_order_cnt = dpb->buffer[i]->picOrderCnt[0];
      dec->dec_param.dpb[i].bottom_field_order_cnt = dpb->buffer[i]->picOrderCnt[1];
      dec->dec_param.dpb[i].flags = DPB_ACTIVE_FLAG(dpb->buffer[i]->status) |
	      DPB_LONG_TERM_FLAG(dpb->buffer[i]->status);
    }
  }
}

void rk_AvcDecoder_getContext(struct rk_avc_decoder *dec)
{
	dec->dec_param.idr_pic_flag = dec->H264deccont->storage.prevNalUnit->nalUnitType == 5;
}

static struct rk_vdec_ops avc_dec_ops = {
  .init = rk_AvcDecoder_init,
  .oneframe = rk_AvcDecoder_oneframe,
  .picture_ready = rk_AvcDecoder_pictureReady,
  .get_picture = rk_AvcDecoder_getPicture,
  .get_unrefed_picture = rk_AvcDecoder_getUnrefedPicture,
  .deinit = rk_AvcDecoder_deinit
};

struct rk_avc_decoder* rk_avc_decoder_alloc_ctx(void)
{
  struct rk_avc_decoder* dec =
    (struct rk_avc_decoder*)calloc(1, sizeof(struct rk_avc_decoder));

  memset(dec, 0, sizeof(dec));

  if (dec == NULL) {
    perror("allocate decoder context failed\n");
    return NULL;
  }

  dec->status = NO_INIT;
  dec->streamSize = 0;
  dec->streamMem = NULL;
  dec->H264deccont = NULL;

  dec->ops = &avc_dec_ops;

  do {
        dec->streamMem = (VPUMemLinear_t *)malloc(sizeof(VPUMemLinear_t));
        memset(dec->streamMem,0,sizeof(VPUMemLinear_t));
        if (NULL == dec->streamMem) {
            printf("On2_AvcDecoder malloc streamMem failed");
            break;
        }
        dec->H264deccont = (decContainer_t *)malloc(sizeof(decContainer_t));
        if (NULL == dec->H264deccont) {
            printf("On2_AvcDecoder malloc decContainer_t failed");
            break;
        }
        dec->status = NO_ERROR;
  } while (0);

  if (dec->status) {
    if (dec->streamMem) {
      free(dec->streamMem);
      dec->streamMem = NULL;
    }
    if (dec->H264deccont) {
      free(dec->H264deccont);
      dec->H264deccont = NULL;
    }
  }

  return dec;
}

void rk_avc_decoder_free_ctx(struct rk_avc_decoder *dec)
{
  if (dec == NULL) {
    return;
  }

  if (dec->streamMem)
    free(dec->streamMem);

  if (dec->H264deccont)
    free(dec->H264deccont);

  free(dec);
}
