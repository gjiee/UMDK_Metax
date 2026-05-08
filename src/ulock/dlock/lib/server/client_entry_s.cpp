/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 * File Name     : client_entry_s.cpp
 * Description   : client entry in server lib
 * History       : create file & add functions
 * 1.Date        : 2021-06-21
 * Author        : zhangjun
 * Modification  : Created file
 */
#include "dlock_log.h"
#include "client_entry_s.h"

namespace dlock {
client_entry_s::client_entry_s(int client_id, dlock_connection *p_conn, jetty_mgr *p_jetty_mgr)
    : m_client_id(client_id), m_p_conn(p_conn), m_p_jetty_mgr(p_jetty_mgr), m_reinit_flag(false),
    m_client_lock_updated(false)
{
    DLOCK_LOG_DEBUG("client construct");
}

client_entry_s::~client_entry_s()
{
    if (m_p_jetty_mgr != nullptr) {
        m_p_jetty_mgr->jetty_mgr_deinit();
        delete m_p_jetty_mgr;
        m_p_jetty_mgr = nullptr;
    }

    // do not delete m_p_conn here
}

void client_entry_s::set_m_p_conn(dlock_connection *p_conn)
{
    m_p_conn = p_conn;
}
};
