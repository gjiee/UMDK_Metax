// SPDX-License-Identifier: GPL-2.0
/*
 * UB Memory based Socket(UMS)
 *
 * Description:UMS close ops implementation
 *
 * Copyright IBM Corp. 2016
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2025. All rights reserved.
 *
 * Original SMC-R implementation:
 *     Author(s): Ursula Braun <ubraun@linux.vnet.ibm.com>
 *
 * UMS implementation:
 *     Author(s): YAO Yufeng ZHANG Chuwen
 */

#include <net/sock.h>
#include <net/tcp.h>

#include <linux/sched/signal.h>
#include <linux/workqueue.h>

#include "ums_cdc.h"
#include "ums_log.h"
#include "ums_mod.h"
#include "ums_accept.h"
#include "ums_tx.h"
#include "ums_close.h"

/* release the clcsock that is assigned to the ums_sock */
void ums_clcsock_release(struct ums_sock *ums)
{
	struct socket *tcp;

	if (ums->listen_ums && !ums->use_fallback && current_work() != &ums->ums_listen_work)
		(void)cancel_work_sync(&ums->ums_listen_work);
	down_write(&ums->clcsock_release_lock);

	if (ums->clcsock) {
		tcp = ums->clcsock;
		ums->clcsock = NULL;
		sock_release(tcp);
	}
	up_write(&ums->clcsock_release_lock);
}

static void ums_close_cleanup_listen(struct sock *parent)
{
	struct sock *sk;

	/* Close non-accepted connections */
	while ((sk = ums_accept_dequeue(parent, NULL)) != NULL)
		ums_close_non_accepted(sk);
}

/* wait for sndbuf data being transmitted */
static void ums_close_stream_wait(struct ums_sock *ums, long timeout)
{
	DEFINE_WAIT_FUNC(wait, woken_wake_function);
	struct sock *sk = &ums->sk;

	if (timeout == 0)
		return;

	if (ums_tx_prepared_sends(&ums->conn) == 0)
		return;

	/* Send out corked data remaining in sndbuf */
	ums_tx_pending(&ums->conn);

	ums->wait_close_tx_prepared = 1;
	add_wait_queue(sk_sleep(sk), &wait);
	while ((signal_pending(current) == 0) && (timeout != 0)) {
		int rc;

		rc = sk_wait_event(sk, &timeout, (ums_tx_prepared_sends(&ums->conn) == 0) ||
			(sk->sk_err == ECONNABORTED) || (sk->sk_err == ECONNRESET) || (ums->conn.killed != 0),
			&wait);
		if (rc != 0)
			break;
	}
	remove_wait_queue(sk_sleep(sk), &wait);
	ums->wait_close_tx_prepared = 0;
}

void ums_close_wake_tx_prepared(struct ums_sock *ums)
{
	if (ums->wait_close_tx_prepared != 0)
		/* wake up socket closing */
		ums->sk.sk_state_change(&ums->sk);
}

static int ums_close_wr(struct ums_connection *conn)
{
	conn->local_tx_ctrl.conn_state_flags.peer_done_writing = 1;

	return ums_cdc_get_slot_and_msg_send(conn);
}

static int ums_close_final(struct ums_connection *conn)
{
	if (atomic_read(&conn->bytes_to_rcv) == 0)
		conn->local_tx_ctrl.conn_state_flags.peer_conn_closed = 1;
	else
		conn->local_tx_ctrl.conn_state_flags.peer_conn_abort = 1;
	if (conn->killed != 0)
		return -EPIPE;

	return ums_cdc_get_slot_and_msg_send(conn);
}

int ums_close_abort(struct ums_connection *conn)
{
	conn->local_tx_ctrl.conn_state_flags.peer_conn_abort = 1;

	return ums_cdc_get_slot_and_msg_send(conn);
}

static void ums_close_cancel_work(struct ums_sock *ums)
{
	struct sock *sk = &ums->sk;

	release_sock(sk);
	if (cancel_work_sync(&ums->conn.close_work))
		sock_put(sk); /* sock_hold done by schedulers of close_work */
	(void)cancel_delayed_work_sync(&ums->conn.tx_work);
	lock_sock(sk);
}

static bool ums_close_active_abort_on_ums_common(struct ums_sock *ums)
{
	struct sock *sk = &ums->sk;
	bool is_break = false;

	sk->sk_state = (unsigned char)UMS_PEERABORTWAIT;
	ums_close_cancel_work(ums);
	if (sk->sk_state != (unsigned char)UMS_PEERABORTWAIT)
		is_break = true;
	else
		sk->sk_state = (unsigned char)UMS_CLOSED;

	return is_break;
}

