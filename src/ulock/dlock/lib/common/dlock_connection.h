/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 * File Name     : dlock_connection.h
 * Description   : dlock connection header
 * History       : create file & add functions
 * 1.Date        : 2022-09-15
 * Author        : huying
 * Modification  : Created file
 */

#ifndef __DLOCK_CONNECTION_H__
#define __DLOCK_CONNECTION_H__

#include <cstdint>
#include <sys/types.h>
#include <dlock_common.h>

namespace dlock {
class dlock_connection {
public:
    dlock_connection();
    virtual ~dlock_connection();

    virtual ssize_t send(const void *buf, size_t len, int flags);
    virtual ssize_t recv(void *buf, size_t len, int flags);

    virtual void set_fd(int fd);
    virtual int get_fd() const;

    virtual bool is_ssl_enabled() const;

    void set_peer_info(dlock_conn_peer_t peer_type, int peer_id);
    void get_peer_info(dlock_conn_peer_info_t &peer_info) const;
    int rand_init_next_message_id(void);
    uint16_t generate_message_id(void);
    uint16_t get_next_message_id(void) const;
    void set_next_message_id(uint16_t next_message_id);

private:
    dlock_conn_peer_info_t m_peer_info;
    uint16_t m_next_message_id;
};
};
#endif /* __DLOCK_CONNECTION_H__ */
