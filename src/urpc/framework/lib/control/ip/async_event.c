/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: realize async event function
 * Create: 2025-1-21
 */

#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>

#include "state.h"
#include "urpc_dbuf_stat.h"
#include "urpc_epoll.h"
#include "urpc_framework_errno.h"
#include "urpc_list.h"
#include "urpc_lib_log.h"

#include "async_event.h"

typedef struct urpc_async_event_node {
    urpc_list_t node;
    urpc_async_event_t event;
} urpc_async_event_node_t;

typedef struct urpc_async_event_ctx {
    int eventfd;
    int activate_task_fd;
    urpc_list_t event_list;
    pthread_mutex_t event_list_mutex;
    urpc_epoll_event_t activate_task_event;
} urpc_async_event_ctx_t;

static urpc_async_event_ctx_t g_urpc_async_event_ctx = {
    .eventfd = URPC_INVALID_FD,
    .activate_task_fd = URPC_INVALID_FD,
};

int urpc_async_event_get(urpc_async_event_t events[], int num)
{
    urpc_async_event_node_t *next;
    urpc_async_event_node_t *cur;
    int index = 0;
    int eventfd = g_urpc_async_event_ctx.eventfd;
    uint64_t count;

    if (urpc_state_get() != URPC_STATE_INIT) {
        URPC_LIB_LOG_ERR("read event failed, urpc is not ready\n");
        return 0;
    }

    if (events == NULL || num <= 0) {
        URPC_LIB_LOG_ERR("invalid parameter\n");
        return -URPC_ERR_EINVAL;
    }

    (void)pthread_mutex_lock(&g_urpc_async_event_ctx.event_list_mutex);
    URPC_LIST_FOR_EACH_SAFE(cur, next, node, &g_urpc_async_event_ctx.event_list)
    {
        if (index >= num) {
            (void)pthread_mutex_unlock(&g_urpc_async_event_ctx.event_list_mutex);
            return index;
        }
        events[index++] = cur->event;
        urpc_list_remove(&cur->node);
        urpc_dbuf_free(cur);
    }

    if (urpc_list_is_empty(&g_urpc_async_event_ctx.event_list) && index != 0) {
        if (eventfd_read(eventfd, &count) == -1) {
            (void)pthread_mutex_unlock(&g_urpc_async_event_ctx.event_list_mutex);
            URPC_LIB_LOG_WARN("read event failed, err:%s\n", strerror(errno));
            return index;
        }
    }
    (void)pthread_mutex_unlock(&g_urpc_async_event_ctx.event_list_mutex);

    return index;
}

int urpc_async_event_fd_get(void)
{
    return g_urpc_async_event_ctx.eventfd;
}

int async_event_ctx_init(void)
{
    g_urpc_async_event_ctx.eventfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (g_urpc_async_event_ctx.eventfd == URPC_INVALID_FD) {
        URPC_LIB_LOG_ERR("create eventfd failed, err:%s\n", strerror(errno));
        return URPC_FAIL;
    }

    g_urpc_async_event_ctx.activate_task_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (g_urpc_async_event_ctx.activate_task_fd == URPC_INVALID_FD) {
        URPC_LIB_LOG_ERR("create activate task fd failed, err:%s\n", strerror(errno));
        (void)close(g_urpc_async_event_ctx.eventfd);
        g_urpc_async_event_ctx.eventfd = URPC_INVALID_FD;
        return URPC_FAIL;
    }
    urpc_list_init(&g_urpc_async_event_ctx.event_list);
    pthread_mutex_init(&g_urpc_async_event_ctx.event_list_mutex, NULL);
    return URPC_SUCCESS;
}

void async_event_ctx_uninit(void)
{
    urpc_async_event_node_t *next;
    urpc_async_event_node_t *cur;
    (void)pthread_mutex_lock(&g_urpc_async_event_ctx.event_list_mutex);
    URPC_LIST_FOR_EACH_SAFE(cur, next, node, &g_urpc_async_event_ctx.event_list)
    {
        urpc_list_remove(&cur->node);
        urpc_dbuf_free(cur);
    }
    (void)pthread_mutex_unlock(&g_urpc_async_event_ctx.event_list_mutex);

    if (g_urpc_async_event_ctx.eventfd != URPC_INVALID_FD) {
        (void)close(g_urpc_async_event_ctx.eventfd);
        g_urpc_async_event_ctx.eventfd = URPC_INVALID_FD;
    }

    if (g_urpc_async_event_ctx.activate_task_fd != URPC_INVALID_FD) {
        (void)close(g_urpc_async_event_ctx.activate_task_fd);
        g_urpc_async_event_ctx.activate_task_fd = URPC_INVALID_FD;
    }
    pthread_mutex_destroy(&g_urpc_async_event_ctx.event_list_mutex);
}

void async_event_notify(urpc_async_event_t *event)
{
    uint64_t value = 1;
    urpc_async_event_node_t *event_info =
        (urpc_async_event_node_t *)urpc_dbuf_calloc(URPC_DBUF_TYPE_CP, 1, sizeof(urpc_async_event_node_t));
    if (event_info == NULL) {
        URPC_LIB_LOG_ERR("malloc event node failed\n");
        return;
    }
    event_info->event = *event;

    (void)pthread_mutex_lock(&g_urpc_async_event_ctx.event_list_mutex);
    urpc_list_push_back(&g_urpc_async_event_ctx.event_list, &event_info->node);
 
    if (eventfd_write(g_urpc_async_event_ctx.eventfd, value) == -1) {
        URPC_LIB_LOG_ERR("write event failed, err:%s\n", strerror(errno));
        urpc_list_remove(&event_info->node);
        (void)pthread_mutex_unlock(&g_urpc_async_event_ctx.event_list_mutex);
        urpc_dbuf_free(event_info);
        return;
    }
    (void)pthread_mutex_unlock(&g_urpc_async_event_ctx.event_list_mutex);
    return;
}

int async_event_activation_fd_get(void)
{
    return g_urpc_async_event_ctx.activate_task_fd;
}

urpc_epoll_event_t *async_event_activation_event_get(void)
{
    return &(g_urpc_async_event_ctx.activate_task_event);
}