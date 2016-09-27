/*
 * Copyright 2015 - 2016 Rockchip Electronics Co. LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef _MPP_ENC_JPEG_GEN_HDR_H_
#include "common.h"
#include "rockchip_driver.h"

void jpege_bits_init(void **ctx);

void jpege_bits_deinit(void *ctx);

void jpege_bits_setup(void *ctx, uint8_t * buf, int32_t size);

void jpege_bits_align_byte(void *ctx);

uint8_t *jpege_bits_get_buf(void *ctx);

int32_t jpege_bits_get_bitpos(void *ctx);

int32_t jpege_bits_get_bytepos(void *ctx);

int32_t write_jpeg_header
(void *bits,
 struct object_surface *obj_surface,
 const uint8_t * qtables[2]);
#endif
