/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: urpc dynamic buffer statistics test
 */
#include "gtest/gtest.h"
#include "urpc_dbuf_stat.h"

static void test_allocate_dynamic_buffer(char **malloc_buf_addr, char **calloc_buf_addr)
{
    for (uint32_t i = 0; i < URPC_DBUF_TYPE_MAX ; i++) {
        uint32_t buf_size = 128 * (i + 1);
        malloc_buf_addr[i] = (char *)urpc_dbuf_malloc((urpc_dbuf_type_t)i, buf_size);
        EXPECT_EQ(malloc_buf_addr[i] != NULL, true);
    }

    for (uint32_t i = 0; i < URPC_DBUF_TYPE_MAX ; i++) {
        uint32_t buf_size = 128 * (URPC_DBUF_TYPE_MAX - i);
        calloc_buf_addr[i] = (char *)urpc_dbuf_calloc((urpc_dbuf_type_t)i, 1, buf_size);
        EXPECT_EQ(calloc_buf_addr[i] != NULL, true);
    }
}

static void test_free_dynamic_buffer(char **malloc_buf_addr, char **calloc_buf_addr)
{
    for (uint32_t i = 0; i < URPC_DBUF_TYPE_MAX ; i++) {
        urpc_dbuf_free(malloc_buf_addr[i]);
    }

    for (uint32_t i = 0; i < URPC_DBUF_TYPE_MAX ; i++) {
        urpc_dbuf_free(calloc_buf_addr[i]);
    }
}

TEST(UrpcDbufStatTest, TestBasicOperations)
{
    urpc_dbuf_stat_record_enable();
    char *malloc_buf_addr[URPC_DBUF_TYPE_MAX];
    memset(malloc_buf_addr, 0, sizeof(malloc_buf_addr));

    char *calloc_buf_addr[URPC_DBUF_TYPE_MAX];
    memset(calloc_buf_addr, 0, sizeof(calloc_buf_addr));

    test_allocate_dynamic_buffer(malloc_buf_addr, calloc_buf_addr);

    uint64_t stat[URPC_DBUF_STAT_NUM] = {0};
    urpc_dbuf_stat_get(stat, URPC_DBUF_STAT_NUM);
    uint32_t buf_size = 128 * (URPC_DBUF_TYPE_MAX + 1) + 2 * sizeof(urpc_dbuf_t);
    for (uint32_t i = 0; i < URPC_DBUF_TYPE_MAX; i++) {
        ASSERT_EQ(stat[i], buf_size);
    }
    ASSERT_EQ(stat[URPC_DBUF_TYPE_MAX], buf_size * URPC_DBUF_TYPE_MAX);

    test_free_dynamic_buffer(malloc_buf_addr, calloc_buf_addr);

    urpc_dbuf_stat_get(stat, URPC_DBUF_STAT_NUM);
    for (uint32_t i = 0; i < URPC_DBUF_STAT_NUM; i++) {
        ASSERT_EQ(stat[i], (uint64_t)0);
    }
    urpc_dbuf_stat_record_disable();
}

TEST(UrpcDbufStatTest, TestDbufStatNameGet)
{
    char test_result[100];
    strcpy(test_result, urpc_dbuf_stat_name_get(0));
    ASSERT_EQ(strcmp(test_result, "Queue"), 0);
    strcpy(test_result, urpc_dbuf_stat_name_get(URPC_DBUF_STAT_NUM - 1));
    ASSERT_EQ(strcmp(test_result, "Total Usage"), 0);
    strcpy(test_result, urpc_dbuf_stat_name_get(URPC_DBUF_STAT_NUM));
    ASSERT_EQ(strcmp(test_result, "Unknown"), 0);
}