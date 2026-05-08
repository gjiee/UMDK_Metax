/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 * File Name     : dlock_server_api.h
 * Description   : external APIs of DLock server
 * History       : create file & add functions
 * 1.Date        : 2022-03-21
 * Author        : geyi
 * Modification  : Created file
 */

#ifndef __DLOCK_SERVER_API_H__
#define __DLOCK_SERVER_API_H__

#include "dlock_types.h"

namespace dlock {
extern "C" {
/**
 * Initialize the DLock server library context
 * @param[in] max_server_num: The maximum number of servers, which must be greater than 0 and
 *                            cannot exceed MAX_NUM_SERVER (value is 32, and can be equal to this value)
 * Return: 0 on success, -1 on failure
 */
int dserver_lib_init(unsigned int max_server_num);

/**
 * Deinitialize the DLock server library context
 */
void dserver_lib_deinit();

/**
 * Start the primary server and initialize server-side resources
 * @param[in] cfg: server configuration parameters
 * @param[out] server_id: Used to store the assigned server_id. The current value range for server_id is 1 to 0xFFFFF.
 * Return: 0 on success, -1 on failure, the specific status codes as follows:
 * DLOCK_SERVER_NO_RESOURCE: Failed to allocate lock resources on the server side, failure in other malloc memory
 *   allocations, or the number of servers exceeds the maximum allowable server count specified by max_server_num.
 */
int server_start(const struct server_cfg &cfg, int &server_id);

/**
 * Destroy the server, terminate the thread corresponding to the server, and release the resources it occupies
 * @param[in] server_id：server ID pending destruction
 * Return: 0 on success, -1 on failure
 */
int server_stop(int server_id);

/**
 * Obtain the data plane exception statistics of the server instance
 * @param[in] server_id：server ID
 * @param[out] stats: Returns the data plane exception statistics of the server instance.
 * Return: 0 on success, -1 on failure
 */
int get_server_debug_stats(int server_id, struct debug_stats *stats);

/**
 * Clear the data plane exception statistics of the server instance
 * @param[in] server_id：server ID
 * Return: 0 on success, -1 on failure
 */
int clear_server_debug_stats(int server_id);
}
};
#endif  // __DLOCK_SERVER_API_H__
