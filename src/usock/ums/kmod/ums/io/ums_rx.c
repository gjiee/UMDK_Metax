// SPDX-License-Identifier: GPL-2.0
/*
 * UB Memory based Socket(UMS)
 *
 * Description:UMS data-plane receive(rx) implementation
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

#include <linux/net.h>
#include <linux/rcupdate.h>
#include <linux/sched/signal.h>
#include <linux/pipe_fs_i.h>
#include <linux/splice.h>

#include <net/sock.h>

#include "ums_mod.h"
#include "ums_core.h"
#include "ums_cdc.h"
#include "ums_tx.h" /* ums_tx_consumer_update() */
#include "ums_rx.h"

#define UMS_CHUNK_MAX 2

struct ums_spd_priv {
	struct ums_sock *ums;
	size_t		 len;
};

/* callback implementation to wakeup consumers blocked with ums_rx_wait().
 * indirectly called by ums_cdc_msg_recv_action().
 */
static void ums_rx_wake_up(struct sock *sk)
{
	struct socket_wq *wq;

	/* derived from sock_def_readable() */
	/* called already in ums_listen_work() */
	rcu_read_lock();
	wq = rcu_dereference(sk->sk_wq);
	if (skwq_has_sleeper(wq))
		wake_up_interruptible_sync_poll(&wq->wait, EPOLLIN | EPOLLPRI | EPOLLRDNORM | EPOLLRDBAND);
	sk_wake_async(sk, SOCK_WAKE_WAITD, POLL_IN);
	if ((sk->sk_shutdown == SHUTDOWN_MASK) || (sk->sk_state == (unsigned char)UMS_CLOSED))
		sk_wake_async(sk, SOCK_WAKE_WAITD, POLL_HUP);
	rcu_read_unlock();
}

/* Update consumer cursor
 *   @ums   connection to update
 *   @cons   consumer cursor
 *   @len    number of Bytes consumed
 *   Returns:
 *   1 if we should end our receive, 0 otherwise
 */
static int ums_rx_update_consumer(struct ums_sock *ums, union ums_host_cursor cons, size_t len)
{
	struct ums_connection *conn = &ums->conn;
	struct sock *sk = &ums->sk;
	bool force = false;
	int diff, rc = 0;

	ums_curs_add(conn->rmb_desc->len, &cons, (int)len);

	/* did we process urgent data? */
	if (conn->urg_state == UMS_URG_VALID || conn->urg_rx_skip_pend) {
		diff = ums_curs_comp((unsigned int)conn->rmb_desc->len, &cons, &conn->urg_curs);
		if (sock_flag(sk, SOCK_URGINLINE)) {
			if (diff == 0) {
				force = true;
				rc = 1;
				conn->urg_state = UMS_URG_READ;
			}
		} else {
			if (diff == 1) {
				/* skip urgent byte */
				force = true;
				ums_curs_add(conn->rmb_desc->len, &cons, 1);
				conn->urg_rx_skip_pend = false;
			} else if (diff < -1) {
				/* we read past urgent byte */
				conn->urg_state = UMS_URG_READ;
			}
		}
	}

	ums_curs_copy(&conn->local_tx_ctrl.cons, &cons, conn);

	/* send consumer cursor update if required */
	/* similar to advertising new TCP rcv_wnd if required */
	ums_tx_consumer_update(conn, force);

	return rc;
}

static void ums_rx_update_cons(struct ums_sock *ums, size_t len)
{
	struct ums_connection *conn = &ums->conn;
	union ums_host_cursor cons;

	ums_curs_copy(&cons, &conn->local_tx_ctrl.cons, conn);
	(void)ums_rx_update_consumer(ums, cons, len);
}

static void ums_rx_pipe_buf_release(struct pipe_inode_info *pipe, struct pipe_buffer *buf)
{
	struct ums_spd_priv *priv = (struct ums_spd_priv *)buf->private;
	struct ums_sock *ums = priv->ums;
	struct ums_connection *conn;
	struct sock *sk = &ums->sk;

	if (sk->sk_state == (unsigned char)UMS_CLOSED ||
		sk->sk_state == (unsigned char)UMS_PEERFINCLOSEWAIT ||
	    sk->sk_state == (unsigned char)UMS_APPFINCLOSEWAIT)
		goto out;
	conn = &ums->conn;
	lock_sock(sk);
	ums_rx_update_cons(ums, priv->len);
	release_sock(sk);
	if (atomic_sub_and_test((int)priv->len, &conn->splice_pending))
		ums_rx_wake_up(sk);
out:
	kfree(priv);
	put_page(buf->page);
	sock_put(sk);
}

