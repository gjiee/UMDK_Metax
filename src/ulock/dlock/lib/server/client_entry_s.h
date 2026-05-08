/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 * File Name     : client_entry_s.h
 * Description   : struct of each client in server lib
 * History       : create file & add functions
 * 1.Date        : 2021-06-21
 * Author        : zhangjun
 * Modification  : Created file
 */

#ifndef __CLIENT_ENTRY_S_H__
#define __CLIENT_ENTRY_S_H__

#include <unordered_map>

#include "lock_entry_s.h"
#include "object_entry_s.h"
#include "jetty_mgr.h"
#include "dlock_connection.h"

namespace dlock {
class dlock_server;

using lock_map_t = std::unordered_map<int32_t, lock_entry_s*>;
using object_map_t = std::unordered_map<int32_t, object_entry_s*>;

class client_entry_s {
    friend class dlock_server;
public:
    client_entry_s() = delete;
    client_entry_s(int client_id, dlock_connection *p_conn, jetty_mgr *p_jetty_mgr);
    ~client_entry_s();

    void set_m_p_conn(dlock_connection *p_conn);
private:
    int m_client_id;
    dlock_connection *m_p_conn;
    jetty_mgr *m_p_jetty_mgr;
    lock_map_t m_lock_map;
    object_map_t m_object_map;
    bool m_reinit_flag;
    bool m_client_lock_updated;
};
};
#endif
