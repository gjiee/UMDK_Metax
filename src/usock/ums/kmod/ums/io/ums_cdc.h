/* SPDX-License-Identifier: GPL-2.0 */
/*
 * UB Memory based Socket(UMS)
 *
 * Description:UMS Connection Data Control(CDC) header file
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

#ifndef UMS_CDC_H
#define UMS_CDC_H

#include <linux/atomic.h>
#include <linux/compiler.h>
#include <linux/in.h>
#include <linux/kernel.h> /* max_t */

#include "ums_core.h"
#include "ums_mod.h"
#include "ums_wr.h"

#define UMS_CDC_MSG_TYPE 0xFE
#define UMS_CDC_TX_WORK_DELAY 0
#define UMS_CDC_G_INSTANCE_NUM 8

/* in network byte order */
union ums_cdc_cursor { /* UMS cursor */
	struct {
		__be32 count;
		__be16 wrap;
		__be16 reserved;
	};
#ifdef KERNEL_HAS_ATOMIC64
	atomic64_t acurs; /* for atomic processing */
#else
	u64 acurs; /* for atomic processing */
#endif
} __aligned(UMS_CDC_G_INSTANCE_NUM);

/* in network byte order */
struct ums_cdc_msg {
	struct ums_wr_rx_hdr common; /* .type = 0xFE */
	u8 len;                      /* 44 */
	__be16 seqno;
	__be32 token;
	union ums_cdc_cursor prod;
	union ums_cdc_cursor cons; /* piggy backed "ack" */
	struct ums_cdc_producer_flags prod_flags;
	struct ums_cdc_conn_state_flags conn_state_flags;
	u8 credits; /* credits is synced by each imm */
	u8 reserved[17];
};

struct ums_cdc_tx_pend {
	struct ums_connection *conn; /* socket connection */
	union ums_host_cursor cursor;   /* tx sndbuf cursor sent */
	union ums_host_cursor p_cursor; /* rx RMBE cursor produced */
	u16 ctrl_seq;                /* conn. tx sequence of CDC */
};

struct ums_imm_tx_pend {
	struct ums_connection *conn;    /* socket connection */
	union ums_host_cursor cursor;   /* tx sndbuf cursor sent */
	union ums_host_cursor p_cursor; /* rx RMBE cursor produced */
};

/* Use Write_with_imm to replace Write + CDC */
union ums_imm {
	struct {
		u32 credits : 8;       /* 256 credits in current version */
		u32 write_blocked : 1; /* set if to_send >= rmbespace */
		u32 skip_flag : 1;     /* set if two writes */
		u32 token : 22;
	};
	u32 data;
};

struct ums_get_free_slot_param {
	struct ums_connection *conn;
	struct ums_link *link;
	struct ums_wr_buf **wr_buf;
	struct ubcore_jfs_wr **wr_ub_buf;
	struct ums_wr_tx_pend_priv **pend;
	ums_wr_tx_handler handler;
};

static inline bool ums_cdc_rxed_any_close(const struct ums_connection *conn)
{
	return (conn->local_rx_ctrl.conn_state_flags.peer_conn_abort != 0) ||
		(conn->local_rx_ctrl.conn_state_flags.peer_conn_closed != 0);
}

static inline bool ums_cdc_rxed_any_close_or_senddone(struct ums_connection *conn)
{
	return ums_cdc_rxed_any_close(conn) ||
		(conn->local_rx_ctrl.conn_state_flags.peer_done_writing != 0);
}

static inline void ums_curs_add(int size, union ums_host_cursor *curs, int value)
{
	curs->count += (u32)value;
	if (curs->count >= (u32)size) {
		curs->wrap++;
		curs->count -= (u32)size;
	}
}

#ifndef KERNEL_HAS_ATOMIC64
static inline void ums_curs_copy_inner(u64 *tgt_acurs, u64 *src_acurs, struct ums_connection *conn)
{
	unsigned long flags;

	spin_lock_irqsave(&conn->acurs_lock, flags);
	*tgt_acurs = *src_acurs;
	spin_unlock_irqrestore(&conn->acurs_lock, flags);
}
#else
static inline void ums_curs_copy_inner(atomic64_t *tgt_acurs, atomic64_t *src_acurs,
	const struct ums_connection *conn)
{
	(void)conn;
	atomic64_set(tgt_acurs, atomic64_read(src_acurs));
}
#endif

/* Copy cursor src into tgt */
static inline void ums_curs_copy(
	union ums_host_cursor *tgt, union ums_host_cursor *src, struct ums_connection *conn)
{
	ums_curs_copy_inner(&tgt->acurs, &src->acurs, conn);
}

static inline void ums_curs_copy_net(union ums_cdc_cursor *tgt, union ums_cdc_cursor *src,
	struct ums_connection *conn)
{
	ums_curs_copy_inner(&tgt->acurs, &src->acurs, conn);
}