static const struct pipe_buf_operations ums_pipe_ops = {
	.release = ums_rx_pipe_buf_release,
	.get = generic_pipe_buf_get
};

static void ums_rx_spd_release(struct splice_pipe_desc *spd, unsigned int i)
{
	put_page(spd->pages[i]);
}

static void ums_rx_splice_init_base(char *src, size_t len, struct ums_sock *ums, int *offset,
	struct ums_rx_splice_page_info *page_info)
{
	int i;

	if (ums->conn.rmb_desc->is_vm == 0) {
		/* ums that uses physically contiguous RMBs */
		page_info->priv[0]->len = len;
		page_info->priv[0]->ums = ums;
		page_info->partial[0].offset = (unsigned int)(src - (char *)ums->conn.rmb_desc->cpu_addr);
		page_info->partial[0].len = (unsigned int)len;
		page_info->partial[0].private = (unsigned long)page_info->priv[0];
		page_info->pages[0] = ums->conn.rmb_desc->pages;
	} else {
		int size, left = (int)len;
		void *buf = src;
		/* ums that uses virtually contiguous RMBs */
		for (i = 0; i < page_info->nr_pages; i++) {
			size = min_t(int, ((int)PAGE_SIZE) - *offset, left);
			page_info->priv[i]->len = (u32)size;
			page_info->priv[i]->ums = ums;
			page_info->pages[i] = vmalloc_to_page(buf);
			page_info->partial[i].offset = (unsigned int)*offset;
			page_info->partial[i].len = (unsigned int)size;
			page_info->partial[i].private = (unsigned long)page_info->priv[i];
			buf += size;
			left -= size;
			*offset = 0;
		}
	}
}

static void ums_rx_splice_init_spd(struct splice_pipe_desc *spd,
	struct ums_rx_splice_page_info *page_info)
{
	spd->nr_pages_max = (unsigned int)page_info->nr_pages;
	spd->nr_pages = page_info->nr_pages;
	spd->pages = page_info->pages;
	spd->partial = page_info->partial;
	spd->ops = &ums_pipe_ops;
	spd->spd_release = ums_rx_spd_release;
}

static int ums_rx_splice(struct pipe_inode_info *pipe, char *src, size_t len, struct ums_sock *ums)
{
	struct ums_rx_splice_page_info page_info;
	struct splice_pipe_desc spd;
	int bytes, offset, i;

	offset = offset_in_page(src);
	page_info.nr_pages = (int)(ums->conn.rmb_desc->is_vm != 0 ?
		PAGE_ALIGN(len + ((u32)offset)) / PAGE_SIZE : 1);

	page_info.pages = kcalloc((u32)page_info.nr_pages, sizeof(*page_info.pages), GFP_KERNEL);
	if (!page_info.pages)
		goto out;
	page_info.partial = kcalloc((u32)page_info.nr_pages, sizeof(*page_info.partial), GFP_KERNEL);
	if (!page_info.partial)
		goto out_page;
	page_info.priv = kcalloc((u32)page_info.nr_pages, sizeof(*page_info.priv), GFP_KERNEL);
	if (!page_info.priv)
		goto out_part;
	for (i = 0; i < page_info.nr_pages; i++) {
		page_info.priv[i] = kzalloc(sizeof(**page_info.priv), GFP_KERNEL);
		if (!page_info.priv[i])
			goto out_priv;
	}

	ums_rx_splice_init_base(src, len, ums, &offset, &page_info);
	ums_rx_splice_init_spd(&spd, &page_info);

	bytes = (int)splice_to_pipe(pipe, &spd);
	if (bytes > 0) {
		sock_hold(&ums->sk);
		if (ums->conn.rmb_desc->is_vm != 0) {
			for (i = 0; i < (int)(PAGE_ALIGN((u32)(bytes + offset)) / PAGE_SIZE); i++)
				get_page(page_info.pages[i]);
		} else {
			get_page(ums->conn.rmb_desc->pages);
		}
		atomic_add(bytes, &ums->conn.splice_pending);
	}
	kfree(page_info.priv);
	kfree(page_info.partial);
	kfree(page_info.pages);

	return bytes;

out_priv:
	for (i = (i - 1); i >= 0; i--)
		kfree(page_info.priv[i]);
	kfree(page_info.priv);
out_part:
	kfree(page_info.partial);
out_page:
	kfree(page_info.pages);
out:
	return -ENOMEM;
}

