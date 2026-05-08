/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 * File Name     : lock_entry_s.h
 * Description   : struct of each lock in server
 * History       : create file & add functions
 * 1.Date        : 2021-06-21
 * Author        : zhangjun
 * Modification  : Created file
 */

#ifndef __LOCK_ENTRY_S_H__
#define __LOCK_ENTRY_S_H__

#include "dlock_common.h"
#include "lock_memory.h"
#include "dlock_descriptor.h"

#include <map>

namespace dlock {
class dlock_server;

using lease_time_map_t = std::map<int32_t, uint32_t>;

class lock_entry_s {
    friend class dlock_server;
public:
    lock_entry_s() = delete;
    lock_entry_s(int32_t lock_id, enum dlock_type lock_type, lock_memory *p_lock_memory);
    lock_entry_s(int32_t lock_id, enum dlock_type lock_type, uint32_t lock_offset, lock_memory *p_lock_memory);
    ~lock_entry_s();

    dlock_descriptor* m_lock_dec;
private:
    int32_t m_lock_id;
    enum dlock_type m_lock_type;
    lock_memory *m_p_lock_memory;
    uint32_t m_lock_offset;
    lease_time_map_t m_lease_time_map;
};
};
#endif
