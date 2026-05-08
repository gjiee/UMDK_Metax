/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: realize ip handshaker function
 */

#include <fcntl.h>
#include <netinet/tcp.h>
#include <string.h>

#include "allocator.h"
#include "async_event.h"
#include "client_manage_channel.h"
#include "cp.h"
#include "crypto.h"
#include "keepalive.h"
#include "resource_release.h"
#include "server_manage_channel.h"
#include "urpc_dbuf_stat.h"
#include "urpc_epoll.h"
#include "urpc_lib_log.h"
#include "urpc_manage.h"
#include "urpc_socket.h"
#include "func.h"

#include "ip_handshaker.h"

#define LISTEN_BACKLOG 128
#define MAX_MANAGER_QUEUE_NUM 1
#define HANDSHAKE_CHECK_CYCLE_MS 1
#define SERVER_TIMEOUT_MS 300000 // 5min
#define MAX_HANDSHAKER_CTX_COUNT 8192


typedef struct ip_ctl_ctx {
    int listen_fd;
    urpc_epoll_event_t *lev;
} ip_ctl_ctx_t;

typedef struct urpc_ip_handshaker_ctx_manager {
    urpc_list_t list;
    uint32_t total_cnt;
    pthread_mutex_t list_mutex;
} urpc_ip_handshaker_ctx_manager_t;

static ip_ctl_ctx_t *g_urpc_ip_ctl_ctx;

static void import_ctx_free(queue_import_ctx_t *ctx)
{
    queue_import_async_info_t *cur = NULL;
    queue_import_async_info_t *next = NULL;
    URPC_LIST_FOR_EACH_SAFE(cur, next, node, &ctx->list)
    {
        urpc_list_remove(&cur->node);
        urpc_dbuf_free(cur);
    }
    urpc_dbuf_free(ctx->event);
    ctx->event = NULL;
    if (ctx->notifier_handle != URPC_INVALID_HANDLE &&
        ctx->provider->ops->destroy_notifier(ctx->notifier_handle) != URPC_SUCCESS) {
        URPC_LIB_LOG_ERR("destroy notifier failed\n");
    }
    ctx->notifier_handle = URPC_INVALID_HANDLE;
    urpc_dbuf_free(ctx);
}

void batch_import_ctx_free(batch_queue_import_ctx_t *ctx)
{
    queue_import_ctx_t *cur = NULL;
    queue_import_ctx_t *next = NULL;
    URPC_LIST_FOR_EACH_SAFE(cur, next, node, &ctx->list)
    {
        urpc_list_remove(&cur->node);
        import_ctx_free(cur);
    }
}

static void ctx_init(ip_handshaker_ctx_t *ctx)
{
    ctx->ctl_hdl.fd = -1;

    ctx->cap.dp_encrypt = crypto_is_dp_ssl_enabled() ? URPC_TRUE : URPC_FALSE;
    ctx->cap.keepalive = is_feature_enable(URPC_FEATURE_KEEPALIVE) ? URPC_TRUE : URPC_FALSE;
    ctx->cap.func_info_enabled = is_feature_enable(URPC_FEATURE_GET_FUNC_INFO) ? URPC_TRUE : URPC_FALSE;
    ctx->cap.multiplex_enabled = is_feature_enable(URPC_FEATURE_MULTIPLEX) ? URPC_TRUE : URPC_FALSE;
    urpc_list_init(&ctx->batch_import_ctx.list);
}