static int ums_rx_data_available_and_no_splice_pend(struct ums_connection *conn)
{
	return (atomic_read(&conn->bytes_to_rcv) != 0) && (atomic_read(&conn->splice_pending) == 0);
}

/* blocks rcvbuf consumer until >=len bytes available or timeout or interrupted
 *   @ums    ums socket
 *   @timeo  pointer to max seconds to wait, pointer to value 0 for no timeout
 *   @fcrit  add'l criterion to evaluate as function pointer
 * Returns:
 * 1 if at least 1 byte available in rcvbuf or if socket error/shutdown.
 * 0 otherwise (nothing in rcvbuf nor timeout, e.g. interrupted).
 */
int ums_rx_wait(struct ums_sock *ums, long *timeo, int (*fcrit)(struct ums_connection *conn))
{
	DEFINE_WAIT_FUNC(wait, woken_wake_function);
	struct ums_connection *conn = &ums->conn;
	struct ums_cdc_conn_state_flags *cflags = &conn->local_tx_ctrl.conn_state_flags;
	struct sock *sk = &ums->sk;
	int rc;

	if (fcrit(conn) != 0)
		return 1;
	sk_set_bit(SOCKWQ_ASYNC_WAITDATA, sk);
	add_wait_queue(sk_sleep(sk), &wait);
	rc = sk_wait_event(sk, timeo, (sk->sk_err != 0) || (cflags->peer_conn_abort != 0) ||
		((sk->sk_shutdown & RCV_SHUTDOWN) != 0) || (conn->killed != 0) || (fcrit(conn) != 0),
		&wait);
	remove_wait_queue(sk_sleep(sk), &wait);
	sk_clear_bit(SOCKWQ_ASYNC_WAITDATA, sk);
	return rc;
}

static int ums_rx_recv_urg(struct ums_sock *ums, struct msghdr *msg, int len, int flags)
{
	struct ums_connection *conn = &ums->conn;
	union ums_host_cursor cons;
	struct sock *sk = &ums->sk;
	int rc = 0;

	if (sock_flag(sk, SOCK_URGINLINE) || !(conn->urg_state == UMS_URG_VALID) ||
		conn->urg_state == UMS_URG_READ)
		return -EINVAL;

	if (conn->urg_state == UMS_URG_VALID) {
		if ((((u32)flags) & MSG_PEEK) == 0)
			ums->conn.urg_state = UMS_URG_READ;
		msg->msg_flags |= MSG_OOB;
		if (len > 0) {
			if ((((u32)flags) & MSG_TRUNC) == 0)
				rc = memcpy_to_msg(msg, &conn->urg_rx_byte, 1);
			len = 1;
			ums_curs_copy(&cons, &conn->local_tx_ctrl.cons, conn);
			if (ums_curs_diff((unsigned int)conn->rmb_desc->len, &cons, &conn->urg_curs) > 1)
				conn->urg_rx_skip_pend = true;
			/* Urgent Byte was already accounted for, but trigger
			 * skipping the urgent byte in non-inline case
			 */
			if ((((u32)flags) & MSG_PEEK) == 0)
				(void)ums_rx_update_consumer(ums, cons, 0);
		} else {
			msg->msg_flags |= MSG_TRUNC;
		}

		return rc != 0 ? -EFAULT : len;
	}

	if ((sk->sk_state == (unsigned char)UMS_CLOSED) || ((sk->sk_shutdown & RCV_SHUTDOWN) != 0))
		return 0;

	return -EAGAIN;
}

static bool ums_rx_recvmsg_data_available(struct ums_sock *ums)
{
	struct ums_connection *conn = &ums->conn;

	if (ums_rx_data_available(conn) != 0)
		return true;
	else if (conn->urg_state == UMS_URG_VALID)
		/* we received a single urgent Byte - skip */
		ums_rx_update_cons(ums, 0);
	return false;
}

static bool ums_rx_readdone_check(int *read_done, struct sock *sk, long timeo)
{
	if (*read_done != 0) {
		if ((sk->sk_err != 0) || (sk->sk_state == (unsigned char)UMS_CLOSED) || (timeo == 0) ||
			(signal_pending(current) != 0))
			return false;
	} else {
		if (sk->sk_err != 0) {
			*read_done = sock_error(sk);
			return false;
		}
		if (sk->sk_state == (unsigned char)UMS_CLOSED) {
			if (!sock_flag(sk, SOCK_DONE))
				/* This occurs when user tries to read from never connected socket. */
				*read_done = -ENOTCONN;
			return false;
		}
		if (timeo == 0) {
			*read_done = -EAGAIN;
			return false;
		}
		if (signal_pending(current) != 0) {
			*read_done = sock_intr_errno(timeo);
			return false;
		}
	}

	return true;
}

