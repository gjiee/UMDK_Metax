/* SPDX-License-Identifier: GPL-2.0 */
/*
 * UB Memory based Socket(UMS)
 *
 * Decription:UMS Dynamic Interrupt Moderation (DIM) header file
 *
 * Copyright (c) 2022, Alibaba Group.
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2025. All rights reserved.
 *
 * UMS implementation:
 *     Author(s): ZHANG Weicheng
 */

#ifndef UMS_DIM_H
#define UMS_DIM_H

#include <linux/dim.h>

#include "ums_ubcore.h"

extern struct ums_sysctl_config g_ums_sysctl_conf;

struct ums_dim {
	struct dim dim;
	bool use_dim;
	u64 prev_idle;
	u64 prev_softirq;
	u64 prev_wall;
};

void ums_dim_init(struct ums_ubcore_jfc *ums_jfc);
void ums_dim_destroy(struct ums_ubcore_jfc *ums_jfc);
void ums_dim(struct dim *dim, u64 completions);

static inline struct ums_dim *to_umsdim(struct dim *dim)
{
	return (struct ums_dim *)dim;
}

static inline struct dim *to_dim(struct ums_dim *umsdim)
{
	return (struct dim *)umsdim;
}

#endif /* UMS_DIM_H */
