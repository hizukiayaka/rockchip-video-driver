/*
 * h264bitstream - a library for reading and writing H.264 video
 * Copyright (C) 2005-2007 Auroras Entertainment, LLC
 * Copyright (C) 2008-2011 Avail-TVN
 *
 * Written by Alex Izvorski <aizvorski@gmail.com> and Alex Giladi <alex.giladi@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "h264_stream.h"

/**
 Calculate the log base 2 of the argument, rounded up.
 Zero or negative arguments return zero
 Idea from http://www.southwindsgames.com/blog/2009/01/19/fast-integer-log2-function-in-cc/
 */
int intlog2(int x)
{
    int log = 0;
    if (x < 0) { x = 0; }
    while ((x >> log) > 0)
    {
        log++;
    }
    if (log > 0 && x == 1<<(log-1)) { log--; }
    return log;
}

/**
   Convert RBSP data to NAL data (Annex B format).
   The size of nal_buf must be 4/3 * the size of the rbsp_buf (rounded up) to guarantee the output will fit.
   If that is not true, output may be truncated and an error will be returned.
   If that is true, there is no possible error during this conversion.
   @param[in] rbsp_buf   the rbsp data
   @param[in] rbsp_size  pointer to the size of the rbsp data
   @param[in,out] nal_buf   allocated memory in which to put the nal data
   @param[in,out] nal_size  as input, pointer to the maximum size of the nal data; as output, filled in with the actual size of the nal data
   @return  actual size of nal data, or -1 on error
 */
// 7.3.1 NAL unit syntax
// 7.4.1.1 Encapsulation of an SODB within an RBSP
int rbsp_to_nal(const uint8_t* rbsp_buf, const int* rbsp_size, uint8_t* nal_buf, int* nal_size)
{
    int i;
    int j     = 1;
    int count = 0;

    if (*nal_size > 0) { nal_buf[0] = 0x00; } // zero out first byte since we start writing from second byte

    for ( i = 0; i < *rbsp_size ; i++ )
    {
        if ( j >= *nal_size )
        {
            // error, not enough space
            return -1;
        }

        if ( ( count == 2 ) && !(rbsp_buf[i] & 0xFC) ) // HACK 0xFC
        {
            nal_buf[j] = 0x03;
            j++;
            count = 0;
        }
        nal_buf[j] = rbsp_buf[i];
        if ( rbsp_buf[i] == 0x00 )
        {
            count++;
        }
        else
        {
            count = 0;
        }
        j++;
    }

    if (rbsp_buf[(*rbsp_size) -1] == 0x00) {
        nal_buf[j] = 0x03;
        j++;
    }

    *nal_size = j;
    return j;
}

/***************************** writing ******************************/

/**
 Write a NAL unit to a byte buffer.
 The NAL which is written out has a type determined by h->nal and data which comes from other fields within h depending on its type.
 @param[in,out]  h          the stream object
 @param[out]     buf        the buffer
 @param[in]      size       the size of the buffer
 @return                    the length of data actually written
 */
//7.3.1 NAL unit syntax
int 
write_nal_unit
(int nal_unit_type, int width, int height, VAProfile profile, 
VAPictureParameterBufferH264 *pic_param, 
VASliceParameterBufferH264 *slice_param, uint8_t* buf, int size)
{
    #define HEADER_SIZE 3
    int rbsp_size = size*3/4; // NOTE this may have to be slightly smaller (3/4 smaller, worst case) in order to be guaranteed to fit
    uint8_t* rbsp_buf = (uint8_t*)calloc(1, rbsp_size); // FIXME can use malloc?
    int nal_size = size - HEADER_SIZE;

    bs_t* b = bs_new(rbsp_buf, rbsp_size);

    switch ( nal_unit_type )
    {
        case NAL_UNIT_TYPE_SPS:
            write_seq_parameter_set_rbsp(width, height, profile, pic_param, b);
            break;

        case NAL_UNIT_TYPE_PPS:
            write_pic_parameter_set_rbsp(pic_param, slice_param, b);
            break;

        default:
            // here comes the reserved/unspecified/ignored stuff
            return 0;
    }


    if (bs_overrun(b)) { bs_free(b); free(rbsp_buf); return -1; }

    // now get the actual size used
    rbsp_size = bs_pos(b);

    int rc = rbsp_to_nal(rbsp_buf, &rbsp_size, buf + HEADER_SIZE, &nal_size);
    if (rc < 0) { bs_free(b); free(rbsp_buf); return -1; }

    bs_free(b);
    free(rbsp_buf);

    b = bs_new(buf, size);

    bs_write_u8(b, 0);
    bs_write_u8(b, 0);
    bs_write_u8(b, 1);

    bs_write_f(b,1, 0);
    bs_write_u(b,2, NAL_REF_IDC_PRIORITY_HIGHEST);
    bs_write_u(b,5, nal_unit_type);

    bs_free(b);

    return nal_size + HEADER_SIZE;
}


