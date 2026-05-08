/* SPDX-License-Identifier: GPL-2.0 */
/*
 * UB Memory based Socket(UMS)
 *
 * Description:UMS adapts kernel version implementation
 *
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2025. All rights reserved.
 *
 * UMS implementation:
 *     Author(s): Yan Ming
 */

#ifndef UMS_ADAPT_H
#define UMS_ADAPT_H

#ifdef KERNEL_VERSION_4
#include <linux/types.h>
#include <linux/uaccess.h>

#define SOCK_TSTAMP_NEW 26

#define SK_USER_DATA_NOCOPY 1UL

#define in_dev_for_each_ifa_rcu(ifa, in_dev)			\
	for (ifa = rcu_dereference((in_dev)->ifa_list); ifa;	\
		ifa = rcu_dereference(ifa->ifa_next))

#if __has_attribute(__fallthrough__)
# define fallthrough                    __attribute__((__fallthrough__))
#else
# define fallthrough                    do {} while (0)  /* fallthrough */
#endif

struct netdev_nested_priv {
	unsigned char flags;
	void *data;
};

static inline char __user *KERNEL_SOCKPTR(void *p)
{
	return (char __user *)p;
}

static inline int copy_from_sockptr(void *dst, char __user *src, size_t size)
{
	return copy_from_user(dst, src, size);
}
#endif /* KERNEL_VERSION_4 */
#endif /* UMS_ADAPT_H */
