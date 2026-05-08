/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * UB Memory based Socket(UMS)
 *
 * Description:UMS bind ops header file
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

#ifndef UMS_BIND_H
#define UMS_BIND_H

#include <net/sock.h>
#include <linux/socket.h>

int ums_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len);
#endif /* UMS_BIND_H */
