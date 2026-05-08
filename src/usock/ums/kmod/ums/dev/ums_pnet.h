/* SPDX-License-Identifier: GPL-2.0 */
/*
 * UB Memory based Socket(UMS)
 *
 * Description:UMS physical net(pnet) header file
 *
 * Copyright IBM Corp. 2016
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2025. All rights reserved.
 *
 * Original SMC-R implementation:
 *     Author(s): Thomas Richter <tmricht@linux.vnet.ibm.com>
 *
 * UMS implementation:
 *     Author(s): YAO Yufeng ZHANG Chuwen
 */

#ifndef UMS_PNET_H
#define UMS_PNET_H

#include <linux/string.h>
#include <linux/smc.h>

#if IS_ENABLED(CONFIG_HAVE_PNETID)
#include <asm/pnet.h>
#endif

#include <ub/urma/ubcore_types.h>

#include "ums_ubcore.h"

#define UB_DEVICE_NAME_MAX UBCORE_MAX_DEV_NAME

extern unsigned int g_ums_net_id;

/**
 * struct ums_pnettable - UMS PNET table anchor
 * @lock: Lock for list action
 * @pnetlist: List of PNETIDs
 */
struct ums_pnettable {
	struct mutex lock;
	struct list_head pnetlist;
};

struct ums_pnetids_ndev { /* list of pnetids for net devices in UP state */
	struct list_head list;
	rwlock_t lock;
};

struct ums_pnetids_ndev_entry {
	struct list_head list;
	u8 pnetid[UMS_MAX_PNETID_LEN];
	refcount_t refcnt;
};

/* per-network namespace private data */
struct ums_net {
	struct ums_pnettable pnettable;
	struct ums_pnetids_ndev pnetids_ndev;
};

static inline int ums_pnetid_by_dev_port(struct device *dev, unsigned short port, u8 *pnetid)
{
#if IS_ENABLED(CONFIG_HAVE_PNETID)
	return pnet_id_by_dev_port(dev, port, pnetid);
#else
	return -ENOENT;
#endif
}

static inline bool check_if_ub_bonding_dev(const struct ubcore_device *ub_dev)
{
    /* If the device name contains "bonding", it is a ub bonding device.
     * Currently, there are no other attributes to distinguish between ub bonding devices and ub devices.
     */
    return ((strnstr(ub_dev->dev_name, "bonding", UBCORE_MAX_DEV_NAME) != NULL) &&
        (ub_dev->transport_type == UBCORE_TRANSPORT_UB));
}

int __init ums_pnet_init(void);
int ums_pnet_net_init(struct net *net);
void ums_pnet_exit(void);
void ums_pnet_net_exit(struct net *net);
void ums_pnet_find_ub_resource(struct sock *sk, struct ums_init_info *ini);
int ums_pnetid_by_table_ub(struct ums_ubcore_device *ubdev, u8 ub_port);
bool ums_pnet_is_pnetid_set(const u8 *pnetid);
#endif /* UMS_PNET_H */
