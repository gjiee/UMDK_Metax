/* SPDX-License-Identifier: GPL-2.0 */
/*
 * UB Memory based Socket(UMS)
 *
 * Description:UMS namespace(ns) header file
 *
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2025. All rights reserved.
 *
 * UMS implementation:
 *     Author(s): LI Yuxing
 */

#ifndef UMS_NS_H
#define UMS_NS_H

#include <linux/sysctl.h>

#define UMS_SNDBUF_DEFAULT_SIZE (1 * 1024 * 1024) /* 1MB by default */
#define UMS_RCVBUF_DEFAULT_SIZE (1 * 1024 * 1024) /* 1MB by default */
#define UMS_AUTOCORKING_DEFAULT_SIZE ((UMS_SNDBUF_DEFAULT_SIZE) >> 1)

struct ums_sysctl_config {
	struct ctl_table_header *ums_hdr;
	unsigned int sysctl_autocorking_size;
	int sysctl_sndbuf;
	int sysctl_rcvbuf;
	int dim_enable;
};
#endif /* UMS_NS_H */
