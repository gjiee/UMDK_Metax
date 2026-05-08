/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: datapath function
 */
#ifndef DP_H
#define DP_H

#include <stdint.h>
#include <semaphore.h>
#include "urpc_framework_types.h"
#include "queue.h"
#include "channel.h"
#include "protocol.h"
#include "allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum URPC_SERVER_STATE {
    URPC_SERVER_STAT_INIT,
    URPC_SERVER_STAT_REQ_RECVED,
    URPC_SERVER_STAT_RSP_SENT
} URPC_SERVER_STATE_T;

#define REQ_ACKED                      (1)         // receive ack
#define REQ_RSPED_WAIT_ACK             (1 << 1)    // receive rsp

typedef struct tx_ctx {
    void *user_ctx;                             // context for user set
    urpc_sge_t *sges;
    urpc_sge_t *rsps;
    uint64_t l_qh;
    uint32_t sge_num;
    uint32_t normal_sge_num;                    // not include dma sges
    uint32_t normal_len;                        // not include dma sges
    uint32_t rsps_sge_num;
    uint32_t rsp_valid_total_size;              // the total size of valid rsps sge
    uint32_t channel_id;
    uint32_t req_id;
    uint32_t wr_idx;                            // currently, only use in read frag scene
    enum urpc_msg_type msg_type;
    uint16_t call_mode;
    uint8_t cur_status;
    uint8_t func_defined;
    /* Ensure that the tx_ctx is safely released after ALL free event(TX CQE, ACK, RSP) have all been completed. */
    uint8_t free_events_completed : 1;
    /*
    * When set to true, indicates that ctx->sges has been either released or handed over to the upper layer.
    * Further flush operations must skip this context to avoid double-free.
    */
    uint8_t sge_handover_completed : 1;
    /* indicate whether is internal tx operation, if so, not report message to user. */
    uint8_t internal_message : 1;
    uint8_t reserved : 5;
} tx_ctx_t;

typedef struct rx_user_ctx {
    urpc_sge_t *sges;
    uint32_t sge_num;
    rq_ctx_t *rq_ctx;
} rx_user_ctx_t;

typedef struct req_ctx {
    queue_t *q_src;
    uint32_t req_id;
    uint32_t client_chid;
    uint32_t server_chid;
    URPC_SERVER_STATE_T state;
    uint32_t stream_psn;
    uint32_t is_stream : 1;
    uint32_t is_stream_last : 1;
    uint32_t ack : 1;
    uint32_t rsvd : 29;
} req_ctx_t;

typedef enum ext_call_pos {
    EXT_CALL_POS_RX_REQ = 0,
    EXT_CALL_POS_TX_READ = 1,
    EXT_CALL_POS_TX_READ_ERR = 2,
    EXT_CALL_POS_RX_RSP = 3,
} ext_call_pos_t;

typedef struct ext_call_ctx {
    uint8_t pos;
    uint8_t func_defined;
    uint32_t chid;
    queue_t *queue;
    queue_msg_t *q_msg;
} ext_call_ctx_t;

typedef struct ext_ops {
    uint8_t func_defined;
    int (*rx_req_process)(ext_call_ctx_t *ext_call_ctx, req_ctx_t **req_ctx, urpc_poll_msg_t *msg);
    int (*tx_read_process)(ext_call_ctx_t *ext_call_ctx, urpc_poll_msg_t *msg);
    int (*tx_read_err_process)(ext_call_ctx_t *ext_call_ctx, urpc_poll_msg_t *msg);
    void (*ext_proto_fill_extra_info)(urpc_req_head_t *req_head, queue_local_t *l_queue, queue_remote_t *r_queue);
    bool (*timeout_create_process)(urpc_call_wr_t *wr, req_entry_t *entry);
    int (*encrypt)(urpc_hdr_type_t hdr_type, urpc_cipher_t *cipher_opt, urpc_sge_t *sge,
        uint32_t sge_num);
    int (*decrypt)(urpc_hdr_type_t hdr_type, urpc_cipher_t *cipher_opt, urpc_sge_t *sge,
        uint32_t sge_num);
    int (*stream_send_process)(void *user_ctx, uint32_t req_id, uint32_t server_node_index, uint32_t timeout);
    int (*stream_send_fail_process)(urpc_call_option_t *option);
    int (*rx_rsp_process)(ext_call_ctx_t *ext_call_ctx, urpc_poll_msg_t *msg);
} ext_ops_t;

typedef enum ext_call_result {
    EXT_CALL_PROCESS_DONE_NONEED_REPORT = 0, // msg have been process in ext module, no need report to user.
    EXT_CALL_PROCESS_DONE_AND_REPORT,        // msg have been process in ext module, need report to user.
    EXT_CALL_PROCESS_BACK_TO_NORMAL,         // datapath need process after ext module.
} ext_call_result_t;

typedef struct sync_req_cb_arg {
    int err;
    sem_t rsp_sem;
    volatile int rsp_received;
} sync_req_cb_arg_t;

int post_rx_buf(uint64_t qh, uint32_t post_num, uint64_t one_buffer_size);
queue_t *urpc_get_local_queue(urpc_channel_info_t *channel, urpc_call_option_t *option);
queue_t *urpc_get_remote_queue(urpc_channel_info_t *channel, urpc_call_option_t *option);
void tx_ctx_try_put(tx_ctx_t *ctx);
// export for dp_ext
int urpc_func_call_early_rsp(uint32_t server_chid, urpc_call_wr_t *wr, urpc_call_option_t *option);

void ext_process_register_ops(ext_ops_t *ext_ops);
bool ext_func_defined_validate(uint8_t func_defined);
void release_save_dma_sge(urpc_sge_t *args, uint32_t *sge_num, urpc_allocator_option_t *option, queue_t *queue);

queue_t *get_server_channel_real_r_queue(uint32_t server_chid, queue_t *l_queue, queue_t *q_src);
void put_server_channel_real_r_queue(uint32_t server_chid);

void urpc_poll_process(urpc_poll_msg_t msg[], uint32_t msg_num, uint64_t l_qh);

void urpc_process_rsp_callback(req_entry_t *entry, int err_code);

void process_cr_err(uint64_t qh, uint32_t err_code);

#ifdef __cplusplus
}
#endif

#endif