//7.3.2.1 Sequence parameter set RBSP syntax
void write_seq_parameter_set_rbsp(int width, int height, VAProfile profile, 
		VAPictureParameterBufferH264 *pic_param, bs_t* b)
{
    int mb_width = (width + 15) / 16;
    int mb_height = (height + 15) / 16;

    if( !pic_param->seq_fields.bits.frame_mbs_only_flag )
        mb_height = ( mb_height + 1 ) & ~1;

    int profile_idc;
    switch (profile) {
        case VAProfileH264Baseline:
            profile_idc = H264_PROFILE_BASELINE;
            break;
        case VAProfileH264Main:
            profile_idc = H264_PROFILE_MAIN;
            break;
        case VAProfileH264High:
        default:
            profile_idc = H264_PROFILE_HIGH;
            break;
    }
    bs_write_u8(b, profile_idc);
    bs_write_u1(b, profile_idc == H264_PROFILE_BASELINE);//sps->constraint_set0_flag);
    bs_write_u1(b, profile_idc <= H264_PROFILE_MAIN);//sps->constraint_set1_flag);
    bs_write_u1(b, 0);//sps->constraint_set2_flag);
    bs_write_u1(b, 0);//sps->constraint_set3_flag);
    bs_write_u(b, 4, 0);  /* reserved_zero_4bits */
    bs_write_u8(b, 40);//sps->level_idc);
    bs_write_ue(b, 0);//sps->seq_parameter_set_id);
    if(profile_idc >= H264_PROFILE_HIGH)
    {
        bs_write_ue(b, 1);//sps->chroma_format_idc);
        bs_write_ue(b, 0);//sps->bit_depth_luma_minus8);
        bs_write_ue(b, 0);//sps->bit_depth_chroma_minus8);
        bs_write_u1(b, 0);//sps->qpprime_y_zero_transform_bypass_flag);
        bs_write_u1(b, 0);//sps->seq_scaling_matrix_present_flag);
    }

    bs_write_ue(b, pic_param->seq_fields.bits.log2_max_frame_num_minus4);
    bs_write_ue(b, pic_param->seq_fields.bits.pic_order_cnt_type);
    if( pic_param->seq_fields.bits.pic_order_cnt_type == 0 )
    {
        bs_write_ue(b, 
		pic_param->seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4);
    }
    else if( pic_param->seq_fields.bits.pic_order_cnt_type == 1 )
    {
        bs_write_u1(b, 
		pic_param->seq_fields.bits.delta_pic_order_always_zero_flag);
        bs_write_se(b, 0);//sps->offset_for_non_ref_pic);
        bs_write_se(b, 0);//sps->offset_for_top_to_bottom_field);
        bs_write_ue(b, 0);//sps->num_ref_frames_in_pic_order_cnt_cycle);
        //for( i = 0; i < sps->num_ref_frames_in_pic_order_cnt_cycle; i++ )
        //{
        //    bs_write_se(b, sps->offset_for_ref_frame[ i ]);
        //}
    }
    bs_write_ue(b, pic_param->num_ref_frames);
    bs_write_u1(b, 0);//sps->gaps_in_frame_num_value_allowed_flag);
    bs_write_ue(b, mb_width-1);//sps->pic_width_in_mbs_minus1);
    bs_write_ue(b, 
	(mb_height >> !pic_param->seq_fields.bits.frame_mbs_only_flag) -1);//sps->pic_height_in_map_units_minus1);
    bs_write_u1(b, pic_param->seq_fields.bits.frame_mbs_only_flag);
    if( !pic_param->seq_fields.bits.frame_mbs_only_flag )
    {
        bs_write_u1(b, 
		pic_param->seq_fields.bits.mb_adaptive_frame_field_flag);
    }
    bs_write_u1(b, pic_param->seq_fields.bits.direct_8x8_inference_flag);

    int crop_width = mb_width*16 - width;
    int crop_height = (mb_height*16 - height) 
	    >> !pic_param->seq_fields.bits.frame_mbs_only_flag;
    int crop = crop_width || crop_height;
    bs_write_u1(b, crop);//sps->frame_cropping_flag);
    if( crop )
    {
        bs_write_ue(b, 0);
        bs_write_ue(b, crop_width);
        bs_write_ue(b, 0);
        bs_write_ue(b, crop_height);
    }
    bs_write_u1(b, 0);//sps->vui_parameters_present_flag);
    //if( sps->vui_parameters_present_flag )
    //{
    //    write_vui_parameters(sps, b);
    //}
    write_rbsp_trailing_bits(b);
}