static void ums_close_active_abort_on_ums_app_close_wait(struct ums_sock *ums)
{
	struct sock *sk = &ums->sk;

	if (ums_close_active_abort_on_ums_common(ums))
		return;
	ums_conn_free(&ums->conn);
	sock_put(sk); /* (postponed) passive closing */
}

static void ums_close_active_abort_on_ums_peer_fin_close_wait(struct ums_sock *ums,
	bool *release_clcsock)
{
	struct sock *sk = &ums->sk;

	if (ums_close_active_abort_on_ums_common(ums))
		return;
	ums_conn_free(&ums->conn);
	*release_clcsock = true;
	sock_put(sk); /* passive closing */
}

static void ums_close_active_abort_on_ums_app_fin_close_wait(struct ums_sock *ums,
	bool *release_clcsock)
{
	if (ums_close_active_abort_on_ums_common(ums))
		return;
	ums_conn_free(&ums->conn);
	*release_clcsock = true;
}

/* terminate ums socket abnormally - active abort
 * link group is terminated, i.e. communication no longer possible
 */
void ums_close_active_abort(struct ums_sock *ums)
{
	struct sock *sk = &ums->sk;
	bool release_clcsock = false;

	if (sk->sk_state != (unsigned char)UMS_INIT && ums->clcsock && ums->clcsock->sk) {
		sk->sk_err = ECONNABORTED;
		UMS_LOGD("try to abort conn %u?", ums->conn.conn_id);
		if (ums->clcsock && ums->clcsock->sk)
			(void)tcp_abort(ums->clcsock->sk, ECONNABORTED);
	}
	switch (sk->sk_state) {
	case UMS_ACTIVE:
	case UMS_APPCLOSEWAIT1:
	case UMS_APPCLOSEWAIT2:
		ums_close_active_abort_on_ums_app_close_wait(ums);
		break;
	case UMS_PEERCLOSEWAIT1:
	case UMS_PEERCLOSEWAIT2:
	case UMS_PEERFINCLOSEWAIT:
		ums_close_active_abort_on_ums_peer_fin_close_wait(ums, &release_clcsock);
		break;
	case UMS_PROCESSABORT:
	case UMS_APPFINCLOSEWAIT:
		ums_close_active_abort_on_ums_app_fin_close_wait(ums, &release_clcsock);
		break;
	case UMS_INIT:
	case UMS_PEERABORTWAIT:
	case UMS_CLOSED:
		break;
	default:
		UMS_LOGE("Invalid sk state.");
		break;
	}

	sock_set_flag(sk, SOCK_DEAD);
	sk->sk_state_change(sk);

	if (release_clcsock) {
		release_sock(sk);
		ums_clcsock_release(ums);
		lock_sock(sk);
	}
}

static inline bool ums_close_sent_any_close(const struct ums_connection *conn)
{
	return (conn->local_tx_ctrl.conn_state_flags.peer_conn_abort != 0) ||
		(conn->local_tx_ctrl.conn_state_flags.peer_conn_closed != 0);
}

static void ums_close_active_on_ums_listen(struct ums_sock *ums, int *rc)
{
	struct sock *sk = &ums->sk;
	int i = 0;

	sk->sk_state = (unsigned char)UMS_CLOSED;
	sk->sk_state_change(sk); /* wake up accept */
	if (ums->clcsock && ums->clcsock->sk) {
		write_lock_bh(&ums->clcsock->sk->sk_callback_lock);
		ums_clcsock_restore_cb(&ums->clcsock->sk->sk_data_ready, &ums->clcsk_data_ready);
		ums->clcsock->sk->sk_user_data = NULL;
		write_unlock_bh(&ums->clcsock->sk->sk_callback_lock);
		*rc = kernel_sock_shutdown(ums->clcsock, SHUT_RDWR);
	}
	ums_close_cleanup_listen(sk);
	release_sock(sk);
	for (i = 0; i < UMS_MAX_TCP_LISTEN_WORKS; i++)
		(void)flush_work(&ums->tcp_listen_works[i].work);
	lock_sock(sk);
}

