/* SPDX-License-Identifier: GPL-2.0 */
/*
 * UB Memory based Socket(UMS)
 *
 * Description:UMS link and link group process header file
 *
 * Copyright IBM Corp. 2016
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2025. All rights reserved.
 *
 * Original SMC-R implementation:
 *     Author(s): Klaus Wacker <Klaus.Wacker@de.ibm.com>
 *  			  Ursula Braun <ubraun@linux.vnet.ibm.com>
 *
 * UMS implementation:
 *     Author(s): Sun fang
 */

#ifndef UMS_PROCESS_LINK_H
#define UMS_PROCESS_LINK_H

#include <net/sock.h>
#include <linux/socket.h>

void ums_llc_lgr_clear(struct ums_link_group *lgr);
void ums_llc_lgr_init(struct ums_link_group *lgr, struct ums_sock *ums);
int ums_llc_link_init(struct ums_link *link);
void ums_llc_link_clear(struct ums_link *link, bool log);
#endif /* UMS_PROCESS_LINK_H */
