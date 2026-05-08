/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 * File Name     : dlock_server_mgr.cpp
 * Description   : dlock server manager
 * History       : create file & add functions
 * 1.Date        : 2022-06-06
 * Author        : huying
 * Modification  : Created file
 */
#include <mutex>

#include "dlock_log.h"
#include "dlock_server.h"
#include "dlock_server_mgr.h"

namespace dlock {
dlock_server_mgr::dlock_server_mgr() noexcept : m_is_inited(false), m_max_server_num(0), m_next_server_id(1)
{
    DLOCK_LOG_DEBUG("dlock serverMgr construct");
}

dlock_server_mgr::~dlock_server_mgr()
{
    DLOCK_LOG_DEBUG("dlock serverMgr deconstruct");
}

dlock_server_mgr dlock_server_mgr::_instance;

dlock_server_mgr &dlock_server_mgr::instance()
{
    return _instance;
}

int dlock_server_mgr::init(unsigned int max_server_num)
{
    std::unique_lock<std::mutex> locker(m_mutex);

    if (m_is_inited) {
        DLOCK_LOG_ERR("serverMgr has been inited");
        return -1;
    }

    m_max_server_num = max_server_num;
    m_next_server_id = 1;
    m_is_inited = true;

    return 0;
}

void dlock_server_mgr::deinit()
{
    std::unique_lock<std::mutex> locker(m_mutex);

    if (!m_is_inited) {
        DLOCK_LOG_ERR("serverMgr has not been inited");
        return;
    }

    server_map_t::iterator iter = m_server_map.begin();
    while (iter != m_server_map.end()) {
        DLOCK_LOG_WARN("server stop, server_id: %d", iter->second->m_server_id);
        iter->second->quit();
        iter->second->deinit();
        delete iter->second;
        static_cast<void>(m_server_map.erase(iter++));
    }
    m_is_inited = false;
}

int dlock_server_mgr::server_start(const struct server_cfg &cfg, int &server_id)
{
    std::unique_lock<std::mutex> locker(m_mutex);

    if (!m_is_inited) {
        DLOCK_LOG_ERR("serverMgr has not been inited");
        return -1;
    }

    int ret;
    dlock_server *p_server = nullptr;

    server_id = get_next_server_id();
    if (server_id < 0) {
        DLOCK_LOG_ERR("get next server id failed, wrong server id");
        return static_cast<int>(DLOCK_SERVER_NO_RESOURCE);
    }
    p_server = new(std::nothrow) dlock_server(server_id);
    if (p_server == nullptr) {
        DLOCK_LOG_ERR("c++ new failed, bad alloc for dlock_server");
        return -1;
    }
    m_server_map[server_id] = p_server;

    DLOCK_LOG_DEBUG("server init, server_id: %d", server_id);
    ret = p_server->init(cfg);
    if (ret != 0) {
        DLOCK_LOG_ERR("server init failed, ret: %d", ret);
        static_cast<void>(m_server_map.erase(server_id));
        delete p_server;
        return ret;
    }

    DLOCK_LOG_DEBUG("server launch, server_id: %d", server_id);
    ret = p_server->launch();
    if (ret != 0) {
        DLOCK_LOG_ERR("server launch failed, ret: %d", ret);
        p_server->deinit();
        static_cast<void>(m_server_map.erase(server_id));
        delete p_server;
        return -1;
    }

    return 0;
}

int dlock_server_mgr::server_stop(int server_id)
{
    std::unique_lock<std::mutex> locker(m_mutex);

    if (!m_is_inited) {
        DLOCK_LOG_ERR("serverMgr has not been inited");
        return -1;
    }

    server_map_t::iterator iter = m_server_map.find(server_id);
    if (iter == m_server_map.end()) {
        DLOCK_LOG_ERR("server has not been inited, server_id: %d", server_id);
        return -1;
    }

    iter->second->quit();
    iter->second->deinit();
    delete iter->second;
    static_cast<void>(m_server_map.erase(iter));

    return 0;
}

int dlock_server_mgr::get_next_server_id()
{
    int ret;

    if (m_server_map.size() >= m_max_server_num) {
        DLOCK_LOG_WARN("reach max server limits, max_server_num: %u", m_max_server_num);
        return -1;
    }

    server_map_t::iterator iter = m_server_map.find(m_next_server_id);
    int icount = 0;
    while ((iter != m_server_map.end() && (icount < MAX_SERVER_ID))) {
        m_next_server_id = (m_next_server_id  % MAX_SERVER_ID) + 1;
        iter = m_server_map.find(m_next_server_id);
        icount++;
    }
    ret = (iter != m_server_map.end()) ? -1 : m_next_server_id;
    if (iter == m_server_map.end()) {
        m_next_server_id = (m_next_server_id  % MAX_SERVER_ID) + 1;
    }
    return ret;
}

int dlock_server_mgr::get_server_debug_stats(int server_id, struct debug_stats *stats)
{
    std::unique_lock<std::mutex> locker(m_mutex);

    if (!m_is_inited) {
        DLOCK_LOG_ERR("serverMgr has not been inited");
        return -1;
    }

    server_map_t::iterator iter = m_server_map.find(server_id);
    if (iter == m_server_map.end()) {
        DLOCK_LOG_ERR("server has not been inited, server_id: %d", server_id);
        return -1;
    }

    static_cast<void>(memcpy(stats, &iter->second->m_stats, sizeof(iter->second->m_stats)));
    return 0;
}

int dlock_server_mgr::clear_server_debug_stats(int server_id)
{
    std::unique_lock<std::mutex> locker(m_mutex);

    if (!m_is_inited) {
        DLOCK_LOG_ERR("serverMgr has not been inited");
        return -1;
    }

    server_map_t::iterator iter = m_server_map.find(server_id);
    if (iter == m_server_map.end()) {
        DLOCK_LOG_ERR("server has not been inited, server_id: %d", server_id);
        return -1;
    }

    static_cast<void>(memset(&iter->second->m_stats, 0, sizeof(iter->second->m_stats)));
    return 0;
}

};
