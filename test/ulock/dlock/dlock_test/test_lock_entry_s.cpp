/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2025. All rights reserved.
 * File Name     : test_lock_entry_s.cpp
 * Description   : dlock unit test cases for the functions of lock_entry_s class
 * History       : create file & add functions
 * 1.Date        : 2024-3-19
 * Author        : huying
 * Modification  : Created file
 */
#include <stdlib.h>
#include <climits>

#include "gtest/gtest.h"
#include "mockcpp/mokc.h"
#include "mockcpp/mockcpp.h"
#include "mockcpp/mockcpp.hpp"

#include "dlock_types.h"
#include "lock_entry_s.h"
#include "lock_memory.h"

#include "test_dlock_comm.h"

TEST(test_lock_entry_s, test_param_p_lock_memory_nullptr_1)
{
    int lock_id = 100;
    lock_entry_s *lock_entry = new(std::nothrow) lock_entry_s(lock_id, DLOCK_ATOMIC, nullptr);
    EXPECT_NE(lock_entry, nullptr);
    EXPECT_EQ(lock_entry->m_lock_dec, nullptr);
    EXPECT_EQ(lock_entry->m_lock_id, lock_id);
    EXPECT_EQ(lock_entry->m_lock_type, DLOCK_ATOMIC);
    EXPECT_EQ(lock_entry->m_p_lock_memory, nullptr);
    EXPECT_EQ(lock_entry->m_lock_offset, UINT_MAX);

    delete lock_entry;
}

TEST(test_lock_entry_s, test_param_p_lock_memory_nullptr_2)
{
    int lock_id = 100;
    uint32_t lock_offset = 32;
    lock_entry_s *lock_entry = new(std::nothrow) lock_entry_s(lock_id, DLOCK_ATOMIC, lock_offset, nullptr);
    EXPECT_NE(lock_entry, nullptr);
    EXPECT_EQ(lock_entry->m_lock_dec, nullptr);
    EXPECT_EQ(lock_entry->m_lock_id, lock_id);
    EXPECT_EQ(lock_entry->m_lock_type, DLOCK_ATOMIC);
    EXPECT_EQ(lock_entry->m_p_lock_memory, nullptr);
    EXPECT_EQ(lock_entry->m_lock_offset, UINT_MAX);

    delete lock_entry;
}

TEST(test_lock_entry_s, test_init_succ)
{
    lock_memory *temp_lock_memory = new(std::nothrow) lock_memory(LOCK_MEMORY_SIZE, true, nullptr);
    ASSERT_NE(temp_lock_memory, nullptr);

    int lock_id = 100;
    uint32_t lock_offset = 32;
    lock_entry_s *lock_entry = new(std::nothrow) lock_entry_s(lock_id, DLOCK_ATOMIC, lock_offset, temp_lock_memory);
    EXPECT_NE(lock_entry, nullptr);
    EXPECT_EQ(lock_entry->m_lock_dec, nullptr);
    EXPECT_EQ(lock_entry->m_lock_id, lock_id);
    EXPECT_EQ(lock_entry->m_lock_type, DLOCK_ATOMIC);
    EXPECT_EQ(lock_entry->m_p_lock_memory, temp_lock_memory);
    EXPECT_EQ(lock_entry->m_lock_offset, lock_offset);

    delete lock_entry;
    delete temp_lock_memory;
}
