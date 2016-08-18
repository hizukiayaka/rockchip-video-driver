/*
 * Copyright (C) 2013 Rockchip Open Libvpu Project
 * Author: Herman Chen chm@rock-chips.com
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

#ifndef __VPU_MEM_H__
#define __VPU_MEM_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

typedef struct VPUMem {
  uint32_t  phy_addr;
  uint32_t* vir_addr;
  uint32_t  size;
  int32_t  offset;
  uint8_t* pbase;
} VPUMemLinear_t;

#define VPU_MEM_IS_NULL(p)          ((p)->offset < 0)

/* SW/HW shared memory */
int32_t VPUMallocLinear(VPUMemLinear_t* p, uint32_t size);
int32_t VPUFreeLinear(VPUMemLinear_t* p);
int32_t VPUMemDuplicate(VPUMemLinear_t* dst, VPUMemLinear_t* src);
int32_t VPUMemLink(VPUMemLinear_t* p);
int32_t VPUMemFlush(VPUMemLinear_t* p);
int32_t VPUMemClean(VPUMemLinear_t* p);
int32_t VPUMemInvalidate(VPUMemLinear_t* p);

#ifdef __cplusplus
}

#endif

#endif /* __VPU_MEM_H__ */

