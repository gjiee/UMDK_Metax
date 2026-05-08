// SPDX-License-Identifier: GPL-2.0-only
/*
 * UB Memory based Socket(UMS)
 *
 * Description:UMS accept ops implementation
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

#include "ums_rx.h"
#include "ums_accept.h"

#define UMS_DEFER_ACCEPT_DEFAULT 10

/* remove a socket from the accept queue of its parental listening socket */
static void ums_accept_unlink(struct sock *sk)
{
	struct ums_sock *par = ums_sk(sk)->listen_ums;

	spin_lock(&par->accept_q_lock);
	list_del_init(&ums_sk(sk)->accept_q);
	sk_acceptq_removed(&ums_sk(sk)->listen_ums->sk);
	spin_unlock(&par->accept_q_lock);
	sock_put(sk); /* sock_hold in ums_accept_enqueue */
}

/* remove a sock from the accept queue to bind it to a new socket created
 * for a socket accept call from user space
 */
struct sock *ums_accept_dequeue(struct sock *parent, struct socket *new_sock)
{
	struct ums_sock *isk, *n;
	struct sock *new_sk;

	list_for_each_entry_safe (isk, n, &ums_sk(parent)->accept_q, accept_q) {
		new_sk = (struct sock *)isk;

		ums_accept_unlink(new_sk);
		if (new_sk->sk_state == (unsigned char)UMS_CLOSED) {
			new_sk->sk_prot->unhash(new_sk);
			down_write(&isk->clcsock_release_lock);
			if (isk->clcsock) {
				sock_release(isk->clcsock);
				isk->clcsock = NULL;
			}
			up_write(&isk->clcsock_release_lock);
			sock_put(new_sk); /* final */
			continue;
		}
		if (new_sock) {
			sock_graft(new_sk, new_sock);
			new_sock->state = SS_CONNECTED;
			if (isk->use_fallback) {
				ums_sk(new_sk)->clcsock->file = new_sock->file;
				isk->clcsock->file->private_data = isk->clcsock;
			}
		}
		return new_sk;
	}
	return NULL;
}

static void ums_process_data_on_socket(const struct ums_sock *lums, struct sock *dst_sk)
{
	long timeo;
	int defer_accept = lums->sockopt_defer_accept;

	if (defer_accept < 0) {
		defer_accept = UMS_DEFER_ACCEPT_DEFAULT;
	} else if (defer_accept > UINT_MAX / MSEC_PER_SEC) {
		defer_accept = UINT_MAX / MSEC_PER_SEC;
	}
	/* wait till data arrives on the socket */
	timeo = (long)msecs_to_jiffies((unsigned int)(defer_accept * MSEC_PER_SEC));
	if (ums_sk(dst_sk)->use_fallback) {
		struct sock *clcsk = ums_sk(dst_sk)->clcsock->sk;

		lock_sock(clcsk);
		if (skb_queue_empty(&clcsk->sk_receive_queue) != 0)
			(void)sk_wait_data(clcsk, &timeo, NULL);
		release_sock(clcsk);
	} else if (atomic_read(&ums_sk(dst_sk)->conn.bytes_to_rcv) == 0) {
		lock_sock(dst_sk);
		(void)ums_rx_wait(ums_sk(dst_sk), &timeo, ums_rx_data_available);
		release_sock(dst_sk);
	}
}

int ums_accept(struct socket *sock, struct socket *new_sock, int flags, bool kern)
{
	struct sock *sk = sock->sk, *dst_sk;
	struct ums_sock *lums;
	bool waited = false;
	DEFINE_WAIT(wait);
	long timeo;
	int rc = 0;

	lums = ums_sk(sk);
	sock_hold(sk); /* sock_put below */
	lock_sock(sk);

	if (lums->sk.sk_state != (unsigned char)UMS_LISTEN) {
		rc = -EINVAL;
		release_sock(sk);
		goto out;
	}

	timeo = sock_rcvtimeo(sk, ((u32)flags) & O_NONBLOCK);
	while (!(dst_sk = ums_accept_dequeue(sk, new_sock))) {
		if (timeo == 0) {
			rc = -EAGAIN;
			break;
		}
		/* Wait for an incoming connection */
		(void)prepare_to_wait_exclusive(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);
		waited = true;
		release_sock(sk);
		if (ums_accept_queue_empty(sk))
			timeo = schedule_timeout(timeo);
		/* wakeup by sk_data_ready in ums_listen_work() */
		sched_annotate_sleep();
		lock_sock(sk);
		if (signal_pending(current) != 0) {
			rc = sock_intr_errno(timeo);
			break;
		}
	}

	if (waited)
		finish_wait(sk_sleep(sk), &wait);

	if (rc == 0)
		rc = sock_error(dst_sk);
	release_sock(sk);
	if (rc != 0)
		goto out;

	if ((lums->sockopt_defer_accept != 0) && ((((u32)flags) & O_NONBLOCK) == 0))
		ums_process_data_on_socket(lums, dst_sk);

out:
	sock_put(sk); /* sock_hold above */
	return rc;
}

#ifdef UMS_UT_TEST
EXPORT_SYMBOL(ums_accept);
#endif
