/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 * File Name     : dlock_connection.cpp
 * Description   : dlock connection module
 * History       : create file & add functions
 * 1.Date        : 2022-09-15
 * Author        : huying
 * Modification  : Created file
 */
#include <openssl/rand.h>

#include "dlock_log.h"
#include "dlock_connection.h"

namespace dlock {
dlock_connection::dlock_connection() : m_next_message_id(0)
{
    DLOCK_LOG_DEBUG("dlock_connection construct");
    m_peer_info.peer_type = DLOCK_CONN_PEER_DEFAULT;
    m_peer_info.peer_id = 0;
}

dlock_connection::~dlock_connection()
{
    DLOCK_LOG_DEBUG("dlock_connection deconstruct");
}

ssize_t dlock_connection::send(const void *buf, size_t len, int flags)
{
    static_cast<void>(buf);
    static_cast<void>(len);
    static_cast<void>(flags);
    return 0;
}

ssize_t dlock_connection::recv(void *buf, size_t len, int flags)
{
    static_cast<void>(buf);
    static_cast<void>(len);
    static_cast<void>(flags);
    return 0;
}

bool dlock_connection::is_ssl_enabled() const
{
    return false;
}

void dlock_connection::set_fd(int fd)
{
    static_cast<void>(fd);
    return;
}

int dlock_connection::get_fd() const
{
    return -1;
}

void dlock_connection::set_peer_info(dlock_conn_peer_t peer_type, int peer_id)
{
    m_peer_info.peer_type = peer_type;
    m_peer_info.peer_id = peer_id;
}

void dlock_connection::get_peer_info(dlock_conn_peer_info_t &peer_info) const
{
    peer_info.peer_type = m_peer_info.peer_type;
    peer_info.peer_id = m_peer_info.peer_id;
}

int dlock_connection::rand_init_next_message_id(void)
{
    int ret = RAND_priv_bytes(reinterpret_cast<unsigned char *>(&m_next_message_id), sizeof(m_next_message_id));
    if (ret != 1) {
        DLOCK_LOG_ERR("failed to generate random next message id, ret: %d", ret);
        return -1;
    }
    return 0;
}

uint16_t dlock_connection::generate_message_id(void)
{
    return (m_next_message_id++);
}

uint16_t dlock_connection::get_next_message_id(void) const
{
    return m_next_message_id;
}

void dlock_connection::set_next_message_id(uint16_t next_message_id)
{
    m_next_message_id = next_message_id;
}
};
