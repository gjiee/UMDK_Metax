/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 * File Name     : object_entry_s.h
 * Description   : struct of each object in server
 * History       : create file & add functions
 * 1.Date        : 2024-08-06
 * Author        : yexin
 * Modification  : Created file
 */

#ifndef __OBJECT_ENTRY_S_H__
#define __OBJECT_ENTRY_S_H__

#include <map>
#include <chrono>

#include "dlock_types.h"
#include "dlock_common.h"
#include "dlock_descriptor.h"

namespace dlock {
class dlock_server;

using std_tp = std::chrono::steady_clock::time_point;
using lease_tp_map_t = std::map<int32_t, std_tp>;

class object_entry_s {
    friend class dlock_server;
public:
    object_entry_s() = delete;
    object_entry_s(int32_t id, int32_t owner_id);
    object_entry_s(int32_t id, int32_t owner_id, uint64_t offset);
    ~object_entry_s();

    dlock_descriptor* m_object_desc;

    bool check_lease_expired();
protected:

private:
    int32_t m_id;
    int32_t m_owner_id;
    uint64_t m_offset;
    int32_t m_refcnt;
    bool m_destroyed;
    std_tp m_max_lease_tp;
    lease_tp_map_t m_lease_tp_map;
};
};
#endif
