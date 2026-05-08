/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 * File Name     : dlock_descriptor.h
 * Description   : dlock descriptor class header
 * History       : create file & add functions
 * 1.Date        : 2022-03-26
 * Author        : geyi
 * Modification  : Created file
 */

#ifndef __DLOCK_DESCRIPTOR_H__
#define __DLOCK_DESCRIPTOR_H__

#include <cstddef>

namespace dlock {
class dlock_client;
class dlock_server;
class client_entry_s;

class dlock_descriptor {
    friend class dlock_client;
    friend class dlock_server;
    friend class client_entry_s;

public:
    unsigned int m_len;
    unsigned char *m_desc;

    dlock_descriptor();
    ~dlock_descriptor();

    dlock_status_t descriptor_init(unsigned int len, unsigned char *buf);
    bool is_desc_equal(const dlock_descriptor *p_desc1, const dlock_descriptor *p_desc2) const;
    unsigned int get_desc_hash(const dlock_descriptor *p_next_desc) const;
};

struct hash_dlock_desc {
    size_t operator()(const dlock_descriptor *p_desc) const
    {
        return p_desc->get_desc_hash(p_desc);
    }
};

struct equal_dlock_desc {
    bool operator()(const dlock_descriptor *p_desc1, const dlock_descriptor *p_desc2) const
    {
        return p_desc1->is_desc_equal(p_desc1, p_desc2);
    }
};
};
#endif  // __DLOCK_DESCRIPTOR_H__