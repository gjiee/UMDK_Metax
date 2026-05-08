/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: control plane definition
 */

#ifndef CP_H
#define CP_H

#include <pthread.h>
#include "channel.h"
#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

// max ctrl msg size 64KB
#define CTRL_MSG_MAX_SIZE (1 << 16)
#define URPC_MANAGE_FEATURE_FLAG (URPC_FEATURE_KEEPALIVE)
#define URPC_TIMER_FEATURE_FLAG (URPC_FEATURE_TIMEOUT | URPC_FEATURE_KEEPALIVE)

typedef int (*urpc_ext_channel_create_cb_t)(uint32_t chid);
typedef void (*urpc_ext_channel_destroy_cb_t)(uint32_t chid);

typedef struct urpc_ctx {
    urpc_role_t role;
    uint32_t feature;
    uint16_t device_class;
    uint16_t sub_class;
    urpc_keepalive_config_t keepalive_cfg;
    urpc_config_t cfg;
    bool skip_post_rx;  // indicates whether to skip post rx when poll to reduce lantency
} urpc_ctx_t;

bool is_server_support_quick_reply(void);
bool is_feature_enable(uint32_t feature);

uint64_t queue_create(urpc_queue_trans_mode_t trans_mode, urpc_qcfg_create_t *cfg, uint16_t flag);
void parse_server_to_host(urpc_server_info_t *server, urpc_host_info_t *server_host, urpc_host_info_t *local_host);
bool urpc_channel_connect_option_set(
    urpc_host_info_t *server, urpc_channel_connect_option_t *in, urpc_channel_connect_option_t *out);
urpc_role_t urpc_role_get(void);
uint64_t urpc_keepalive_attr_get(void);
int urpc_ctrl_msg_process(urpc_ctrl_msg_type_t msg_type, urpc_ctrl_msg_t *ctrl_msg);
uint16_t urpc_device_class_get(void);
uint16_t urpc_sub_class_get(void);
void urpc_skip_post_rx_set(bool is_skip);
bool check_queue_in_channel(urpc_channel_info_t *channel, uint64_t qh);
int urpc_tseg_unimport(uint32_t server_chid, uint32_t token_id, uint32_t token_value);
int urpc_tseg_import(uint32_t server_chid, xchg_mem_info_t *mem_info);
int urpc_mem_unimport(uint32_t server_chid, uint32_t token_id, uint32_t token_value);
int urpc_mem_import(uint32_t server_chid, xchg_mem_info_t *mem_info);
#ifdef __cplusplus
}
#endif

#endif