static bool ums_close_active_on_ums_active(struct ums_sock *ums, int *rc, long timeout)
{
	struct ums_connection *conn = &ums->conn;
	struct sock *sk = &ums->sk;
	bool again = true;
	int rc1 = 0;

	ums_close_stream_wait(ums, timeout);
	release_sock(sk);
	(void)cancel_delayed_work_sync(&conn->tx_work);
	lock_sock(sk);
	if (sk->sk_state == (unsigned char)UMS_ACTIVE) {
		/* send close request */
		*rc = ums_close_final(conn);
		sk->sk_state = (unsigned char)UMS_PEERCLOSEWAIT1;

		/* actively shutdown clcsock before peer close it,
			* prevent peer from entering TIME_WAIT state.
			*/
		if (ums->clcsock && ums->clcsock->sk) {
			rc1 = kernel_sock_shutdown(ums->clcsock, SHUT_RDWR);
			*rc = *rc != 0 ? *rc : rc1;
		}
		again = false;
	}
	return again;
}

static void ums_close_active_on_ums_peer_close_wait(struct ums_sock *ums, int *rc)
{
	struct ums_cdc_conn_state_flags *txflags = &ums->conn.local_tx_ctrl.conn_state_flags;
	struct ums_connection *conn = &ums->conn;

	if ((txflags->peer_done_writing != 0) && !ums_close_sent_any_close(conn))
		/* just shutdown wr done, send close request */
		*rc = ums_close_final(conn);
}

static void ums_close_active_on_app_fin_close_wait(struct ums_sock *ums, int *rc)
{
	ums_close_active_on_ums_peer_close_wait(ums, rc);
}

static bool ums_close_active_on_ums_app_close_wait(struct ums_sock *ums, int *rc, long timeout)
{
	struct ums_connection *conn = &ums->conn;
	struct sock *sk = &ums->sk;
	bool again = false;

	do {
		if (!ums_cdc_rxed_any_close(conn))
			ums_close_stream_wait(ums, timeout);
		release_sock(sk);
		(void)cancel_delayed_work_sync(&conn->tx_work);
		lock_sock(sk);
		if (sk->sk_state != (unsigned char)UMS_APPCLOSEWAIT1 &&
			sk->sk_state != (unsigned char)UMS_APPCLOSEWAIT2) {
			again = true;
			break;
		}
		/* confirm close from peer */
		*rc = ums_close_final(conn);
		if (ums_cdc_rxed_any_close(conn)) {
			/* peer has closed the socket already */
			sk->sk_state = (unsigned char)UMS_CLOSED;
			sock_put(sk); /* postponed passive closing */
		} else {
			/* peer has just issued a shutdown write */
			sk->sk_state = (unsigned char)UMS_PEERFINCLOSEWAIT;
		}
	} while (0);

	return again;
}

static bool ums_close_active_inner(struct ums_sock *ums, struct sock *sk, long timeout, int *rc)
{
	struct ums_connection *conn = &ums->conn;
	bool again = false;

	switch (sk->sk_state) {
	case UMS_INIT:
		sk->sk_state = (unsigned char)UMS_CLOSED;
		break;
	case UMS_LISTEN:
		ums_close_active_on_ums_listen(ums, rc);
		break;
	case UMS_ACTIVE:
		if (ums_close_active_on_ums_active(ums, rc, timeout))
			/* peer event has changed the state */
			again = true;
		break;
	case UMS_APPFINCLOSEWAIT:
		/* socket already shutdown wr or both (active close) */
		ums_close_active_on_app_fin_close_wait(ums, rc);
		sk->sk_state = (unsigned char)UMS_CLOSED;
		break;
	case UMS_APPCLOSEWAIT1:
	case UMS_APPCLOSEWAIT2:
		if (ums_close_active_on_ums_app_close_wait(ums, rc, timeout))
			again = true;
		break;
	case UMS_PEERCLOSEWAIT1:
	case UMS_PEERCLOSEWAIT2:
		ums_close_active_on_ums_peer_close_wait(ums, rc);
		/* peer sending PeerConnectionClosed will cause transition */
		break;
	case UMS_PROCESSABORT:
		*rc = ums_close_abort(conn);
		sk->sk_state = (unsigned char)UMS_CLOSED;
		break;
	case UMS_PEERABORTWAIT:
		sk->sk_state = (unsigned char)UMS_CLOSED;
		sock_put(sk); /* (postponed) passive closing */
		break;
	case UMS_CLOSED:
	case UMS_PEERFINCLOSEWAIT:
		break;
	default:
		UMS_LOGE("Invalid sk state.");
		break;
	}

	return again;
}

