/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: urpc handshaker information cmd
 * Create: 2025-07-11
 */

#ifndef HANDSHAKER_INFO_H
#define HANDSHAKER_INFO_H

#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum urpc_handshaker_cmd_id {
    URPC_HANDSHAKER_CMD_ID_ALL_HANDSHAKER,
    URPC_HANDSHAKER_CMD_ID_BY_TASK_ID,
    URPC_HANDSHAKER_CMD_ID_MAX,
} urpc_handshaker_cmd_id_t;

typedef struct urpc_handshaker_cmd_input {
    uint32_t channel_id;
    uint32_t task_id;
} urpc_handshaker_cmd_input_t;

int handshaker_cmd_init(void);
void handshaker_cmd_uninit(void);

#ifdef __cplusplus
}
#endif

#endif