int client_ctx_init(ip_handshaker_ctx_t *ctx, urpc_ctrl_msg_t *ctrl_msg, urpc_channel_info_t *channel,
    urpc_host_info_t *server, urpc_host_info_t *local)
{
    ctx_init(ctx);
    urpc_async_task_ctx_t *base_task = &ctx->base_task;
    if (server != NULL) {
        base_task->endpoints.server = *server;
        ctx->client.endpoints.server = *server;
    }

    ctx->timestamp_ms = UINT64_MAX;
    ctx->client.timeout = -1;
    ctx->client.channel = channel;

    if (local != NULL) {
        base_task->endpoints.local = *local;
        base_task->endpoints.bind_local = URPC_TRUE;
        ctx->client.endpoints.local = *local;
        ctx->client.endpoints.bind_local = URPC_TRUE;
    }

    if (ctrl_msg == NULL) {
        ctx->ctrl_msg = &ctx->dummy_ctrl_msg;
    } else {
        memcpy(&ctx->dummy_ctrl_msg, ctrl_msg, sizeof(urpc_ctrl_msg_t));
        ctx->dummy_ctrl_msg.msg_max_size = CTRL_MSG_MAX_SIZE;
        ctx->dummy_ctrl_msg.msg = urpc_dbuf_malloc(URPC_DBUF_TYPE_CP, CTRL_MSG_MAX_SIZE);
        if (ctx->dummy_ctrl_msg.msg == NULL) {
            URPC_LIB_LOG_ERR("malloc ctrl msg buffer failed\n");
            return URPC_FAIL;
        }
        if (ctrl_msg->msg_size > 0) {
            if (ctrl_msg->msg_size > CTRL_MSG_MAX_SIZE) {
                urpc_dbuf_free(ctx->dummy_ctrl_msg.msg);
                ctx->dummy_ctrl_msg.msg = NULL;
                URPC_LIB_LOG_ERR("msg buffer length is too long\n");
                return URPC_FAIL;
            }
            memcpy(ctx->dummy_ctrl_msg.msg, ctrl_msg->msg, ctrl_msg->msg_size);
        }
        ctx->ctrl_msg = &ctx->dummy_ctrl_msg;
    }
    return URPC_SUCCESS;
}

void ip_handshaker_ctx_server_param_set(
    ip_handshaker_ctx_t *ctx, urpc_server_channel_info_t *channel, urpc_instance_key_t *client)
{
    ctx->server.chid = channel->id;
    ctx->server.mapped_id = channel->mapped_id;
    ctx->server.client_instance = *client;
    ctx->server.manage_chid = channel->manage_chid;
    ctx->server.manage_mapped_chid = channel->manage_mapped_id;
}

void ip_handshaker_ctx_server_uninit(ip_handshaker_ctx_t *ctx)
{
    uint32_t server_chid = ctx->server.chid;
    uint32_t manage_chid = URPC_INVALID_ID_U32;
    uint32_t ref_cnt = 0;
    if (ctx->server.manage_created) {
        manage_chid = ctx->server.manage_chid;
        ref_cnt = server_manage_channel_put(&ctx->server.client_instance, false, false);
        if (ref_cnt == 0) {
            (void)server_channel_free(ctx->server.manage_chid, true);
        }
    }
    URPC_LIB_LOG_INFO("uninit server channel[%u], manage channel[%u], ref_cnt[%u]\n",
        server_chid, manage_chid, ref_cnt);
    ctx->server.manage_chid = URPC_INVALID_ID_U32;
    // server channel is allocated automatically, so release here
    urpc_server_channel_info_t *server_channel = server_channel_get_with_rw_lock(ctx->server.chid, true);
    if (server_channel != NULL) {
        server_channel->manage_chid = URPC_INVALID_ID_U32;
        (void)pthread_rwlock_unlock(&server_channel->rw_lock);
    }
    (void)server_channel_free(ctx->server.chid, true);
}