/* calculate cursor difference between old and new, where old <= new and
 * difference cannot exceed size
 */
static inline int ums_curs_diff(unsigned int size, const union ums_host_cursor *old,
	const union ums_host_cursor *new)
{
	if (old->wrap != new->wrap)
		return max_t(int, 0, ((size - old->count) + new->count));

	return max_t(int, 0, (new->count - old->count));
}

/* calculate cursor difference between old and new - returns negative
 * value in case old > new
 */
static inline int ums_curs_comp(unsigned int size, union ums_host_cursor *old,
	union ums_host_cursor *new)
{
	if (old->wrap > new->wrap || (old->wrap == new->wrap && old->count > new->count))
		return -ums_curs_diff(size, new, old);
	return ums_curs_diff(size, old, new);
}

/* calculate cursor difference between old and new, where old <= new and
 * difference may exceed size
 */
static inline int ums_curs_diff_large(unsigned int size, const union ums_host_cursor *old,
	const union ums_host_cursor *new)
{
	if (old->wrap < new->wrap)
		return min_t(int, (size - old->count) + new->count + ((new->wrap - old->wrap) - 1) * size,
			size);

	if (old->wrap > new->wrap) /* wrap has switched from 0xffff to 0x0000 */
		return min_t(int, (size - old->count) + new->count + (new->wrap + 0xffff - old->wrap) *
			size, size);

	return max_t(int, 0, (new->count - old->count));
}

static inline void ums_host_cursor_to_cdc(union ums_cdc_cursor *peer, union ums_host_cursor *local,
	union ums_host_cursor *save, struct ums_connection *conn)
{
	ums_curs_copy(save, local, conn);
	peer->count = htonl(save->count);
	peer->wrap = htons(save->wrap);
	/* peer->reserved = htons(0); must be ensured by caller */
}

static inline void ums_host_msg_to_cdc(struct ums_cdc_msg *peer, struct ums_connection *conn,
	union ums_host_cursor *save)
{
	struct ums_host_cdc_msg *local = &conn->local_tx_ctrl;

	peer->common.type = local->common.type;
	peer->len = local->len;
	peer->seqno = htons(local->seqno);
	peer->token = htonl(local->token);
	ums_host_cursor_to_cdc(&peer->prod, &local->prod, save, conn);
	ums_host_cursor_to_cdc(&peer->cons, &local->cons, save, conn);
	peer->prod_flags = local->prod_flags;
	peer->conn_state_flags = local->conn_state_flags;
}

static inline void ums_cdc_cursor_to_host(union ums_host_cursor *local, union ums_cdc_cursor *peer,
	struct ums_connection *conn)
{
	union ums_host_cursor temp, old;
	union ums_cdc_cursor net;

	ums_curs_copy(&old, local, conn);
	ums_curs_copy_net(&net, peer, conn);
	temp.count = ntohl(net.count);
	temp.wrap = ntohs(net.wrap);
	if (((old.wrap > temp.wrap) && (temp.wrap != 0)) ||
		((old.wrap == temp.wrap) && (old.count > temp.count)))
		return;
	ums_curs_copy(local, &temp, conn);
}

static inline void ums_cdc_msg_to_host(struct ums_host_cdc_msg *local, struct ums_cdc_msg *peer,
	struct ums_connection *conn)
{
	local->common.type = peer->common.type;
	local->len = peer->len;
	local->seqno = ntohs(peer->seqno);
	local->token = ntohl(peer->token);
	ums_cdc_cursor_to_host(&local->prod, &peer->prod, conn);
	ums_cdc_cursor_to_host(&local->cons, &peer->cons, conn);
	local->prod_flags = peer->prod_flags;
	local->conn_state_flags = peer->conn_state_flags;
}

int ums_cdc_get_free_slot(struct ums_connection *conn, struct ums_link *link,
	struct ums_wr_buf **wr_buf, struct ums_cdc_tx_pend **pend);
void ums_conn_wait_pend_tx_wr(struct ums_connection *conn);
int ums_cdc_msg_send(struct ums_connection *conn, struct ums_wr_buf *wr_buf,
	struct ums_cdc_tx_pend *pend);
int ums_cdc_get_slot_and_msg_send(struct ums_connection *conn);
int __init ums_cdc_init(void);
void ums_cdc_tx_pending(struct ums_connection *conn);
void ums_cdc_tx_work(struct work_struct *work);
int ums_imm_get_free_slot(struct ums_connection *conn, struct ums_link *link,
	struct ubcore_jfs_wr **wr_ub_buf, struct ums_imm_tx_pend **pend);
int ums_urg_get_free_slot(struct ums_connection *conn, struct ums_link *link,
	struct ums_wr_buf **wr_buf, struct ubcore_jfs_wr **wr_ub_buf, struct ums_cdc_tx_pend **pend);
#endif /* UMS_CDC_H */
