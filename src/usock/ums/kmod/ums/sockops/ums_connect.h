/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * UB Memory based Socket(UMS)
 *
 * Description:UMS connect ops header file
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

#ifndef UMS_CONNECT_H
#define UMS_CONNECT_H

#include <net/sock.h>
#include <linux/socket.h>

#define UMS_LLC_CONFIRM_WAIT_TIME  (2 * UMS_LLC_WAIT_TIME)

int ums_connect(struct socket *sock, struct sockaddr *addr, int alen, int flags);
void ums_connect_work(struct work_struct *work);
#endif /* UMS_CONNECT_H */