int ip_check_listen_cfg(const urpc_host_info_t *server)
{
    socket_addr_t addr;
    if (server->host_type == HOST_TYPE_IPV4) {
        if (inet_pton(AF_INET, server->ipv4.ip_addr, &(addr.in.sin_addr)) != 1) {
            URPC_LIB_LOG_ERR("ip address for listening is invalid\n");
            return URPC_FAIL;
        }

        if (addr.in.sin_addr.s_addr == htonl(INADDR_ANY)) {
            URPC_LIB_LOG_ERR("listening on 0.0.0.0 is invalid\n");
            return URPC_FAIL;
        }

        return URPC_SUCCESS;
    }

    if (inet_pton(AF_INET6, server->ipv6.ip_addr, &(addr.in6.sin6_addr)) != 1) {
        URPC_LIB_LOG_ERR("ip address for listening is invalid\n");
        return URPC_FAIL;
    }

    if ((memcmp(addr.in6.sin6_addr.s6_addr, in6addr_any.s6_addr, sizeof(addr.in6.sin6_addr.s6_addr)) == 0)) {
        URPC_LIB_LOG_ERR("listening on :: is invalid\n");
        return URPC_FAIL;
    }

    return URPC_SUCCESS;
}

static inline bool urpc_server_info_ipv4_compare(const urpc_host_info_t *server, const urpc_host_info_t *target)
{
    return ((server->host_type == target->host_type) && !strcmp(server->ipv4.ip_addr, target->ipv4.ip_addr) &&
            (server->ipv4.port == target->ipv4.port));
}

static bool urpc_server_info_ipv6_compare(const urpc_host_info_t *server, const urpc_host_info_t *target)
{
    if ((server->host_type != target->host_type) || (server->ipv6.port != target->ipv6.port)) {
        return false;
    }

    if (strcmp(server->ipv6.ip_addr, target->ipv6.ip_addr) == 0) {
        return true;
    }

    // if ip_addr string is not same, use inet_pton to try again to prevent redundant '0' characters
    struct sockaddr_in6 addr_v6;
    struct sockaddr_in6 addr_v6_target;
    if (inet_pton(AF_INET6, server->ipv6.ip_addr, &(addr_v6.sin6_addr)) != 1) {
        return false;
    }

    if (inet_pton(AF_INET6, target->ipv6.ip_addr, &(addr_v6_target.sin6_addr)) != 1) {
        return false;
    }

    return memcmp(addr_v6.sin6_addr.s6_addr, addr_v6_target.sin6_addr.s6_addr, sizeof(addr_v6.sin6_addr.s6_addr)) == 0;
}

bool urpc_server_info_ip_compare(const urpc_host_info_t *server, const urpc_host_info_t *target)
{
    if (server->host_type == HOST_TYPE_IPV4) {
        return urpc_server_info_ipv4_compare(server, target);
    }

    return urpc_server_info_ipv6_compare(server, target);
}

uint32_t ctrl_opcode_get(urpc_ctrl_msg_type_t msg_type)
{
    uint32_t ctl_opcode;
    if (msg_type == URPC_CTRL_MSG_ATTACH) {
        ctl_opcode = URPC_CTL_QUEUE_INFO_ATTACH;
    } else if (msg_type == URPC_CTRL_MSG_DETACH) {
        ctl_opcode = URPC_CTL_QUEUE_INFO_DETACH;
    } else {
        ctl_opcode = URPC_CTL_QUEUE_INFO_REFRESH;
    }

    return ctl_opcode;
}

urpc_chmsg_v1_t *user_queue_info_xchg_get(ip_handshaker_ctx_t *ctx)
{
    if (!is_feature_enable(URPC_MANAGE_FEATURE_FLAG)) {
        return &ctx->chnl_ctx.attach_msg_v1_recv.chmsg_arr.chmsgs[0];
    }

    return &ctx->chnl_ctx.attach_msg_v1_recv.chmsg_arr.chmsgs[1];
}

urpc_chmsg_v1_t *manage_queue_info_xchg_get(ip_handshaker_ctx_t *ctx)
{
    if (!is_feature_enable(URPC_MANAGE_FEATURE_FLAG)) {
        return NULL;
    }

    return &ctx->chnl_ctx.attach_msg_v1_recv.chmsg_arr.chmsgs[0];
}

