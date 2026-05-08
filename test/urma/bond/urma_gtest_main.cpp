/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 * Description: URMA gtest main source file
 * Author: Chen Wen
 * Create: 2023-11-20
 * Note:
 * History:
 */

#include "urma_gtest_basic.h"

static int gtest_add(int a, int b)
{
    return a + b;
}

TEST_F(UrmaBasicGTest, AddTest)
{
    EXPECT_EQ(3, gtest_add(1, 2));
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}