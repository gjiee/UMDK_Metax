/* SPDX-License-Identifier: GPL-2.0 */
/*
 * UB Memory based Socket(UMS)
 *
 * Description:UMS release ops header file
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

#ifndef UMS_RELEASE_H
#define UMS_RELEASE_H

#include <net/sock.h>
#include <linux/socket.h>

int ums_release(struct socket *sock);
int ums_release_inner(struct ums_sock *ums);
#endif /* UMS_RELEASE_H */
