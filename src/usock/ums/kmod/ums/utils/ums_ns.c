// SPDX-License-Identifier: GPL-2.0
/*
 * UB Memory based Socket(UMS)
 *
 * Description:UMS namespace(ns) implementation
 *
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2025. All rights reserved.
 *
 * UMS implementation:
 *     Author(s): LI Yuxing
 */

#include "ums_ns.h"

struct ums_sysctl_config g_ums_sysctl_conf = {
	.ums_hdr = NULL,
	.sysctl_autocorking_size = UMS_AUTOCORKING_DEFAULT_SIZE,
	.sysctl_sndbuf = UMS_SNDBUF_DEFAULT_SIZE,
	.sysctl_rcvbuf = UMS_RCVBUF_DEFAULT_SIZE,
	.dim_enable = 0,
};
