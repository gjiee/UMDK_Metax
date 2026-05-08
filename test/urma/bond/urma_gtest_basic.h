/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 * Description: URMA gtest basic class header file
 * Author: Chen Wen
 * Create: 2023-11-20
 * Note:
 * History:
 */

#ifndef URMA_GTEST_BASIC_H
#define URMA_GTEST_BASIC_H

#include <gtest/gtest.h>
#include "mockcpp/mokc.h"

#ifdef _cplusplus
#define atomic_int (std::atomic_int)
#define atomic_uint (std::atomic_uint)
#define atomic_bool (std::atomic_bool)
#define atomic_ulong (std::atomic_ulong)
#define atomic_fetch_add (std::atomic_fetch_add)
#define atomic_fetch_sub (std::atomic_fetch_sub)
#define atomic_store_explicit (std::atomic_store_explicit)
#define memory_order_relaxed (std::memory_order_relaxed)
#define atomic_load (std::atomic_load)
#endif

class UrmaBasicGTest : public ::testing::Test
{
public:
    UrmaBasicGTest() {}
    ~UrmaBasicGTest() {}
protected:
    void SetUp() override
    {
    }

    void TearDown() override
    {
        // clear all mock rules
        GlobalMockObject::verify();
    }
private:
};

#endif