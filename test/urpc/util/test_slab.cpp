/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: urpc dynamic buffer statistics test
 */
#include "gtest/gtest.h"
#include "urpc_slab.h"

TEST(UrpcESlabTest, TestESlabAllocOutOfRange)
{
    eslab_t slab;
    uint32_t id;
    slab.next_free = 11;
    slab.total = 10;
    slab.obj_size = 10;
    slab.addr = malloc(100);
    (void)pthread_spin_init(&slab.lock, PTHREAD_PROCESS_PRIVATE);

    void *result = eslab_alloc(&slab, &id);

    ASSERT_EQ(errno, URPC_ERR_EPERM);
    ASSERT_EQ(result, nullptr);

    free(slab.addr);
}

TEST(UrpcESlabTest, TestESlabAllocUseAfterFree)
{
    eslab_t slab;
    uint32_t id;
    slab.next_free = 10;
    slab.total = 10;
    slab.obj_size = 10;
    slab.addr = malloc(100);
    (void)pthread_spin_init(&slab.lock, PTHREAD_PROCESS_PRIVATE);

    void *result = eslab_alloc(&slab, &id);

    ASSERT_EQ(errno, URPC_ERR_EPERM);
    ASSERT_EQ(result, nullptr);

    free(slab.addr);
}
