/* SPDX-License-Identifier: GPL-2.0 */
/*
 * UB Memory based Socket(UMS)
 *
 * Description:UMS close ops header file
 *
 * Copyright IBM Corp. 2016
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2025. All rights reserved.
 *
 * UMS implementation:
 *     Author(s): YAO Yufeng ZHANG Chuwen
 */

#ifndef UMS_CLOSE_H
#define UMS_CLOSE_H

#include <linux/workqueue.h>

#include "ums_mod.h"

#define UMS_MAX_STREAM_WAIT_TIMEOUT (2 * HZ)
#define UMS_CLOSE_SOCK_PUT_DELAY HZ

void ums_close_wake_tx_prepared(struct ums_sock *ums);
int ums_close_active(struct ums_sock *ums);
int ums_close_shutdown_write(struct ums_sock *ums);
void ums_close_init(struct ums_sock *ums);
void ums_clcsock_release(struct ums_sock *ums);
int ums_close_abort(struct ums_connection *conn);
void ums_close_active_abort(struct ums_sock *ums);
#ifdef UMS_UT_TEST
void ums_close_passive_abort_received(struct ums_sock *ums);
#endif
#endif /* UMS_CLOSE_H */
