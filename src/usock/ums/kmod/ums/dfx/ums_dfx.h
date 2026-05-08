/* SPDX-License-Identifier: GPL-2.0 */
/*
 * UB Memory based Socket(UMS)
 *
 * Description:UMS DFX support functions header file
 *
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2025. All rights reserved.
 *
 * UMS implementation:
 *     Author(s): LI Yuxing
 */

#ifndef UMS_DFX_H
#define UMS_DFX_H

#include <linux/socket.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#define UMS_PRINT_EID_STRING_SIZE (UMS_EID_SIZE * 2 + 8)

/* ums proc file system support */
struct ums_iter_state {
	struct seq_net_private  p;
	int			            offset;
};

extern struct ums_sysctl_config g_ums_sysctl_conf;

int ums4_seq_show(struct seq_file *seq, void *v);
int ums6_seq_show(struct seq_file *seq, void *v);
void *ums_seq_start(struct seq_file *seq, loff_t *pos);
void *ums_seq_next(struct seq_file *seq, void *v, loff_t *pos);
void ums_seq_stop(struct seq_file *seq, void *v);
int __net_init ums_sysctl_net_init(struct net *net);
void __net_exit ums_sysctl_net_exit(const struct net *net);
#endif /* UMS_DFX_H */
