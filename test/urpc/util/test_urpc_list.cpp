/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: urpc list test
 */
#include "gtest/gtest.h"
#include "urpc_list.h"

TEST(UrpcUtilTest, TestList)
{
    struct channel {
        struct urpc_list node;
        int id;
    };
    struct urpc_list channel_list;
    urpc_list_init(&channel_list);

    struct channel c1, c2, c3;
    c1.id = 1; c2.id = 2; c3.id = 3;
    urpc_list_push_back(&channel_list, &c1.node);
    urpc_list_push_back(&channel_list, &c2.node);
    urpc_list_push_back(&channel_list, &c3.node);

    struct channel *channel;
    int i = 1;
    URPC_LIST_FOR_EACH(channel, node, &channel_list)
    {
        ASSERT_EQ(channel->id, i++);
    }
    struct channel *next;
    i = 1;
    URPC_LIST_FOR_EACH_SAFE(channel, next, node, &channel_list)
    {
        ASSERT_EQ(channel->id, i++);
        urpc_list_remove(&channel->node);
    }
    ASSERT_EQ(urpc_list_is_empty(&channel_list), true);
}