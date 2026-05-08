/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: urpc slist test
 */
#include "gtest/gtest.h"
#include "urpc_slist.h"

TEST(UrpcUtilTest, TestSlist) {
    struct channel {
        URPC_SLIST_ENTRY(channel) node;
        int value;
    };

    URPC_SLIST_HEAD(channel_list_head, channel) channel_list_head;
    URPC_SLIST_INIT(&channel_list_head);

    struct channel c1, c2, c3;
    c1.value = 1; c2.value = 2; c3.value = 3;

    URPC_SLIST_INSERT_HEAD(&channel_list_head, &c1, node);
    URPC_SLIST_INSERT_HEAD(&channel_list_head, &c2, node);
    URPC_SLIST_INSERT_HEAD(&channel_list_head, &c3, node);

    struct channel *cur;
    int i = 3;
    URPC_SLIST_FOR_EACH(cur, &channel_list_head, node) {
        ASSERT_EQ(cur->value, i--);
    }

    URPC_SLIST_FOR_EACH(cur, &channel_list_head, node) {
        URPC_SLIST_REMOVE(&channel_list_head, cur, channel, node);
    }
    ASSERT_EQ(URPC_SLIST_EMPTY(&channel_list_head), true);

    URPC_SLIST_INSERT_HEAD(&channel_list_head, &c1, node);
    URPC_SLIST_INSERT_AFTER(&c1, &c2, node);
    URPC_SLIST_INSERT_AFTER(&c2, &c3, node);

    i = 1;
    URPC_SLIST_FOR_EACH(cur, &channel_list_head, node) {
        ASSERT_EQ(cur->value, i++);
    }
}