static bool ums_close_shutdown_write_inner(struct ums_sock *ums, struct sock *sk, long timeout,
	int *rc)
{
	struct ums_connection *conn = &ums->conn;
	bool again = false;

	switch (sk->sk_state) {
	case UMS_ACTIVE:
		ums_close_stream_wait(ums, timeout);
		release_sock(sk);
		(void)cancel_delayed_work_sync(&conn->tx_work);
		lock_sock(sk);
		if (sk->sk_state != (unsigned char)UMS_ACTIVE) {
			again = true;
			break;
		}
		/* send close wr request */
		*rc = ums_close_wr(conn);
		sk->sk_state = (unsigned char)UMS_PEERCLOSEWAIT1;
		break;
	case UMS_APPCLOSEWAIT1:
		/* passive close */
		if (!ums_cdc_rxed_any_close(conn))
			ums_close_stream_wait(ums, timeout);
		release_sock(sk);
		(void)cancel_delayed_work_sync(&conn->tx_work);
		lock_sock(sk);
		if (sk->sk_state != (unsigned char)UMS_APPCLOSEWAIT1) {
			again = true;
			break;
		}
		/* confirm close from peer */
		*rc = ums_close_wr(conn);
		sk->sk_state = (unsigned char)UMS_APPCLOSEWAIT2;
		break;
	case UMS_APPCLOSEWAIT2:
	case UMS_PEERFINCLOSEWAIT:
	case UMS_PEERCLOSEWAIT1:
	case UMS_PEERCLOSEWAIT2:
	case UMS_APPFINCLOSEWAIT:
	case UMS_PROCESSABORT:
	case UMS_PEERABORTWAIT:
		break;
	default:
		UMS_LOGE("Invalid sk state.");
		break;
	}

	return again;
}

static int ums_close_with_cmd(struct ums_sock *ums, const enum sock_shutdown_cmd cmd)
{
	struct sock *sk = &ums->sk;
	bool again = true;
	int old_state;
	long timeout;
	int rc = 0;

	timeout = (current->flags & PF_EXITING) != 0 ? 0 : sock_flag(sk, SOCK_LINGER) ?
		(long)sk->sk_lingertime : UMS_MAX_STREAM_WAIT_TIMEOUT;

	old_state = sk->sk_state;

	while (again) {
		switch (cmd) {
		case SHUT_RDWR:
			if (!ums_close_active_inner(ums, sk, timeout, &rc))
				again = false;
			break;
		case SHUT_WR:
			if (!ums_close_shutdown_write_inner(ums, sk, timeout, &rc))
				again = false;
			break;
		case SHUT_RD:
		default:
			break;
		}
	}

	if (old_state != sk->sk_state)
		sk->sk_state_change(sk);
	return rc;
}

int ums_close_active(struct ums_sock *ums)
{
	return ums_close_with_cmd(ums, SHUT_RDWR);
}

int ums_close_shutdown_write(struct ums_sock *ums)
{
	return ums_close_with_cmd(ums, SHUT_WR);
}

#ifdef UMS_UT_TEST
void ums_close_passive_abort_received(struct ums_sock *ums)
#else
static void ums_close_passive_abort_received(struct ums_sock *ums)
#endif
{
	struct ums_cdc_conn_state_flags *txflags = &ums->conn.local_tx_ctrl.conn_state_flags;
	struct sock *sk = &ums->sk;

	switch (sk->sk_state) {
	case UMS_INIT:
	case UMS_ACTIVE:
	case UMS_APPCLOSEWAIT1:
		sk->sk_state = (unsigned char)UMS_PROCESSABORT;
		sock_put(sk); /* passive closing */
		break;
	case UMS_APPFINCLOSEWAIT:
		sk->sk_state = (unsigned char)UMS_PROCESSABORT;
		break;
	case UMS_PEERCLOSEWAIT1:
	case UMS_PEERCLOSEWAIT2:
		if ((txflags->peer_done_writing != 0) && !ums_close_sent_any_close(&ums->conn))
			/* just shutdown, but not yet closed locally */
			sk->sk_state = (unsigned char)UMS_PROCESSABORT;
		else
			sk->sk_state = (unsigned char)UMS_CLOSED;
		sock_put(sk); /* passive closing */
		break;
	case UMS_APPCLOSEWAIT2:
	case UMS_PEERFINCLOSEWAIT:
		sk->sk_state = (unsigned char)UMS_CLOSED;
		sock_put(sk); /* passive closing */
		break;
	case UMS_PEERABORTWAIT:
		sk->sk_state = (unsigned char)UMS_CLOSED;
		sock_put(sk); /* passive closing */
		break;
	case UMS_PROCESSABORT:
		break;
	default:
		UMS_LOGE("Invalid sk state.");
		break;
	}
}

