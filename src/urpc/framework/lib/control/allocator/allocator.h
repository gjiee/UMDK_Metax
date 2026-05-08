/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: define allocator function
 * Create: 2024-1-1
 */
#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include "urpc_framework_types.h"
#include "queue.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEFAULT_SGE_HEAD_SIZE       (sizeof(struct urpc_sge))
#define DEFAULT_SGE_SIZE            128
#define DEFAULT_LARGE_SGE_SIZE      2048
#define DEFAULT_SGE_NUM             (64 * 128 * 2)
#define DEFAULT_LARGE_SGE_NUM       8192
#define DEFAULT_LARGE_SGE_MAX_SIZE  8192

typedef struct default_allocator_cfg {
    bool need_large_sge;
    uint32_t large_sge_size;
} default_allocator_cfg_t;

urpc_allocator_t *default_allocator_get(void);

int urpc_default_allocator_init(default_allocator_cfg_t *cfg);
void urpc_default_allocator_uninit(void);

#ifdef __cplusplus
}
#endif

#endif
