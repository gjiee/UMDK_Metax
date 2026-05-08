/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 * File Name     : object_entry_s.cpp
 * Description   : object entry in server lib
 * History       : create file & add functions
 * 1.Date        : 2024-08-06
 * Author        : yexin
 * Modification  : Created file
 */

#include "object_entry_s.h"

namespace dlock {
object_entry_s::object_entry_s(int32_t id, int32_t owner_id)
    : m_object_desc(nullptr), m_id(id), m_owner_id(owner_id),  m_offset(0), m_refcnt(0), m_destroyed(false)
{
}

object_entry_s::object_entry_s(int32_t id, int32_t owner_id, uint64_t offset)
    : m_object_desc(nullptr), m_id(id), m_owner_id(owner_id), m_offset(offset), m_refcnt(0), m_destroyed(false)
{
}

object_entry_s::~object_entry_s()
{
}

bool object_entry_s::check_lease_expired()
{
    return std::chrono::steady_clock::now() >= m_max_lease_tp;
}
};
