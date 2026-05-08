/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 * File Name     : object_memory.cpp
 * Description   : manager of object memory in the server lib
 * History       : create file
 * 1.Date        : 2024-08-06
 * Author        : yexin
 * Modification  : Created file
 */

#include <malloc.h>

#include "dlock_server.h"
#include "dlock_log.h"
#include "object_memory.h"

namespace dlock {

object_memory::object_memory(uint32_t total, bool is_primary, dlock_server *server)
    : m_addr(nullptr), m_ctrl(nullptr), m_next_free(0), m_total(total), m_is_primary(is_primary), m_server(server)
{
}

object_memory::~object_memory()
{
    if (m_addr != nullptr) {
        free(m_addr);
        m_addr = nullptr;
    }
    if (m_ctrl != nullptr) {
        delete[] m_ctrl;
        m_ctrl = nullptr;
    }
}

bool object_memory::init()
{
    uint32_t obj_mem_size = m_total * sizeof(uint64_t);
    m_addr = (uint64_t *)memalign(DLOCK_UB_SEG_VA_ALIGN_SIZE, obj_mem_size);
    if (m_addr == nullptr) {
        return false;
    }
    static_cast<void>(memset(m_addr, 0, obj_mem_size));

    m_ctrl = new(std::nothrow) uint32_t[m_total];
    if (m_ctrl == nullptr) {
        free(m_addr);
        m_addr = nullptr;
        return false;
    }

    uint32_t i;
    for (i = 0; i < m_total - 1; i++) {
        m_ctrl[i] = i + 1;
    }
    m_ctrl[i] = UINT32_MAX;

    m_next_free = 0;

    DLOCK_LOG_DEBUG("object_memory init");
    return true;
}

uint64_t object_memory::alloc_object_memory(void)
{
    if (m_next_free == UINT32_MAX) {
        return INVALID_OFFSET;
    }

    uint32_t cur = m_next_free;
    uint32_t next = m_ctrl[cur];
    m_next_free = next;

    return cur * sizeof(uint64_t);
}

void object_memory::free_object_memory(uint64_t offset)
{
    if (offset >= INVALID_OFFSET) {
        DLOCK_LOG_DEBUG("invalid offset %lu", offset);
        return;
    }

    uint32_t id = offset / sizeof(uint64_t);
    m_ctrl[id] = m_next_free;
    m_next_free = id;
}

void object_memory::set(uint64_t offset, uint64_t val)
{
    if (offset >= INVALID_OFFSET) {
        DLOCK_LOG_DEBUG("invalid offset %lu", offset);
        return;
    }

    uint32_t id = offset / sizeof(uint64_t);
    m_addr[id] = val;
}
};
