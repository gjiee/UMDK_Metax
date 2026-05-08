/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 * File Name     : tcp_connection.h
 * Description   : TCP connection header
 * History       : create file & add functions
 * 1.Date        : 2022-09-15
 * Author        : huying
 * Modification  : Created file
 */

#ifndef __TCP_CONNECTION_H__
#define __TCP_CONNECTION_H__

#include "dlock_connection.h"

namespace dlock {
class tcp_connection : public dlock_connection {
public:
    tcp_connection() = delete;
    explicit tcp_connection(int sockfd);
    ~tcp_connection() override;

    ssize_t send(const void *buf, size_t len, int flags) override;
    ssize_t recv(void *buf, size_t len, int flags) override;

    void set_fd(int fd) override;
    int get_fd() const override;
private:
    int m_sockfd;
};
};
#endif /* __TCP_CONNECTION_H__ */
