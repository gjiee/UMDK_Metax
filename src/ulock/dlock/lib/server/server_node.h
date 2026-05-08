/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 * File Name     : server_node.h
 * Description   : header file for server node class
 * History       : create file & add functions
 * 1.Date        : 2021-07-06
 * Author        : zhangjun
 * Modification  : Created file
 */

#ifndef __SERVER_NODE_H__
#define __SERVER_NODE_H__

#include <atomic>

#include "jetty_mgr.h"
#include "dlock_connection.h"

namespace dlock {
class dlock_server;

class server_node {
    friend class dlock_server;
public:
    server_node() = delete;
    server_node(dlock_connection *p_conn, jetty_mgr *p_jetty_mgr, jetty_mgr *p_backup_jetty_mgr);
    ~server_node();

protected:
    int m_id;
    std::atomic<dlock_server_state_t> m_state;
    bool update_flag;    /* Indicates whether to update the m_p_jetty_mgr->m_ci. */

private:
    dlock_connection *m_p_conn;
    jetty_mgr *m_p_jetty_mgr;
    jetty_mgr *m_p_backup_jetty_mgr;
};
};
#endif