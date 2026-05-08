/* SPDX-License-Identifier: GPL-2.0 */
/*
 * UB Memory based Socket(UMS)
 *
 * Description:UMS data-plane transport(tx) header file
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

#ifndef UMS_TX_H
#define UMS_TX_H

#include <linux/version.h>
#include <linux/socket.h>
#include <linux/types.h>

#include "ums_mod.h"
#include "ums_cdc.h"

static inline int ums_tx_prepared_sends(struct ums_connection *conn)
{
	union ums_host_cursor sent, prep;

	ums_curs_copy(&sent, &conn->tx_curs_sent, conn);
	ums_curs_copy(&prep, &conn->tx_curs_prep, conn);
	return ums_curs_diff((unsigned int)conn->sndbuf_desc->len, &sent, &prep);
}

void ums_tx_pending(struct ums_connection *conn);
void ums_tx_work(struct work_struct *work);
void ums_tx_init(struct ums_sock *ums);
int ums_tx_sendmsg(struct ums_sock *ums, struct msghdr *msg, size_t len);
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0)
int ums_tx_sendpage(struct ums_sock *ums, struct page *page, int offset,
	size_t size, int flags);
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0) */
int ums_tx_sndbuf_nonempty(struct ums_connection *conn);
void ums_tx_sndbuf_nonfull(struct ums_sock *ums);
void ums_tx_consumer_update(struct ums_connection *conn, bool force);
#endif /* UMS_TX_H */
