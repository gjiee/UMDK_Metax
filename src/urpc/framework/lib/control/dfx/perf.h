/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: urpc statistics cmd
 * Create: 2024-11-21
 */

#ifndef URPC_PERF_H
#define URPC_PERF_H

#include <stdint.h>
#include <stdbool.h>
#include "urpc_framework_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define URPC_PERF_QUANTILE_MAX_NUM (8u)

typedef enum urpc_perf_cmd_id {
    URPC_PERF_CMD_ID_START,
    URPC_PERF_CMD_ID_STOP,
    URPC_PERF_CMD_ID_CLEAR,
    URPC_PERF_CMD_MAX,
} urpc_perf_cmd_id_t;

typedef struct urpc_perf_record {
    struct {
        double mean;
        double std_m2;
        uint64_t min;
        uint64_t max;
        uint64_t cnt;
        // bucket[URPC_PERF_QUANTILE_MAX_NUM] stores the number of samples that exceed the max quantile_thresh
        uint64_t bucket[URPC_PERF_QUANTILE_MAX_NUM + 1];
    } type_record[PERF_RECORD_POINT_MAX];
    bool is_used;
} urpc_perf_record_t;

int urpc_perf_cmd_init(void);
void urpc_perf_cmd_uninit(void);

void urpc_perf_record_alloc(void);
void urpc_dp_thread_run_once(void);

uint64_t urpc_perf_record_begin(urpc_perf_record_point_t point);
void urpc_perf_record_end(urpc_perf_record_point_t point, uint64_t start);

#ifdef __cplusplus
}
#endif

#endif
