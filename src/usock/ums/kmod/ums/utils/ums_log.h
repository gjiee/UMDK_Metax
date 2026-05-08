/* SPDX-License-Identifier: GPL-2.0 */
/*
 * UB Memory based Socket(UMS)
 *
 * Description:UMS common log definition header file
 *
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2025. All rights reserved.
 *
 * UMS implementation:
 *     Author(s): YAN Ming
 */

#ifndef UMS_LOG_H
#define UMS_LOG_H

#include <linux/printk.h>

#define UMS_LOGD(format, ...) \
	pr_debug("[UMS_DEBUG (%s:%u)] " format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define UMS_LOGI(format, ...) \
	(void)pr_info("[UMS_INFO (%s:%u)] " format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define UMS_LOGW(format, ...) \
	(void)pr_warn("[UMS_WARN (%s:%u)] " format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define UMS_LOGE(format, ...) \
	(void)pr_err("[UMS_ERR (%s:%u)] " format "\n", __func__, __LINE__, ##__VA_ARGS__)

#define UMS_LOGI_LIMITED(format, ...) \
	(void)pr_info_ratelimited("[UMS_INFO (%s:%u)] " format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define UMS_LOGW_LIMITED(format, ...) \
	(void)pr_warn_ratelimited("[UMS_WARN (%s:%u)] " format "\n", __func__, __LINE__, ##__VA_ARGS__)
#define UMS_LOGE_LIMITED(format, ...) \
	(void)pr_err_ratelimited("[UMS_ERR (%s:%u)] " format "\n", __func__, __LINE__, ##__VA_ARGS__)

#define UMS_LOGW_ONCE(format, ...) \
	(void)pr_warn_once("[UMS_WARN (%s:%u)] " format "\n", __func__, __LINE__, ##__VA_ARGS__)
#endif /* UMS_LOG_H */
