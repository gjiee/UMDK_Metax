/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 * File Name     : dlock_server_mgr.h
 * Description   : dlock server manager
 * History       : create file & add functions
 * 1.Date        : 2022-06-06
 * Author        : huying
 * Modification  : Created file
 */

#ifndef __DLOCK_SERVER_MGR_H__
#define __DLOCK_SERVER_MGR_H__

#include <unordered_map>
#include <mutex>

#include "dlock_server.h"
#include "utils.h"

namespace dlock {
using server_map_t = std::unordered_map<int32_t, dlock_server*>;

class dlock_server_mgr {
public:
    static dlock_server_mgr &instance();
    int init(unsigned int max_server_num);
    void deinit();
    int server_start(const struct server_cfg &cfg, int &server_id);
    int server_stop(int server_id);
    int get_server_debug_stats(int server_id, struct debug_stats *stats);
    int clear_server_debug_stats(int server_id);

protected:
    dlock_server_mgr() noexcept;
    ~dlock_server_mgr();
private:
    int get_next_server_id();

    static dlock_server_mgr _instance;
    bool m_is_inited;
    unsigned int m_max_server_num;
    server_map_t m_server_map;
    int m_next_server_id;
    std::mutex m_mutex;
};
};
#endif /* __DLOCK_SERVER_MGR_H__ */
