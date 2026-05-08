/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 * File Name     : server_node.cpp
 * Description   : server node class
 * History       : create file & add functions
 * 1.Date        : 2021-06-21
 * Author        : zhangjun
 * Modification  : Created file
 */

#include "server_node.h"

#include "dlock_log.h"

namespace dlock {
server_node::server_node(dlock_connection *p_conn, jetty_mgr *p_jetty_mgr, jetty_mgr *p_backup_jetty_mgr)
    : m_id(0), m_state(SERVER_INIT), update_flag(true), m_p_conn(p_conn), m_p_jetty_mgr(p_jetty_mgr),
      m_p_backup_jetty_mgr(p_backup_jetty_mgr)
{
    DLOCK_LOG_DEBUG("server construct");
}

server_node::~server_node()
{
    if (m_p_jetty_mgr != nullptr) {
        m_p_jetty_mgr->jetty_mgr_deinit();
        delete m_p_jetty_mgr;
        m_p_jetty_mgr = nullptr;
    }

    if (m_p_backup_jetty_mgr != nullptr) {
        m_p_backup_jetty_mgr->jetty_mgr_deinit();
        delete m_p_backup_jetty_mgr;
        m_p_backup_jetty_mgr = nullptr;
    }

    // do not delete m_p_conn here
}
};