int create_negotiate_msg(ip_handshaker_ctx_t *ctx)
{
    if (ctx->cap.dp_encrypt == URPC_FALSE) {
        return 0;
    }

    if (urpc_neg_msg_v1_serialize(&ctx->neg_ctx.neg_msg_v1) != URPC_SUCCESS) {
        URPC_LIB_LOG_ERR("serialize negotiate message failed\n");
        return URPC_FAIL;
    }

    return ctx->neg_ctx.neg_msg_v1.data.len;
}

bool negotiate_msg_validate(ip_handshaker_ctx_t *ctx, urpc_ctl_head_t *recv_head)
{
    if (recv_head->error_code != URPC_SUCCESS) {
        URPC_LIB_LOG_ERR("negotiate failed, recv error code %d\n", recv_head->error_code);
        return false;
    }

    if (ctx->cap.dp_encrypt != recv_head->dp_encrypt) {
        URPC_LIB_LOG_ERR("negotiate failed, local dp_encrypt is %s but remote not support\n",
            ctx->cap.dp_encrypt == 0 ? "off" : "on");
        return false;
    }

    /* for func_info_enabled flag, we negotiate it with the follow strategy:
     * client 1 server 1, exchange func info;
     * client 1 server 0, return attach fail;
     * client 0 server 1, skip func info exchanging;
     * client 0 server 0, skip func info exchanging; */
    if (ctx->is_server && ctx->cap.func_info_enabled != recv_head->func_info_enabled) {
        if (ctx->cap.func_info_enabled == URPC_FALSE) {
            URPC_LIB_LOG_ERR("negotiate failed, server func_info feature is off but client need it\n");
            return false;
        }
        // skip server_func_send()
        ctx->cap.func_info_enabled = URPC_FALSE;
    }

    if (ctx->cap.multiplex_enabled != recv_head->multiplex_enabled) {
        URPC_LIB_LOG_ERR("negotiate failed, local multiplex_enabled is %s but remote not support\n",
            ctx->cap.multiplex_enabled == 0 ? "off" : "on");
        return false;
    }

    return true;
}

static urpc_server_channel_info_t *server_channel_get_from_transport(urpc_server_accept_entry_t *entry)
{
    urpc_server_channel_info_t *server_channel = NULL;
    URPC_LIST_FOR_EACH(server_channel, node, &entry->server_channel_list)
    {
        return server_channel;
    }
    return NULL;
}

int server_attach_channel_process(ip_handshaker_ctx_t *ctx)
{
    urpc_server_accept_entry_t *entry = (urpc_server_accept_entry_t *)ctx->base_task.transport_handle;
    urpc_chinfo_t *chinfo = &ctx->attach_info.channel_info;
    urpc_server_channel_info_t *server_channel = NULL;
    if (ctx->cap.multiplex_enabled) {
        server_channel = server_channel_get_from_transport(entry);
        if (server_channel != NULL) {
            // update server channel info
            if (server_channel_add_client_chid(server_channel, chinfo->chid) != URPC_SUCCESS) {
                URPC_LIB_LOG_ERR("add client channel %u to server channel failed, eid: " EID_FMT ", pid: %u\n",
                    chinfo->chid, EID_ARGS(chinfo->key.eid), chinfo->key.pid);
                return URPC_FAIL;
            }
            ip_handshaker_ctx_server_param_set(ctx, server_channel, &ctx->attach_info.channel_info.key);
            return URPC_SUCCESS;
        }
    }
    uint32_t server_chid = server_channel_id_map_lookup(ctx->attach_info.server_chid);
    server_channel = server_chid == URPC_INVALID_ID_U32 ? NULL : server_channel_get_with_rw_lock(server_chid, false);
    if (server_channel != NULL) {
        if (urpc_instance_key_cmp(&server_channel->key, &chinfo->key)) {
            ip_handshaker_ctx_server_param_set(ctx, server_channel, &chinfo->key);
            pthread_rwlock_unlock(&server_channel->rw_lock);
            return URPC_SUCCESS;
        }
        pthread_rwlock_unlock(&server_channel->rw_lock);
    }

    // user channel key and manage channel key is same
    server_channel = server_channel_alloc(&chinfo->key, ctx->server.client_keepalive_attr);
    if (server_channel == NULL) {
        URPC_LIB_LOG_ERR("create server channel failed, eid: " EID_FMT ", pid: %u\n",
            EID_ARGS(chinfo->key.eid), chinfo->key.pid);
        return URPC_FAIL;
    }
    server_channel->client_chid[0] = chinfo->chid;
    server_channel->client_chid_num = 1;
    ip_handshaker_ctx_server_param_set(ctx, server_channel, &ctx->attach_info.channel_info.key);
    pthread_rwlock_unlock(&server_channel->rw_lock);
    urpc_list_push_back(&entry->server_channel_list, &server_channel->node);
    return URPC_SUCCESS;
}

