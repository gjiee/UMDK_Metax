// SPDX-License-Identifier: GPL-2.0-only
/*
 * UB Memory based Socket(UMS)
 *
 * Description:UMS bind ops implementation
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

#include <net/net_namespace.h>
#include <linux/socket.h>

#include "ums_core.h"
#include "ums_bind.h"

int ums_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sockaddr_in *addr;
	struct ums_sock *ums;
	struct sock *sk;
	int rc;

	rc = -EINVAL;
	if (addr_len < (int)sizeof(struct sockaddr_in))
		goto out;

	addr = (struct sockaddr_in *)uaddr;
	sk = sock->sk;
	ums = ums_sk(sk);

	rc = -EAFNOSUPPORT;
	if (addr->sin_family != AF_INET && addr->sin_family != AF_INET6 &&
		addr->sin_family != AF_UNSPEC)
		goto out;
	if (addr->sin_family == AF_UNSPEC && addr->sin_addr.s_addr != htonl(INADDR_ANY))
		goto out;

	rc = -EINVAL;
	if (ums->connect_nonblock != 0)
		goto out;
	lock_sock(sk);
	if (sk->sk_state != (unsigned char)UMS_INIT)
		goto out_rel;
	ums->clcsock->sk->sk_reuse = sk->sk_reuse;
	ums->clcsock->sk->sk_reuseport = sk->sk_reuseport;

	rc = kernel_bind(ums->clcsock, uaddr, addr_len);

out_rel:
	release_sock(sk);
out:
	return rc;
}
