#ifndef _PV_AVCDEC_API_H_
#define _PV_AVCDEC_API_H_

#include "h264_dec_context.h"

typedef struct decContainer decContainer_t;
typedef struct VPUMem VPUMemLinear_t;

struct rk_avc_decoder;

struct rk_vdec_ops {
  int32_t (*init)(struct rk_avc_decoder *dec);
  int32_t (*oneframe)(struct rk_avc_decoder *dec,
                               uint8_t* aInputBuf, uint32_t aInBufSize);
  int32_t (*picture_ready)(struct rk_avc_decoder *dec, int index);
  int (*get_picture)(struct rk_avc_decoder *dec);
  int (*get_unrefed_picture)(struct rk_avc_decoder *dec);
  int32_t (*deinit)(struct rk_avc_decoder *dec);
};

#define USED_AS_NONREF          0
#define USED_AS_REC             1
#define USED_AS_REF             2

struct rk_avc_decoder {
  struct rk_vdec_ops *ops;
  int32_t status;
  int32_t streamSize;
  VPUMemLinear_t *streamMem;
  decContainer_t *H264deccont;

  long int frame_index[256];//per h264_cmodel_regs in file

  int width;
  int height;

  /* dpb management */
  int dpb_size;
  int dpb_status[32];

  char header[256];

  struct v4l2_ctrl_h264_sps sps;
  struct v4l2_ctrl_h264_pps pps;
  struct v4l2_ctrl_h264_scaling_matrix scaling_matrix;
  struct v4l2_ctrl_h264_slice_param slice_param;
  struct v4l2_ctrl_h264_decode_param dec_param;

  struct v4l2_ctrl_h264_slice_param slice_params[16];
};

struct rk_avc_decoder* rk_avc_decoder_alloc_ctx(void);
void rk_avc_decoder_free_ctx(struct rk_avc_decoder *dec);
void rk_AvcDecoder_getsps(struct rk_avc_decoder *dec);
void rk_AvcDecoder_getpps(struct rk_avc_decoder *dec);
void rk_AvcDecoder_getDpbInfo(struct rk_avc_decoder *dec);
void rk_AvcDecoder_getListInfo(struct rk_avc_decoder *dec);
void rk_AvcDecoder_getPoc(struct rk_avc_decoder *dec);
void rk_AvcDecoder_getScalingList(struct rk_avc_decoder *dec);
void rk_AvcDecoder_getSliceHeader(struct rk_avc_decoder *dec);
void rk_AvcDecoder_getContext(struct rk_avc_decoder *dec);


#endif
