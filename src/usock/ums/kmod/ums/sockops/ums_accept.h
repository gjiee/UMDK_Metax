/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * UB Memory based Socket(UMS)
 *
 * Description:UMS accept ops header file
 *
 * Copyright IBM Corp. 2016, 2018
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2025. All rights reserved.
 *
 * Original SMC-R implementation:
 *     Author(s): Ursula Braun <ubraun@linux.vnet.ibm.com>
 *                based on prototype from Frank Blaschka
 *
 * UMS implementation:
 *     Author(s): Sun fang
 */

#ifndef UMS_ACCEPT_H
#define UMS_ACCEPT_H

#include <net/sock.h>
#include <linux/socket.h>

#include "ums_core.h"

static inline bool ums_accept_queue_empty(struct sock *sk)
{
	return list_empty(&ums_sk(sk)->accept_q) != 0;
}

int ums_accept(struct socket *sock, struct socket *new_sock, int flags, bool kern);
struct sock *ums_accept_dequeue(struct sock *parent, struct socket *new_sock);
#endif /* UMS_ACCEPT_H */