static bool ums_rx_wait_check(struct ums_sock *ums, const struct msghdr *msg, long timeo,
	struct ums_rx_recvmsg_len_status *len_status)
{
	struct ums_connection *conn = &ums->conn;
	int (*func)(struct ums_connection *conn);

	len_status->splbytes = atomic_read(&conn->splice_pending);
	len_status->readable = atomic_read(&conn->bytes_to_rcv);
	if ((len_status->readable == 0) || (msg && (len_status->splbytes != 0))) {
		if (len_status->splbytes != 0)
			func = ums_rx_data_available_and_no_splice_pend;
		else
			func = ums_rx_data_available;
		(void)ums_rx_wait(ums, &timeo, func);
		return false;
	}

	return true;
}

static size_t ums_rx_get_copylen(struct ums_sock *ums, union ums_host_cursor *cons,
	struct ums_rx_recvmsg_len_status *len_status)
{
	struct ums_connection *conn = &ums->conn;

	ums_curs_copy(cons, &conn->local_tx_ctrl.cons, conn);
	/* subsequent splice() calls pick up where previous left */
	if (len_status->splbytes != 0)
		ums_curs_add(conn->rmb_desc->len, cons, len_status->splbytes);
	if ((conn->urg_state == UMS_URG_VALID) && sock_flag(&ums->sk, SOCK_URGINLINE) &&
		(len_status->readable > 1))
		len_status->readable--; /* always stop at urgent Byte */
	/* not more than what user space asked for */
	return min_t(size_t, len_status->read_remaining, (u32)len_status->readable);
}

static bool ums_rx_recvmsg_update_curser(struct ums_sock *ums, const struct msghdr *msg, int flags,
	size_t copylen, const union ums_host_cursor *cons)
{
	struct ums_connection *conn = &ums->conn;

	if ((((u32)flags) & MSG_PEEK) == 0) {
		/* increased in recv tasklet ums_cdc_msg_rcv() */
		smp_mb__before_atomic();
		atomic_sub((int)copylen, &conn->bytes_to_rcv);
		/* guarantee 0 <= bytes_to_rcv <= rmb_desc->len */
		smp_mb__after_atomic();
		if (msg && (ums_rx_update_consumer(ums, *cons, copylen) != 0))
			return false;
	}

	return true;
}

static int ums_rx_recvmsg_do(struct ums_sock *ums, struct msghdr *msg,
	struct pipe_inode_info *pipe, union ums_host_cursor *cons, int flags,
	struct ums_rx_recvmsg_len_status *len_status)
{
	size_t chunk_len, chunk_off, chunk_len_sum;
	struct ums_connection *conn = &ums->conn;
	char *rcvbuf_base;
	int chunk, rc;

	/* we use 1 RMBE per RMB, so RMBE == RMB base addr */
	rcvbuf_base = conn->rx_off + conn->rmb_desc->cpu_addr;
	chunk_len = min_t(size_t, len_status->copylen, conn->rmb_desc->len - conn->rx_off - cons->count);
	chunk_off = cons->count;
	chunk_len_sum = chunk_len;

	for (chunk = 0; chunk < UMS_CHUNK_MAX; chunk++) {
		if ((((u32)flags) & MSG_TRUNC) == 0) {
			if (msg)
				rc = (int)memcpy_to_msg(msg, rcvbuf_base + chunk_off, (int)chunk_len);
			else
				rc = ums_rx_splice(pipe, rcvbuf_base + chunk_off, chunk_len, ums);
				
			if (rc < 0)
				return -EFAULT;
		}
			
		len_status->read_remaining -= chunk_len;
		len_status->read_done += (int)chunk_len;

		if (chunk_len_sum == len_status->copylen) /* either on 1st or 2nd iteration */
			return 0;

		/* prepare next (== 2nd) iteration */
		chunk_len = len_status->copylen - chunk_len; /* remainder */
		chunk_len_sum += chunk_len;
		chunk_off = 0; /* modulo offset in recv ring buffer */

		if ((conn->rx_off + chunk_len) > conn->rmb_desc->len) {
			UMS_LOGE("chunk_len error! conn %u, jetty[eid:%pI6c, id:%u], rx_off: %u, copylen: %lu, rmb_desc->len: %d, "
				"cons[wrap: %u, count: %u], chunk_len: %lu, chunk_off: %lu, read_remaining: %lu, readable: %d, "
				"bytes_to_rcv: %d",
				conn->conn_id, &conn->lnk->ub_jetty->jetty_id.eid, conn->lnk->ub_jetty->jetty_id.id,
				conn->rx_off, len_status->copylen, conn->rmb_desc->len, cons->wrap, cons->count, chunk_len,
				chunk_off, len_status->read_remaining, len_status->readable, atomic_read(&conn->bytes_to_rcv));
			return -EFAULT;
		}
	}

