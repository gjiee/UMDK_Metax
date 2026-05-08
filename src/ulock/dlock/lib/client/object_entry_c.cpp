/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 * File Name     : object_entry_c.cpp
 * Description   : object entry class in the client lib
 * History       : create file & add functions
 * 1.Date        : 2024-08-06
 * Author        : yexin
 * Modification  : Created file
 */

#include "object_entry_c.h"
#include "client_entry_c.h"

namespace dlock {

object_entry_c::object_entry_c(int32_t id, uint64_t offset)
    : m_object_desc(nullptr), m_id(id), m_offset(offset)
{
}

object_entry_c::~object_entry_c()
{
}

};  // namespace dlock
