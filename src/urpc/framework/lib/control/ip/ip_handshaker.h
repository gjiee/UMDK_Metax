/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: define ip handshaker function
 */
#ifndef IP_HANDSHAKER_H
#define IP_HANDSHAKER_H

#include <semaphore.h>
#include <stdbool.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "urpc_epoll.h"
#include "urpc_socket.h"
#include "channel.h"
#include "cp_vers_compat.h"
#include "protocol.h"
#include "task_engine.h"
#include "transport.h"

#ifdef __cplusplus
extern "C" {
#endif


#define URPC_CTL_RELEATED_CTX_MAX_NUM           1           // keepalive advise ctx

typedef void (*async_callback_t)(void *ctx, int result);

struct ip_ctl_handle;
struct ip_handshaker_ctx;

typedef int (*ip_ctl_send_async)(
    struct ip_handshaker_ctx *ctl_hdl, urpc_epoll_event_func_t func, void *data, size_t data_size);
typedef int (*ip_ctl_recv_async)(
    struct ip_handshaker_ctx *ctl_hdl, urpc_epoll_event_func_t func, void *data, size_t data_size);

typedef struct ip_ctl_handle {
    int fd;
    SSL *ssl;
    socket_addr_t tcp_addr;  // Server Address
    size_t offset;
    ip_ctl_send_async send_async;
    ip_ctl_recv_async recv_async;
    data_transmission_phase_t phase;
    urpc_epoll_event_t *event;
    bool is_non_block;
} ip_ctl_handle_t;

typedef struct ip_handshaker_client {
    uint64_t server_keepalive_attr;
    urpc_channel_info_t *channel;
    urpc_channel_info_t *manage_channel;
    urpc_endpoints_t endpoints;
    urpc_instance_key_t server_instance;
    uint32_t server_chid;
    uint32_t server_manage_chid;
    void *ctx;
    int timeout;  // - 1:indicates never timed out
    void (*func)(void *ctx, int result);
    bool manage_created;
    bool keepalive_created;
    bool client_inited;
} ip_handshaker_client_t;

typedef struct ip_handshaker_server {
    uint64_t client_keepalive_attr;
    uint32_t chid;
    uint32_t mapped_id;
    uint32_t manage_chid;
    uint32_t manage_mapped_chid;
    urpc_instance_key_t client_instance;
    uint32_t client_chid;
    uint32_t client_manage_chid;
    uint32_t ctl_opcode;
    uint32_t inner_step;
    uint8_t client_version;
    bool manage_created;
    bool ctrl_code_received;
    bool keepalive_created;
    bool channel_attached;
} ip_handshaker_server_t;

typedef struct negotiate_ctx {
    bool inited;
    urpc_neg_msg_v1_t neg_msg_v1;
    uint32_t negotiate_step;
} negotiate_ctx_t;

typedef struct channel_info_ctx {
    bool inited;
    urpc_attach_msg_v1_t attach_msg_v1_send;
    urpc_attach_msg_v1_t attach_msg_v1_recv;
} channel_info_ctx_t;

typedef struct func_info {
    bool inited;
    void *func;
    uint32_t len;
} func_info_t;

typedef struct mem_info {
    void *mem;
    uint32_t len;
} mem_info_t;

typedef struct channel_attach_info {
    uint32_t server_chid;
    urpc_chinfo_t channel_info;
} channel_attach_info_t;

typedef struct ip_handshaker_ctx {
    urpc_async_task_ctx_t base_task;
    urpc_list_t node;
    ip_ctl_handle_t ctl_hdl;
    urpc_ctrl_msg_t dummy_ctrl_msg;
    urpc_ctrl_msg_t *ctrl_msg; // if user ctrl_msg is NULL, this is ctrl_msg ptr to dummy_ctrl_msg
    union {
        ip_handshaker_client_t client;
        ip_handshaker_server_t server;
    };
    ip_ctl_capability_t cap;
    urpc_ctl_head_t head;
    negotiate_ctx_t neg_ctx;
    channel_info_ctx_t chnl_ctx;
    channel_attach_info_t attach_info;
    urpc_detach_msg_v1_t detach_msg_v1;
    batch_queue_import_ctx_t batch_import_ctx;
    uint64_t timestamp_ms;
    char *ctrl_msg_recv;
    uint32_t current_step;
    func_info_t func_info;
    mem_info_t mem_info;
    int result;
    task_workflow_type_t type;
    bool head_inited;
    bool is_server;
} ip_handshaker_ctx_t;

int ip_check_listen_cfg(const urpc_host_info_t *server);
bool urpc_server_info_ip_compare(const urpc_host_info_t *server, const urpc_host_info_t *target);

int ip_handshaker_init(urpc_host_info_t *host, void *user_ctx);
void ip_handshaker_uninit(void);

void batch_import_ctx_free(batch_queue_import_ctx_t *ctx);
int client_ctx_init(ip_handshaker_ctx_t *ctx, urpc_ctrl_msg_t *ctrl_msg, urpc_channel_info_t *channel,
    urpc_host_info_t *server, urpc_host_info_t *local);
int create_negotiate_msg(ip_handshaker_ctx_t *ctx);
bool negotiate_msg_validate(ip_handshaker_ctx_t *ctx, urpc_ctl_head_t *recv_head);
uint32_t ctrl_opcode_get(urpc_ctrl_msg_type_t msg_type);
int urpc_ctrl_msg_process(urpc_ctrl_msg_type_t msg_type, urpc_ctrl_msg_t *ctrl_msg);
urpc_chmsg_v1_t *user_queue_info_xchg_get(ip_handshaker_ctx_t *ctx);
urpc_chmsg_v1_t *manage_queue_info_xchg_get(ip_handshaker_ctx_t *ctx);
void ip_handshaker_ctx_server_uninit(ip_handshaker_ctx_t *ctx);
int client_channel_create_remote_queue(
    urpc_channel_info_t *channel, urpc_endpoints_t *endpoints, urpc_chmsg_v1_t *chmsg, bool is_all_queue);
int init_cipher_for_server_node(urpc_channel_info_t *channel, urpc_host_info_t *server, crypto_key_t *crypto_key);
int server_attach_channel_process(ip_handshaker_ctx_t *ctx);
void ip_handshaker_ctx_server_param_set(
    ip_handshaker_ctx_t *ctx, urpc_server_channel_info_t *channel, urpc_instance_key_t *client);

#ifdef __cplusplus
}
#endif

#endif