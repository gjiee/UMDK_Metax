/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 * File Name     : tcp_connection.cpp
 * Description   : TCP connection module
 * History       : create file & add functions
 * 1.Date        : 2022-09-15
 * Author        : huying
 * Modification  : Created file
 */

#include <sys/socket.h>
#include <unistd.h>

#include "dlock_log.h"
#include "tcp_connection.h"

namespace dlock {
tcp_connection::tcp_connection(int sockfd) : dlock_connection(), m_sockfd(sockfd)
{
    DLOCK_LOG_DEBUG("tcp_connection construct");
}

tcp_connection::~tcp_connection()
{
    DLOCK_LOG_DEBUG("tcp_connection deconstruct");
    if (m_sockfd > 0) {
        static_cast<void>(close(m_sockfd));
        m_sockfd = -1;
    }
}

ssize_t tcp_connection::send(const void *buf, size_t len, int flags)
{
    ssize_t ret = ::send(m_sockfd, buf, len, flags);
    return ret;
}

ssize_t tcp_connection::recv(void *buf, size_t len, int flags)
{
    ssize_t ret = ::recv(m_sockfd, buf, len, flags);
    return ret;
}

void tcp_connection::set_fd(int fd)
{
    m_sockfd = fd;
}

int tcp_connection::get_fd() const
{
    return m_sockfd;
}
};