	return 0;
}

static void ums_rx_len_status_init(struct ums_rx_recvmsg_len_status *len_status, size_t len)
{
	len_status->copylen = 0;
	len_status->read_remaining = len;
	len_status->read_done = 0;
	len_status->readable = 0;
	len_status->splbytes = 0;
}

/* ums_rx_recvmsg - receive data from RMBE
 * @msg:	copy data to receive buffer
 * @pipe:	copy data to pipe if set - indicates splice() call
 *
 * rcvbuf consumer: main API called by socket layer.
 * Called under sk lock.
 */
int ums_rx_recvmsg(struct ums_sock *ums, struct msghdr *msg, struct pipe_inode_info *pipe,
	size_t len, int flags)
{
	struct ums_rx_recvmsg_len_status len_status;
	struct ums_connection *conn = &ums->conn;
	union ums_host_cursor cons;
	struct sock *sk = &ums->sk;
	int target; /* Read at least target many bytes */
	long timeo;

	ums_rx_len_status_init(&len_status, len);

	if (unlikely((((u32)flags) & MSG_ERRQUEUE) != 0))
		return -EINVAL;

	if (sk->sk_state == (unsigned char)UMS_LISTEN)
		return -ENOTCONN;

	ums_conn_tx_rx_refcnt_inc(conn);
	if (conn->freed != 0) {
		ums_conn_tx_rx_refcnt_dec(conn);
		return -EPIPE;
	}

	if ((((u32)flags) & MSG_OOB) != 0) {
		int ret = ums_rx_recv_urg(ums, msg, (int)len, flags);
		ums_conn_tx_rx_refcnt_dec(conn);
		return ret;
	}

	timeo = sock_rcvtimeo(sk, ((u32)flags) & MSG_DONTWAIT);
	target = sock_rcvlowat(sk, (int)(((u32)flags) & MSG_WAITALL), (int)len);

	do { /* while (read_remaining) */
		if ((len_status.read_done >= target) ||
			(pipe && len_status.read_done != 0) ||
			(conn->killed != 0))
			break;

		if (ums_rx_recvmsg_data_available(ums))
			goto copy;

		if ((sk->sk_shutdown & RCV_SHUTDOWN) != 0) {
			/* ums_cdc_msg_recv_action() could have run after
			 * above ums_rx_recvmsg_data_available()
			 */
			if (ums_rx_recvmsg_data_available(ums))
				goto copy;
			break;
		}

		if (!ums_rx_readdone_check(&len_status.read_done, sk, timeo))
			break;

		if (ums_rx_data_available(conn) == 0) {
			(void)ums_rx_wait(ums, &timeo, ums_rx_data_available);
			continue;
		}

copy:
		/* initialize variables for first iteration of subsequent loop */
		/* could be just 1 byte, even after waiting on data above */
		if (!ums_rx_wait_check(ums, msg, timeo, &len_status))
			continue;

		len_status.copylen = ums_rx_get_copylen(ums, &cons, &len_status);
		conn->rx_bytes += len_status.copylen;
		/* determine chunks where to read from rcvbuf */
		/* either unwrapped case, or 1st chunk of wrapped case */
		if (ums_rx_recvmsg_do(ums, msg, pipe, &cons, flags, &len_status) != 0) {
			ums_conn_tx_rx_refcnt_dec(conn);
			return -EFAULT;
		}
		
		++conn->rx_cnt;

		/* update cursors */
		if (!ums_rx_recvmsg_update_curser(ums, msg, flags, len_status.copylen, &cons))
			goto out;
	} while (len_status.read_remaining > 0);
out:
	ums_conn_tx_rx_refcnt_dec(conn);
	return (int)len_status.read_done;
}

/* Initialize receive properties on connection establishment. NB: not __init! */
void ums_rx_init(struct ums_sock *ums)
{
	ums->sk.sk_data_ready = ums_rx_wake_up;
	atomic_set(&ums->conn.splice_pending, 0);
	ums->conn.urg_state = UMS_URG_READ;
}
