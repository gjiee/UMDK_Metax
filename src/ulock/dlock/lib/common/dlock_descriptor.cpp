/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 * File Name     : dlock_descriptor.cpp
 * Description   : dlock descriptor class
 * History       : create file & add functions
 * 1.Date        : 2022-03-26
 * Author        : geyi
 * Modification  : Created file
 */

#include <cstring>

#include "dlock_log.h"
#include "utils.h"
#include "dlock_descriptor.h"

namespace dlock {
dlock_descriptor::dlock_descriptor() : m_len(0), m_desc(nullptr)
{
}

dlock_descriptor::~dlock_descriptor()
{
    if (m_desc != nullptr) {
        free(m_desc);
        m_desc = nullptr;
    }
    m_len = 0;
}

dlock_status_t dlock_descriptor::descriptor_init(unsigned int len, unsigned char *buf)
{
    m_len = len;

    if (len > MAX_LOCK_DESC_LEN) {
        DLOCK_LOG_ERR("Invalid lenth(%d) to init dlock descriptor", len);
        return DLOCK_EINVAL;
    }

    m_desc = (unsigned char *)calloc(1, len);
    if (m_desc == nullptr) {
        DLOCK_LOG_ERR("calloc error (errno=%d %m)", errno);
        return DLOCK_ENOMEM;
    }

    static_cast<void>(memcpy(m_desc, buf, len));

    return DLOCK_SUCCESS;
}

bool dlock_descriptor::is_desc_equal(const dlock_descriptor *p_desc1, const dlock_descriptor *p_desc2) const
{
    bool ret = (p_desc1->m_len == p_desc2->m_len);
    return ((!ret) || (p_desc1->m_len == 0u)) ? ret :
           (std::memcmp(p_desc1->m_desc, p_desc2->m_desc, p_desc1->m_len) == 0);
}

unsigned int dlock_descriptor::get_desc_hash(const dlock_descriptor *p_next_desc) const
{
    if ((p_next_desc->m_len == 0u) || (p_next_desc->m_desc == nullptr)) {
        return 0;
    }
    return get_hash(p_next_desc->m_len, p_next_desc->m_desc);
}
};