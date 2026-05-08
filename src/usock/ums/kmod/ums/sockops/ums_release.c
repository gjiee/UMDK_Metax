// SPDX-License-Identifier: GPL-2.0-only
/*
 * UB Memory based Socket(UMS)
 *
 * Description:UMS release ops implementation
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
#include <net/tcp.h>
#include <linux/socket.h>

#include "ums_core.h"
#include "ums_close.h"
#include "ums_release.h"

#if defined(UMS_TRACE)
static void ums_dump_stats(void)
{
	int i;

	trace_printk("Packets TXed:\n");
	for (i = 0; i < PKT_TX_STATS_ARRAY_SIZE; i++) {
		if (pkt_tx_stats[i] != 0) {
			if (i < PKT_TX_STATS_ARRAY_SIZE - 1)
				trace_printk("Length %d to %d: %llu\n", i * PKT_STATS_SIZE_PER_ELEMENT,
					(i + 1) * PKT_STATS_SIZE_PER_ELEMENT - 1, pkt_tx_stats[i]);
			else
				trace_printk("Length above %d: %llu\n", i * PKT_STATS_SIZE_PER_ELEMENT,
					pkt_tx_stats[i]);
		}
	}
	trace_printk("Packets RXed:\n");
	for (i = 0; i < PKT_RX_STATS_ARRAY_SIZE; i++) {
		if (pkt_rx_stats[i] != 0) {
			if (i < PKT_RX_STATS_ARRAY_SIZE - 1)
				trace_printk("Length %d to %d: %llu\n", i * PKT_STATS_SIZE_PER_ELEMENT,
					(i + 1) * PKT_STATS_SIZE_PER_ELEMENT - 1, pkt_rx_stats[i]);
			else
				trace_printk("Length above %d: %llu\n", i * PKT_STATS_SIZE_PER_ELEMENT,
					pkt_rx_stats[i]);
		}
	}
}
#endif

static void ums_fback_restore_callbacks(struct ums_sock *ums)
{
	struct sock *clcsk = ums->clcsock->sk;

	if (!clcsk)
		return;

	write_lock_bh(&clcsk->sk_callback_lock);
	clcsk->sk_user_data = NULL;

	ums_clcsock_restore_cb(&clcsk->sk_state_change, &ums->clcsk_state_change);
	ums_clcsock_restore_cb(&clcsk->sk_data_ready, &ums->clcsk_data_ready);
	ums_clcsock_restore_cb(&clcsk->sk_write_space, &ums->clcsk_write_space);
	ums_clcsock_restore_cb(&clcsk->sk_error_report, &ums->clcsk_error_report);

	write_unlock_bh(&clcsk->sk_callback_lock);
}

static void ums_restore_fallback_changes(struct ums_sock *ums)
{
	if (ums->clcsock->file) { /* non-accepted sockets have no file yet */
		ums->clcsock->file->private_data = ums->sk.sk_socket;
		ums->clcsock->file = NULL;
		ums_fback_restore_callbacks(ums);
	}
}

int ums_release_inner(struct ums_sock *ums)
{
	struct sock *sk = &ums->sk;
	int rc = 0;

	if (!ums->use_fallback) {
		rc = ums_close_active(ums);
		sock_set_flag(sk, SOCK_DEAD);
		sk->sk_shutdown |= SHUTDOWN_MASK;
	} else {
		if (sk->sk_state != (unsigned char)UMS_CLOSED) {
			if (sk->sk_state != (unsigned char)UMS_LISTEN && sk->sk_state != (unsigned char)UMS_INIT)
				sock_put(sk); /* passive closing */
			if (sk->sk_state == (unsigned char)UMS_LISTEN)
				/* wake up clcsock accept */
				rc = kernel_sock_shutdown(ums->clcsock, SHUT_RDWR);
			sk->sk_state = (unsigned char)UMS_CLOSED;
			sk->sk_state_change(sk);
		}
		ums_restore_fallback_changes(ums);
	}

	sk->sk_prot->unhash(sk);

	if (sk->sk_state == (unsigned char)UMS_CLOSED) {
		if (ums->clcsock) {
			release_sock(sk);
			ums_clcsock_release(ums);
			lock_sock(sk);
		}

		if (!ums->use_fallback) {
			sock_hold(sk);
			if (!queue_work(g_ums_close_wq, &ums->free_work))
				sock_put(sk);
		}
	}

	return rc;
}

int ums_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	int old_state, rc = 0;
	struct ums_sock *ums;

#if defined(UMS_TRACE)
	ums_dump_stats();
#endif
	if (!sk)
		goto out;
	sock_hold(sk);
	ums = ums_sk(sk);
	old_state = sk->sk_state;

	if ((ums->connect_nonblock != 0) && (old_state == (unsigned char)UMS_INIT))
		(void)tcp_abort(ums->clcsock->sk, ECONNABORTED);
	if ((ums->connect_nonblock != 0) && cancel_work_sync(&ums->connect_work))
		sock_put(&ums->sk); /* sock_hold in ums_connect for passive closing */
	if (sk->sk_state == (unsigned char)UMS_LISTEN)
		/* ums_close_non_accepted() is called and acquires
		 * sock lock for child sockets again
		 */
		lock_sock_nested(sk, SINGLE_DEPTH_NESTING);
	else
		lock_sock(sk);
	if (old_state == (unsigned char)UMS_INIT && sk->sk_state == (unsigned char)UMS_ACTIVE &&
		!ums->use_fallback)
		ums_close_active_abort(ums);
	rc = ums_release_inner(ums);
	/* detach socket */
	sock_orphan(sk);
	sock->sk = NULL;
	release_sock(sk);
	/* for hold in this function */
	sock_put(sk);
	/* for global */
	sock_put(sk);
out:
	return rc;
}
