/* SPDX-License-Identifier: GPL-2.0 */
/*
 * UB Memory based Socket(UMS)
 *
 * Description:UMS sockops related ops implementation
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

#include <net/net_namespace.h>

#include "ums_core.h"
#include "ums_mod.h"
#include "ums_common.h"
#include "ums_clc.h"
#include "ums_tx.h"
#include "ums_sockops.h"

static int ums_process_sockopt(struct sock *sk, struct ums_sock *ums, int optname, int val)
{
	int rc = 0;

	lock_sock(sk);
	switch (optname) {
	case TCP_FASTOPEN:
	case TCP_FASTOPEN_CONNECT:
	case TCP_FASTOPEN_KEY:
	case TCP_FASTOPEN_NO_COOKIE:
		/* option not supported by UMS */
		if ((sk->sk_state == (unsigned char)UMS_INIT) && (ums->connect_nonblock == 0)) {
			rc = ums_switch_to_fallback(ums, UMS_CLC_DECL_OPTUNSUPP);
		} else {
			rc = -EINVAL;
		}
		break;
	case TCP_NODELAY:
		if (sk->sk_state != (unsigned char)UMS_INIT && sk->sk_state != (unsigned char)UMS_LISTEN &&
			sk->sk_state != (unsigned char)UMS_CLOSED) {
			if (val != 0) {
				ums_tx_pending(&ums->conn);
				(void)cancel_delayed_work(&ums->conn.tx_work);
			}
		}
		break;
	case TCP_CORK:
		if (sk->sk_state != (unsigned char)UMS_INIT && sk->sk_state != (unsigned char)UMS_LISTEN &&
			sk->sk_state != (unsigned char)UMS_CLOSED) {
			if (val == 0) {
				ums_tx_pending(&ums->conn);
				(void)cancel_delayed_work(&ums->conn.tx_work);
			}
		}
		break;
	case TCP_DEFER_ACCEPT:
		ums->sockopt_defer_accept = val;
		break;
	default:
		break;
	}

	release_sock(sk);

	return rc;
}

#ifdef KERNEL_VERSION_4
int ums_setsockopt(struct socket *sock, int level, int optname, char __user * optval,
	unsigned int optlen)
#else
int ums_setsockopt(struct socket *sock, int level, int optname, sockptr_t optval,
	unsigned int optlen)
#endif
{
	struct sock *sk = sock->sk;
	struct ums_sock *ums;
	int val, rc;

	if ((level == SOL_SMC) || (level == SOL_TCP && optname == TCP_ULP))
		return -EOPNOTSUPP;

	ums = ums_sk(sk);
	/* generic setsockopts reaching us here always apply to the
	 * CLC socket
	 */
	down_read(&ums->clcsock_release_lock);
	if (!ums->clcsock) {
		up_read(&ums->clcsock_release_lock);
		return -EBADF;
	}
	if (unlikely(!ums->clcsock->ops->setsockopt))
		rc = -EOPNOTSUPP;
	else
		rc = ums->clcsock->ops->setsockopt(ums->clcsock, level, optname, optval, optlen);
	if (ums->clcsock->sk->sk_err != 0) {
		sk->sk_err = ums->clcsock->sk->sk_err;
		sk->sk_error_report(sk);
	}
	up_read(&ums->clcsock_release_lock);
	if ((rc != 0) || ums->use_fallback)
		return rc;
	if (optlen < sizeof(int))
		return -EINVAL;
	if (copy_from_sockptr(&val, optval, sizeof(int)) != 0)
		return -EFAULT;

	return ums_process_sockopt(sk, ums, optname, val);
}

int ums_getsockopt(struct socket *sock, int level, int optname, char __user *optval,
	int __user *optlen)
{
	struct ums_sock *ums;
	int rc;

	if (level == SOL_SMC)
		return -EOPNOTSUPP;

	ums = ums_sk(sock->sk);
	down_read(&ums->clcsock_release_lock);
	if (!ums->clcsock) {
		up_read(&ums->clcsock_release_lock);
		return -EBADF;
	}
	/* socket options apply to the CLC socket */
	if (unlikely(!ums->clcsock->ops->getsockopt)) {
		up_read(&ums->clcsock_release_lock);
		return -EOPNOTSUPP;
	}
	rc = ums->clcsock->ops->getsockopt(ums->clcsock, level, optname, optval, optlen);
	up_read(&ums->clcsock_release_lock);
	return rc;
}
