/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: provider ops based on jetty
 */

#ifndef PROVIDER_OPS_JETTY_H
#define PROVIDER_OPS_JETTY_H

#include <errno.h>
#include <stdbool.h>
#include "urma_types.h"
#include "queue.h"
#include "cp.h"
#include "urpc_epoll.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum provider_ctx_status {
    /** provider_ctx is idle */
    PROVIDER_CTX_STATUS_IDLE,
    /** provider_ctx is ready */
    PROVIDER_CTX_STATUS_READY,
    /** provider_ctx has seen a failure but expects to recover */
    PROVIDER_CTX_STATUS_FAULT,
    /** provider_ctx has seen a failure that it cannot recover */
    PROVIDER_CTX_STATUS_ERR,
} provider_ctx_status_t;

typedef enum import_notifier_status {
    IMPORT_NOTIFIER_STATUS_READY = 0,
    IMPORT_NOTIFIER_STATUS_ERR,
} import_notifier_status_t;

typedef struct import_notifier {
    urma_notifier_t *notifier;
    import_notifier_status_t status;
    urpc_epoll_event_t event;
    import_callback_t import_callback;
} import_notifier_t;

typedef struct jetty_provider {
    provider_t provider;
    urma_device_t *urma_dev;
    urma_context_t *urma_ctx;
    urpc_epoll_event_t *event;
    urma_device_attr_t dev_attr;
    provider_ctx_status_t status;
    import_notifier_t import_notifier;
    int err_code;
} jetty_provider_t;

static inline uint32_t token_policy_get(void)
{
    return is_feature_enable(URPC_FEATURE_DISABLE_TOKEN_POLICY) ? URMA_TOKEN_NONE : URMA_TOKEN_PLAIN_TEXT;
}

#ifdef __cplusplus
}
#endif

#endif
