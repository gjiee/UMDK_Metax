/* SPDX-License-Identifier: GPL-2.0 */
/*
 * UB Memory based Socket(UMS)
 *
 * Description:UMS data-plane receive(rx) header file
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

#ifndef UMS_RX_H
#define UMS_RX_H

#include <linux/socket.h>
#include <linux/types.h>

#include "ums_mod.h"

struct ums_rx_recvmsg_len_status {
	size_t read_remaining;
	size_t copylen;
	int read_done;
	int readable;
	int splbytes;
};

struct ums_rx_splice_page_info {
	struct partial_page *partial;
	struct ums_spd_priv **priv;
	struct page **pages;
	int nr_pages;
};

static inline int ums_rx_data_available(struct ums_connection *conn)
{
	return atomic_read(&conn->bytes_to_rcv);
}

void ums_rx_init(struct ums_sock *ums);
int ums_rx_recvmsg(struct ums_sock *ums, struct msghdr *msg,
	struct pipe_inode_info *pipe, size_t len, int flags);
int ums_rx_wait(struct ums_sock *ums, long *timeo,
	int (*fcrit)(struct ums_connection *conn));
#endif /* UMS_RX_H */
