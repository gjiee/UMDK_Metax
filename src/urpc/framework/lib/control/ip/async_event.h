/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: define async event function
 * Create: 2025-1-21
 */
#ifndef URPC_ASYNC_EVENT_H
#define URPC_ASYNC_EVENT_H

#include <stdint.h>
#include <sys/eventfd.h>

#include "urpc_epoll.h"
#include "urpc_framework_types.h"

#ifdef __cplusplus
extern "C" {
#endif

int async_event_ctx_init(void);
void async_event_ctx_uninit(void);
void async_event_notify(urpc_async_event_t *event);
int async_event_activation_fd_get(void);
urpc_epoll_event_t *async_event_activation_event_get(void);

#ifdef __cplusplus
}
#endif

#endif