/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* This file implements the operation of memory vpu used.
 * Some implements are empty for remaining interface transfered from
 * other platforms, remove these empty some day in the future, TODO
 */

#include "vpu_mem.h"

#include <malloc.h>
#include <memory.h>

int32_t vpu_mem_link() {
  return 0;
}

int32_t VPUMallocLinear(VPUMemLinear_t* p, uint32_t size) {
  p->pbase = (uint8_t*)calloc(1, size + 127);
  if(NULL == p->pbase){
    return -1;
  }
  p->phy_addr = ((uint32_t)p->pbase) & (~63);
  p->phy_addr += 64;
  p->vir_addr = (uint32_t*)p->phy_addr;
  p->vir_addr[-1] = 1;
  p->size = size;
  p->phy_addr = 0x0; // used for calculate the offset.
  return 0;
}

int32_t VPUFreeLinear(VPUMemLinear_t* p) {
  if (p->vir_addr[-1] > 1) {
    p->vir_addr[-1]--;
    return 0;
  }
  free(p->pbase);
  memset(p, 0, sizeof(VPUMemLinear_t));
  return 0;
}

int32_t VPUMemDuplicate(VPUMemLinear_t* dst, VPUMemLinear_t* src) {
  *dst = *src;
  src->vir_addr[-1]++;
  return 0;
}

int32_t VPUMemLink(VPUMemLinear_t* p) {
  return 0;
}

int32_t VPUMemFlush(VPUMemLinear_t* p) {
  return 0;
}

uint32_t VPUMemPhysical(VPUMemLinear_t* p) {
  return 0;
}

uint32_t* VPUMemVirtual(VPUMemLinear_t* p) {
  return 0;
}

int32_t VPUMemInvalidate(VPUMemLinear_t* p) {
  return 0;
}

int32_t VPUMemClean(VPUMemLinear_t* p) {
  return 0;
}
