/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 * File Name     : lock_entry_s.cpp
 * Description   : lock entry in server lib
 * History       : create file & add functions
 * 1.Date        : 2021-06-22
 * Author        : zhangjun
 * Modification  : Created file
 */
#include <climits>

#include "dlock_log.h"
#include "lock_memory.h"
#include "lock_entry_s.h"

namespace dlock {
lock_entry_s::lock_entry_s(int32_t lock_id, enum dlock_type lock_type, lock_memory *p_lock_memory)
    : m_lock_dec(nullptr), m_lock_id(lock_id), m_lock_type(lock_type), m_p_lock_memory(nullptr), m_lock_offset(UINT_MAX)
{
    if (p_lock_memory == nullptr) {
        DLOCK_LOG_ERR("p_lock_memory is nullptr");
        return;
    }

    m_p_lock_memory = p_lock_memory;
    m_lock_offset = p_lock_memory->get_lock_memory(lock_type);
    DLOCK_LOG_DEBUG("new lock %d at %u", lock_id, m_lock_offset);
}

lock_entry_s::lock_entry_s(int32_t lock_id, enum dlock_type lock_type,
    uint32_t lock_offset, lock_memory *p_lock_memory)
    : m_lock_dec(nullptr), m_lock_id(lock_id), m_lock_type(lock_type), m_p_lock_memory(nullptr), m_lock_offset(UINT_MAX)
{
    if (p_lock_memory == nullptr) {
        DLOCK_LOG_ERR("p_lock_memory is nullptr");
        return;
    }

    m_p_lock_memory = p_lock_memory;
    m_lock_offset = p_lock_memory->get_lock_memory(lock_type, lock_offset);
    DLOCK_LOG_DEBUG("new lock %d at %u", lock_id, m_lock_offset);
}

lock_entry_s::~lock_entry_s()
{
    if (m_lock_offset != UINT_MAX) {
        m_p_lock_memory->release_lock_memory(m_lock_offset, m_lock_type);
    }

    if (m_lock_dec != nullptr) {
        delete m_lock_dec;
        m_lock_dec = nullptr;
    }
    m_p_lock_memory = nullptr;
    DLOCK_LOG_DEBUG("delete lock %d", m_lock_id);
}
};
