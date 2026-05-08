/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * UB Memory based Socket(UMS)
 *
 * Description:UMS listen ops header file
 *
 * Copyright IBM Corp. 2016, 2018
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2025. All rights reserved.
 *
 * Original SMC-R implementation:
 *     Author(s): Ursula Braun <ubraun@linux.vnet.ibm.com>
 *                based on prototype from Frank Blaschka
 *
 * UMS implementation:
 *     Author(s):Sun fang
 */

#ifndef UMS_LISTEN_H
#define UMS_LISTEN_H

#include <net/sock.h>
#include <linux/socket.h>
#include <linux/workqueue.h>

#include "ums_types.h"

int ums_listen(struct socket *sock, int backlog);
void ums_listen_work(struct work_struct *work);
void ums_listen_out_err(struct ums_sock *new_ums);
void ums_listen_out_connected(struct ums_sock *new_ums);
#endif /* UMS_LISTEN_H */
