/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: urpc slist test
 */
#include "gtest/gtest.h"
#include "urpc_hmap.h"

#define HMAP_SIZE 1024
TEST(UrpcUtilTest, TestHmap) {
    struct urpc_hmap func_hmap;
    struct func_entry {
        struct urpc_hmap_node node;
        uint32_t func_id;
    };

    int ret = urpc_hmap_init(&func_hmap, HMAP_SIZE);
    ASSERT_EQ(ret, 0);

    struct func_entry entry1, entry2, entry3;
    entry1.func_id = 1; entry2.func_id = 2; entry3.func_id = 3;
    urpc_hmap_insert(&func_hmap, &entry1.node, 1);
    urpc_hmap_insert(&func_hmap, &entry2.node, 2);
    urpc_hmap_insert(&func_hmap, &entry3.node, 3);
    ASSERT_EQ(urpc_hmap_count(&func_hmap), (uint32_t)3);

    struct func_entry *entry;
    URPC_HMAP_FOR_EACH_WITH_HASH(entry, node, 1, &func_hmap) {
        ASSERT_EQ(entry->func_id, (uint32_t)1);
    }
    URPC_HMAP_FOR_EACH_WITH_HASH(entry, node, 2, &func_hmap) {
        ASSERT_EQ(entry->func_id, (uint32_t)2);
    }
    URPC_HMAP_FOR_EACH_WITH_HASH(entry, node, 3, &func_hmap) {
        ASSERT_EQ(entry->func_id, (uint32_t)3);
    }

    int i = 1;
    URPC_HMAP_FOR_EACH(entry, node, &func_hmap) {
        ASSERT_EQ(entry->func_id, (uint32_t)i++);
    }
    struct func_entry *next;
    i = 1;
    URPC_HMAP_FOR_EACH_SAFE(entry, next, node, &func_hmap) {
        ASSERT_EQ(entry->func_id, (uint32_t)i++);
    }
    urpc_hmap_remove(&func_hmap, &entry1.node);
    urpc_hmap_remove(&func_hmap, &entry2.node);
    urpc_hmap_remove(&func_hmap, &entry3.node);
    ASSERT_EQ(urpc_hmap_count(&func_hmap), (uint32_t)0);

    urpc_hmap_uninit(&func_hmap);
}