/* Either some kind of closing has been received: peer_conn_closed,
 * peer_conn_abort, or peer_done_writing
 * or the link group of the connection terminates abnormally.
 */
static void ums_close_passive_work(struct work_struct *work)
{
	struct ums_connection *conn = container_of(work, struct ums_connection, close_work);
	struct ums_sock *ums = container_of(conn, struct ums_sock, conn);
	struct ums_cdc_conn_state_flags *rx_flags;
	bool release_clcsock = false;
	struct sock *sk = &ums->sk;
	int old_state;

	lock_sock(sk);

	rx_flags = &conn->local_rx_ctrl.conn_state_flags;
	old_state = sk->sk_state;
	if (rx_flags->peer_conn_abort != 0) {
		/* peer has not received all data */
		ums_close_passive_abort_received(ums);
		release_sock(sk);
		(void)cancel_delayed_work_sync(&conn->tx_work);
		lock_sock(sk);
		goto wakeup;
	}

	switch (sk->sk_state) {
	case UMS_INIT:
		sk->sk_state = (unsigned char)UMS_APPCLOSEWAIT1;
		break;
	case UMS_ACTIVE:
		sk->sk_state = (int)UMS_APPCLOSEWAIT1;
		/* postpone sock_put() for passive closing to cover
		 * received SEND_SHUTDOWN as well
		 */
		break;
	case UMS_PEERCLOSEWAIT1:
		if (rx_flags->peer_done_writing != 0)
			sk->sk_state = (unsigned char)UMS_PEERCLOSEWAIT2;
		fallthrough;
		/* to check for closing */
	case UMS_PEERCLOSEWAIT2:
		if (!ums_cdc_rxed_any_close(conn))
			break;
		if (sock_flag(sk, SOCK_DEAD) && ums_close_sent_any_close(conn)) {
			/* ums_release has already been called locally */
			sk->sk_state = (unsigned char)UMS_CLOSED;
		} else {
			/* just shutdown, but not yet closed locally */
			sk->sk_state = (unsigned char)UMS_APPFINCLOSEWAIT;
		}
		sock_put(sk); /* passive closing */
		break;
	case UMS_PEERFINCLOSEWAIT:
		if (ums_cdc_rxed_any_close(conn)) {
			sk->sk_state = (unsigned char)UMS_CLOSED;
			sock_put(sk); /* passive closing */
		}
		break;
	case UMS_APPCLOSEWAIT1:
	case UMS_APPCLOSEWAIT2:
		/* postpone sock_put() for passive closing to cover
		 * received SEND_SHUTDOWN as well
		 */
		break;
	case UMS_APPFINCLOSEWAIT:
	case UMS_PEERABORTWAIT:
	case UMS_PROCESSABORT:
	case UMS_CLOSED:
		break;
	default:
		UMS_LOGE("Invalid sk state.");
		break;
	}

wakeup:
	sk->sk_data_ready(sk);  /* wakeup blocked rcvbuf consumers */
	sk->sk_write_space(sk); /* wakeup blocked sndbuf producers */

	if (old_state != sk->sk_state) {
		sk->sk_state_change(sk);
		if ((sk->sk_state == (unsigned char)UMS_CLOSED) && (sock_flag(sk, SOCK_DEAD) || !sk->sk_socket)) {
			ums_conn_free(conn);
			if (ums->clcsock)
				release_clcsock = true;
		}
	}
	release_sock(sk);
	if (release_clcsock)
		ums_clcsock_release(ums);
	sock_put(sk); /* sock_hold done by schedulers of close_work */
}

/* Initialize close properties on connection establishment. */
void ums_close_init(struct ums_sock *ums)
{
	INIT_WORK(&ums->conn.close_work, ums_close_passive_work);
}

#ifdef UMS_UT_TEST
EXPORT_SYMBOL(ums_close_abort);
EXPORT_SYMBOL(ums_close_active_abort);
EXPORT_SYMBOL(ums_close_active);
EXPORT_SYMBOL(ums_close_passive_abort_received);
#endif
