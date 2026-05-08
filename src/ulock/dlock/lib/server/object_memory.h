/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 * File Name     : object_memory.h
 * Description   : header file for manager of object memory in the server lib
 * History       : create file
 * 1.Date        : 2024-08-06
 * Author        : yexin
 * Modification  : Created file
 */

#ifndef __OBJECT_MEMORY_H__
#define __OBJECT_MEMORY_H__

#include "dlock_common.h"

namespace dlock {
constexpr uint64_t INVALID_OFFSET = UINT32_MAX * sizeof(uint64_t);

class dlock_server;
class client_entry_s;

class object_memory {
    friend class dlock_server;
    friend class client_entry_s;
public:
    object_memory() = delete;
    object_memory(uint32_t total, bool is_primary, dlock_server *server);
    ~object_memory();

    bool init();
    uint64_t alloc_object_memory(void);
    void free_object_memory(uint64_t offset);

    void set(uint64_t offset, uint64_t val);

protected:
private:
    uint64_t *m_addr;
    uint32_t *m_ctrl;
    uint32_t m_next_free;
    uint32_t m_total;
    bool m_is_primary;
    dlock_server *m_server;
};
};
#endif
