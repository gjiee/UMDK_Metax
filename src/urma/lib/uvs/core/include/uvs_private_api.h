/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2025. All rights reserved.
 * Description: uvs private api
 * Author: Li Hai
 * Create: 2024-2-18
 * Note:
 * History:
 */

#ifndef UVS_PRIVATE_API_H
#define UVS_PRIVATE_API_H

#include <string.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

void uvs_get_api_rdlock(void);
void uvs_get_api_wrlock(void);
void put_uvs_lock(void);

#ifdef __cplusplus
}
#endif

#endif