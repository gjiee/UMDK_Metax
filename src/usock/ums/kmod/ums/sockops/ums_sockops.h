/* SPDX-License-Identifier: GPL-2.0 */
/*
 * UB Memory based Socket(UMS)
 *
 * Description:UMS sockops related ops header file
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

#ifndef UMS_SOCK_OPERATE_H
#define UMS_SOCK_OPERATE_H

#include <net/sock.h>
#include <linux/socket.h>

#ifdef KERNEL_VERSION_4
int ums_setsockopt(struct socket *sock, int level, int optname, char __user * optval,
	unsigned int optlen);
#else
int ums_setsockopt(struct socket *sock, int level, int optname, sockptr_t optval,
	unsigned int optlen);
#endif
int ums_getsockopt(struct socket *sock, int level, int optname, char __user *optval,
	int __user *optlen);
#endif /* UMS_SOCK_OPERATE_H */