//7.3.2.1.1 Scaling list syntax
void write_scaling_list(bs_t* b, uint8_t* scalingList, int sizeOfScalingList, int useDefaultScalingMatrixFlag )
{
    int j;

    int lastScale = 8;
    int nextScale = 8;

    for( j = 0; j < sizeOfScalingList; j++ )
    {
        int delta_scale;

        if( nextScale != 0 )
        {
            // FIXME will not write in most compact way - could truncate list if all remaining elements are equal
            nextScale = scalingList[ j ];

            if (useDefaultScalingMatrixFlag)
            {
                nextScale = 0;
            }

            delta_scale = (nextScale - lastScale) % 256 ;
            bs_write_se(b, delta_scale);
        }

        lastScale = scalingList[ j ];
    }
}

//7.3.2.2 Picture parameter set RBSP syntax
void 
write_pic_parameter_set_rbsp(VAPictureParameterBufferH264 *pic_param, 
	VASliceParameterBufferH264 *slice_param, bs_t* b)
{
    bs_write_ue(b, 0);//pps->pic_parameter_set_id);
    bs_write_ue(b, 0);//pps->seq_parameter_set_id);
    bs_write_u1(b, pic_param->pic_fields.bits.entropy_coding_mode_flag);
    bs_write_u1(b, pic_param->pic_fields.bits.pic_order_present_flag);
    bs_write_ue(b, 0);//pps->num_slice_groups_minus1);

    /* FIXME */
    bs_write_ue(b, slice_param->num_ref_idx_l0_active_minus1);
    bs_write_ue(b, slice_param->num_ref_idx_l1_active_minus1);
    bs_write_u1(b, pic_param->pic_fields.bits.weighted_pred_flag);
    bs_write_u(b,2, pic_param->pic_fields.bits.weighted_bipred_idc);
    bs_write_se(b, pic_param->pic_init_qp_minus26);
#if 0
    bs_write_se(b, 0);//pps->pic_init_qs_minus26);
#else
    bs_write_se(b, pic_param->pic_init_qs_minus26);
#endif
    bs_write_se(b, pic_param->chroma_qp_index_offset);
    bs_write_u1(b, pic_param->pic_fields.bits.
		    deblocking_filter_control_present_flag);
    bs_write_u1(b, pic_param->pic_fields.bits.constrained_intra_pred_flag);
    bs_write_u1(b, pic_param->pic_fields.bits.redundant_pic_cnt_present_flag);

    if ( 1 )//pps->_more_rbsp_data_present )
    {
        bs_write_u1(b, pic_param->pic_fields.bits.transform_8x8_mode_flag);
        bs_write_u1(b, 0);//pps->pic_scaling_matrix_present_flag);

        bs_write_se(b, pic_param->second_chroma_qp_index_offset);
    }

    write_rbsp_trailing_bits(b);
}

//7.3.2.11 RBSP trailing bits syntax
void write_rbsp_trailing_bits(bs_t* b)
{
    int rbsp_stop_one_bit = 1;
    int rbsp_alignment_zero_bit = 0;

    bs_write_f(b,1, rbsp_stop_one_bit); // equal to 1
    while( !bs_byte_aligned(b) )
    {
        bs_write_f(b,1, rbsp_alignment_zero_bit); // equal to 0
    }
}

#if 0
#include <stdio.h>
int main(int ac, char **av) {
    VdpPictureInfoH264 info;
    info.frame_num = 0;
    info.field_pic_flag = 0;
    info.bottom_field_flag = 0;
    info.num_ref_frames = 3;
    info.mb_adaptive_frame_field_flag = 0;
    info.constrained_intra_pred_flag = 0;
    info.weighted_pred_flag = 0;
    info.weighted_bipred_idc = 0;
    info.frame_mbs_only_flag = 0;
    info.transform_8x8_mode_flag = 0;
    info.chroma_qp_index_offset = 0;
    info.second_chroma_qp_index_offset = 0;
    info.pic_init_qp_minus26 = 0;
    info.num_ref_idx_l0_active_minus1 = 0;
    info.num_ref_idx_l1_active_minus1 = 0;
    info.log2_max_frame_num_minus4 = 5;
    info.pic_order_cnt_type = 0;
    info.log2_max_pic_order_cnt_lsb_minus4 = 5;
    info.delta_pic_order_always_zero_flag = 0;
    info.direct_8x8_inference_flag = 0;
    info.entropy_coding_mode_flag = 0;
    info.pic_order_present_flag = 0;
    info.redundant_pic_cnt_present_flag = 0;

    uint8_t buf[512];
    int size;
    FILE *f = fopen("test", "w");
    size = write_nal_unit(NAL_UNIT_TYPE_SPS, 720, 576, VDP_DECODER_PROFILE_H264_HIGH, &info, buf, sizeof(buf));
    fwrite(buf, 1, size, f);
    fclose(f);
}
#endif