int client_channel_create_remote_queue(
    urpc_channel_info_t *channel, urpc_endpoints_t *endpoints, urpc_chmsg_v1_t *chmsg, bool is_all_queue)
{
    uint32_t server_pid = chmsg->chinfo->key.pid;

    server_node_t *server_node = channel_get_server_node(channel, &endpoints->server);
    // 1. remote queue info is new, create new server node
    if (server_node == NULL) {
        if (channel_put_remote_queue_infos(channel, chmsg->chinfo->chid, endpoints, chmsg)) {
            return URPC_SUCCESS;
        }
        URPC_LIB_LOG_ERR(
            "channel[%u] put remote queue infos failed, remote channel[%u]\n", channel->id, chmsg->chinfo->chid);
        return URPC_FAIL;
    }

    // 2. remote pid changed, we need to add new remote q, set old added remote q to QUEUE_STATUS_ERR, and delete
    // not added remote q. cipher need to reset in this case
    if (server_node->instance_key.pid != server_pid) {
        return channel_flush_remote_queue_info(channel, endpoints, server_node, chmsg);
    }

    // 3. remote pid is same, just process different remote queue
    return channel_update_remote_queue_info(channel, endpoints, server_node, chmsg, is_all_queue);
}

int init_cipher_for_server_node(urpc_channel_info_t *channel, urpc_host_info_t *server, crypto_key_t *crypto_key)
{
    // server node is created in remote queue add
    server_node_t *server_node = channel_get_server_node(channel, server);
    if (server_node == NULL || server_node->cipher_opt == NULL) {
        URPC_LIB_LOG_ERR("find server node for cipher initialization failed\n");
        return URPC_FAIL;
    }

    // already inited
    if (server_node->cipher_opt->chid != URPC_INVALID_ID_U32) {
        return URPC_SUCCESS;
    }

    if (crypto_cipher_init(server_node->cipher_opt, crypto_key) != URPC_SUCCESS) {
        URPC_LIB_LOG_ERR("init cipher failed\n");
        return URPC_FAIL;
    }
    server_node->cipher_opt->chid = channel->id;

    return URPC_SUCCESS;
}

