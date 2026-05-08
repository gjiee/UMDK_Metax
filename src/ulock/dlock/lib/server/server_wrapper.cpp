/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 * File Name     : server_wrapper.cpp
 * Description   : wrapper of server
 * History       : create file & add functions
 * 1.Date        : 2021-06-15
 * Author        : zhangjun
 * Modification  : Created file
 */

#include "dlock_server_api.h"
#include "dlock_types.h"
#include "dlock_common.h"
#include "dlock_server_mgr.h"
#include "dlock_log.h"
#include "utils.h"

namespace dlock {
extern "C" {
int dserver_lib_init(unsigned int max_server_num)
{
    dlock_server_mgr& serverMgr = dlock_server_mgr::instance();

    if ((max_server_num == 0u) || (max_server_num > MAX_NUM_SERVER)) {
        DLOCK_LOG_ERR("invalid max_server_num: %d", max_server_num);
        return -1;
    }

    DLOCK_LOG_DEBUG("init server lib, max_server_num:%d", max_server_num);
    return serverMgr.init(max_server_num);
}

void dserver_lib_deinit()
{
    dlock_server_mgr& serverMgr = dlock_server_mgr::instance();

    DLOCK_LOG_DEBUG("deinit server lib");
    serverMgr.deinit();
}

int server_start(const struct server_cfg &cfg, int &server_id)
{
    dlock_server_mgr& serverMgr = dlock_server_mgr::instance();

    if (cfg.type != SERVER_PRIMARY) {
        DLOCK_LOG_ERR("invalid server type");
        return -1;
    }

    if (!check_primary_cfg_valid(cfg.primary)) {
        DLOCK_LOG_ERR("invalid primary cfg");
        return -1;
    }

    if (!check_ssl_cfg_valid(cfg.ssl)) {
        DLOCK_LOG_ERR("invalid SSL cfg");
        return -1;
    }

    if ((cfg.tp_mode != SEPERATE_CONN) && (cfg.tp_mode != UNI_CONN)) {
        DLOCK_LOG_ERR("invalid transport mode set");
        return -1;
    }

    DLOCK_LOG_DEBUG("server start, server_type: %d", static_cast<int>(cfg.type));
    return serverMgr.server_start(cfg, server_id);
}

int server_stop(int server_id)
{
    dlock_server_mgr& serverMgr = dlock_server_mgr::instance();

    if (check_server_id_invalid(server_id)) {
        DLOCK_LOG_ERR("invalid server_id: %d", server_id);
        return -1;
    }

    DLOCK_LOG_DEBUG("server stop, server_id: %d", server_id);
    return serverMgr.server_stop(server_id);
}

int get_server_debug_stats(int server_id, struct debug_stats *stats)
{
    if (check_server_id_invalid(server_id)) {
        DLOCK_LOG_ERR("invalid server_id: %d", server_id);
        return -1;
    }

    if (stats == nullptr) {
        DLOCK_LOG_ERR("debug_stats is nullptr");
        return -1;
    }

    dlock_server_mgr& serverMgr = dlock_server_mgr::instance();
    return serverMgr.get_server_debug_stats(server_id, stats);
}

int clear_server_debug_stats(int server_id)
{
    if (check_server_id_invalid(server_id)) {
        DLOCK_LOG_ERR("invalid server_id: %d", server_id);
        return -1;
    }

    dlock_server_mgr& serverMgr = dlock_server_mgr::instance();
    return serverMgr.clear_server_debug_stats(server_id);
}
}
};
