/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 * File Name     : object_entry_c.h
 * Description   : header file for object entry class in the client lib
 * History       : create file & add functions
 * 1.Date        : 2024-08-06
 * Author        : yexin
 * Modification  : Created file
 */

#ifndef __OBJECT_ENTRY_C_H__
#define __OBJECT_ENTRY_C_H__

#include "dlock_types.h"
#include "dlock_common.h"
#include "dlock_descriptor.h"

namespace dlock {
class dlock_client;
class client_entry_c;

class object_entry_c {
    friend class dlock_client;
public:
    object_entry_c() = delete;
    object_entry_c(int32_t id, uint64_t offset);
    ~object_entry_c();

    dlock_descriptor *m_object_desc;
protected:

private:
    int32_t m_id;
    uint64_t m_offset;
};
};
#endif
