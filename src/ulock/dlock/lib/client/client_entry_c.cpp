/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 * File Name     : client_entry_c.cpp
 * Description   : client entry class in the client lib
 * History       : create file & add functions
 * 1.Date        : 2021-06-17
 * Author        : zhangjun
 * Modification  : Created file
 */

#include "client_entry_c.h"

#include "dlock_log.h"
#include "dlock_common.h"
#include "utils.h"

namespace dlock {
client_entry_c::client_entry_c(int client_id, dlock_connection *p_conn, jetty_mgr *p_jetty_mgr)
    : m_client_id(client_id), m_p_conn(p_conn), m_update_lock_state(0), m_update_lock_num(0),
    m_async_flag(false), m_async_op(-1), m_async_id(0), m_p_jetty_mgr(p_jetty_mgr), m_obj_invalid_flag(false)
{
    static_cast<void>(memset(&m_stats, 0, sizeof(struct debug_stats)));
}

client_entry_c::~client_entry_c()
{
    clear_m_lock_map();
    clear_m_update_map();
    clear_m_object_map();
    clear_m_obj_desc_map();

    if (m_p_conn != nullptr) {
        delete m_p_conn;
        m_p_conn = nullptr;
    }

    if (m_p_jetty_mgr != nullptr) {
        m_p_jetty_mgr->jetty_mgr_deinit();
        delete m_p_jetty_mgr;
        m_p_jetty_mgr = nullptr;
    }
}

void client_entry_c::clear_m_lock_map(void) noexcept
{
    if (!m_lock_map.empty()) {
        auto iter = m_lock_map.begin();
        while (iter != m_lock_map.end()) {
            delete iter->second;
            static_cast<void>(iter++);
        }
        m_lock_map.clear();
    }
}

void client_entry_c::clear_m_update_map(void) noexcept
{
    if (!m_update_map.empty()) {
        auto iter = m_update_map.begin();
        while (iter != m_update_map.end()) {
            delete iter->second;
            static_cast<void>(iter++);
        }
        m_update_map.clear();
    }
}

// clear object_entry_c
void client_entry_c::clear_m_object_map(void) noexcept
{
    if (!m_object_map.empty()) {
        auto iter = m_object_map.begin();
        while (iter != m_object_map.end()) {
            delete iter->second->m_object_desc;
            delete iter->second;
            static_cast<void>(iter++);
        }
        m_object_map.clear();
    }
}

// clear dlock_descriptor
void client_entry_c::clear_m_obj_desc_map(void) noexcept
{
    if (!m_obj_desc_map.empty()) {
        auto iter = m_obj_desc_map.begin();
        while (iter != m_obj_desc_map.end()) {
            delete iter->first;
            static_cast<void>(iter++);
        }
        m_obj_desc_map.clear();
    }
}

void client_entry_c::update_associated_client_pointer(void)
{
    if (m_lock_map.empty()) {
        return;
    }

    auto iter = m_lock_map.begin();
    while (iter != m_lock_map.end()) {
        iter->second->set_m_client(this);
        static_cast<void>(iter++);
    }
}

void client_entry_c::update_locks_requester(void)
{
    if (m_update_map.empty()) {
        m_update_map.swap(m_lock_map);
    }

    uint8_t *buf = construct_control_msg(BATCH_UPDATE_LOCKS_REQUEST, DLOCK_PROTO_VERSION,
        DLOCK_FIXED_CTRL_MSG_HDR_LEN, DLOCK_MAX_CTRL_MSG_SIZE, 0, m_client_id);
    if (buf == nullptr) {
        DLOCK_LOG_ERR("malloc error (errno=%d %m)", errno);
        m_update_lock_state = 0;
        return;
    }
    struct dlock_control_hdr *msg_hdr = reinterpret_cast<struct dlock_control_hdr*>(reinterpret_cast<void*>(buf));
    struct batch_update_lock_body *msg_batch_update_lock_body =
        reinterpret_cast<struct batch_update_lock_body *>(buf + DLOCK_FIXED_CTRL_MSG_HDR_LEN);
    msg_batch_update_lock_body->lock_num = 0;
    size_t initial_offset = DLOCK_FIXED_CTRL_MSG_HDR_LEN + DLOCK_BATCH_UPDATE_LOCK_BODY_LEN;
    size_t offset = initial_offset;
    lock_map_t::iterator lock_iter = m_update_map.begin();
    struct update_lock_body *p_msg_update = nullptr;
    lock_entry_c *p_lock_entry = nullptr;
    ssize_t ret = 0;

    while ((m_update_lock_state == 1) && (lock_iter != m_update_map.end())) {
        p_lock_entry = lock_iter->second;
        p_msg_update = reinterpret_cast<struct update_lock_body*>(buf + offset);
        p_lock_entry->fill_update_msg(p_msg_update);
        msg_batch_update_lock_body->lock_num++;
        offset += DLOCK_UPDATE_LOCK_BODY_LEN + p_msg_update->desc_len;
        if (msg_batch_update_lock_body->lock_num == MAX_LOCK_BATCH_SIZE) {
            msg_hdr->total_len = static_cast<uint16_t>(offset);
            msg_hdr->message_id = m_p_conn->generate_message_id();
            ret = m_p_conn->send(buf, offset, 0);
            if (ret < 0) {
                DLOCK_LOG_ERR("send error (errno=%d %m)", errno);
                break;
            }
            msg_batch_update_lock_body->lock_num = 0;
            offset = initial_offset;
            m_update_lock_num++;
        }
        ++lock_iter;
    }
    if ((msg_batch_update_lock_body->lock_num != 0) && (ret >= 0)) {
        msg_hdr->total_len = static_cast<uint16_t>(offset);
        msg_hdr->message_id = m_p_conn->generate_message_id();
        ret = m_p_conn->send(buf, offset, 0);
        if (ret < 0) {
            DLOCK_LOG_ERR("send error (errno=%d %m)", errno);
        } else {
            m_update_lock_num++;
        }
    }

    m_update_lock_state = 0;
    free(buf);
}
};