static int ip_create_listen_fd(urpc_host_info_t *listen_cfg)
{
    socket_addr_t addr;
    int listen_fd, optval = 1;
    socklen_t len;

    if (urpc_socket_addr_format(listen_cfg, &addr, &len) != 0) {
        return URPC_FAIL;
    }

    listen_fd = socket(listen_cfg->host_type == HOST_TYPE_IPV4 ? AF_INET : AF_INET6, (int)SOCK_STREAM, IPPROTO_TCP);
    if (listen_fd < 0) {
        URPC_LIB_LOG_ERR("create socket failed, %s\n", strerror(errno));
        return URPC_FAIL;
    }

    // The default listening port of the server is no block.
    if (urpc_socket_set_non_block(listen_fd) != 0) {
        URPC_LIB_LOG_ERR("set socket non block failed, %s\n", strerror(errno));
        return URPC_FAIL;
    }

    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval)) != 0) {
        URPC_LIB_LOG_ERR("set socket reuseport failed, %s\n", strerror(errno));
        goto ERROR;
    }

    if (bind(listen_fd, (struct sockaddr *)(void *)&addr, len) < 0) {
        URPC_LIB_LOG_ERR("bind socket failed, %s\n", strerror(errno));
        goto ERROR;
    }

    if (listen(listen_fd, LISTEN_BACKLOG) < 0) {
        URPC_LIB_LOG_ERR("listen socket failed, %s\n", strerror(errno));
        goto ERROR;
    }

    return listen_fd;

ERROR:
    (void)close(listen_fd);

    return URPC_FAIL;
}

static void ip_handle_listen_event(uint32_t events, urpc_epoll_event_t *lev)
{
    if (lev == NULL) {
        URPC_LIB_LOG_ERR("listen event is null\n");
        return;
    }
    if ((events & ((uint32_t)EPOLLERR | EPOLLHUP)) != 0) {
        URPC_LIB_LOG_ERR("listen event is EPOLLERR or EPOLLHUP\n");
        return;
    }

    (void)transport_connection_accept(g_urpc_ip_ctl_ctx->listen_fd, lev->args);
    return;
}

int ip_handshaker_init(urpc_host_info_t *host, void *user_ctx)
{
    if (g_urpc_ip_ctl_ctx != NULL) {
        URPC_LIB_LOG_ERR("server already started\n");
        return URPC_FAIL;
    }
    int listen_fd = ip_create_listen_fd(host);
    if (listen_fd < 0) {
        URPC_LIB_LOG_ERR("create listen socket failed\n");
        return URPC_FAIL;
    }

    urpc_epoll_event_t *lev =
        (urpc_epoll_event_t *)urpc_dbuf_malloc(URPC_DBUF_TYPE_CP, sizeof(urpc_epoll_event_t));
    if (lev == NULL) {
        URPC_LIB_LOG_ERR("malloc listen event failed\n");
        goto CLOSE_FD;
    }
    lev->fd = listen_fd;
    lev->args = user_ctx;
    lev->func = ip_handle_listen_event;
    lev->events = EPOLLIN;
    lev->is_handshaker_ctx = false;
    g_urpc_ip_ctl_ctx = (ip_ctl_ctx_t *)urpc_dbuf_calloc(URPC_DBUF_TYPE_CP, 1, sizeof(ip_ctl_ctx_t));
    if (g_urpc_ip_ctl_ctx == NULL) {
        URPC_LIB_LOG_ERR("malloc control context failed\n");
        goto FREE_LEV;
    }
    g_urpc_ip_ctl_ctx->listen_fd = listen_fd;
    g_urpc_ip_ctl_ctx->lev = lev;
    if (urpc_mange_event_register(URPC_MANAGE_JOB_TYPE_LISTEN, lev) != URPC_SUCCESS) {
        goto FREE_CTL_CTX;
    }

    return URPC_SUCCESS;

FREE_CTL_CTX:
    urpc_dbuf_free(g_urpc_ip_ctl_ctx);
    g_urpc_ip_ctl_ctx = NULL;
FREE_LEV:
    urpc_dbuf_free(lev);
CLOSE_FD:
    (void)close(listen_fd);

    return URPC_FAIL;
}

void ip_handshaker_uninit(void)
{
    if (g_urpc_ip_ctl_ctx == NULL) {
        return;
    }

    (void)close(g_urpc_ip_ctl_ctx->listen_fd);
    urpc_dbuf_free(g_urpc_ip_ctl_ctx->lev);
    urpc_dbuf_free(g_urpc_ip_ctl_ctx);
    g_urpc_ip_ctl_ctx = NULL;
}
