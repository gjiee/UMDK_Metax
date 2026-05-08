/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2025. All rights reserved.
 * File Name     : test_lock_memory.cpp
 * Description   : dlock unit test cases for the functions of lock_memory class
 * History       : create file & add functions
 * 1.Date        : 2024-3-19
 * Author        : huying
 * Modification  : Created file
 */
#include <stdlib.h>
#include <sys/time.h>

#include "gtest/gtest.h"
#include "mockcpp/mokc.h"
#include "mockcpp/mockcpp.h"
#include "mockcpp/mockcpp.hpp"

#include "dlock_types.h"
#include "dlock_server.h"
#include "lock_memory.h"
#include "test_dlock_comm.h"

class test_lock_memory : public testing::Test {
protected:
    dlock_server *m_server;
    lock_memory *m_lock_memory;
    struct lock_cmd_msg m_cmd_msg;
    uint32_t m_lock_offset;
    uint32_t m_time_out;

    void SetUp()
    {
        m_server = new(std::nothrow) dlock_server(1);
        ASSERT_NE(m_server, nullptr);

        m_lock_memory = new(std::nothrow) lock_memory(LOCK_MEMORY_SIZE, true, m_server);
        ASSERT_NE(m_lock_memory, nullptr);
        m_server->m_lock_memory = m_lock_memory;

        struct timeval tv_start;
        gettimeofday(&tv_start, NULL);

        m_lock_offset = 32;
        m_time_out = tv_start.tv_sec + 60000;

        m_cmd_msg.magic_no = DLOCK_DP_MAGIC_NO;
        m_cmd_msg.version = DLOCK_PROTO_VERSION;
        m_cmd_msg.message_id = 100;
        m_cmd_msg.rsvd = 0;
        m_cmd_msg.lock_type = DLOCK_FAIR;
        m_cmd_msg.op_code = EXCLUSIVE_TRYLOCK;
        m_cmd_msg.op_ret = DLOCK_SUCCESS;
        m_cmd_msg.lock_offset = m_lock_offset;
        m_cmd_msg.ls.fl.m_exclusive = 2;
        m_cmd_msg.ls.fl.m_shared = 1;
        m_cmd_msg.ls.fl.n_exclusive = 1;
        m_cmd_msg.ls.fl.n_shared = 1;
        m_cmd_msg.ls.fl.time_out = m_time_out;
    }

    void TearDown()
    {
        GlobalMockObject::verify();

        // When m_server is deleted, m_lock_memory is also deleted.
        delete m_server;
    }
};

class test_get_lock_memory : public testing::Test {
protected:
    dlock_server *m_server;
    lock_memory *m_lock_memory;

    void SetUp()
    {
        m_server = new(std::nothrow) dlock_server(1);
        m_lock_memory = new(std::nothrow) lock_memory(LOCK_MEMORY_SIZE, true, m_server);

        ASSERT_NE(m_server, nullptr);
        ASSERT_NE(m_lock_memory, nullptr);

        m_server->m_lock_memory = m_lock_memory;
    }

    void TearDown()
    {
        GlobalMockObject::verify();

        // When m_server is deleted, m_lock_memory is also deleted.
        delete m_server;
    }
};

TEST_F(test_lock_memory, test_update_lock_state_1_update_fair_lock_state_success_1)
{
    lock_state *p_lockstate = reinterpret_cast<lock_state*>(m_lock_memory->m_p_lock_memory + m_lock_offset);

    m_lock_memory->update_lock_state(&m_cmd_msg);
    EXPECT_EQ(p_lockstate->fl.m_exclusive, 2);
    EXPECT_EQ(p_lockstate->fl.m_shared, 1);
    EXPECT_EQ(p_lockstate->fl.n_exclusive, 1);
    EXPECT_EQ(p_lockstate->fl.n_shared, 1);
    EXPECT_EQ(p_lockstate->fl.time_out, m_time_out);
}

TEST_F(test_lock_memory, test_update_lock_state_2_update_fair_lock_state_success_2)
{
    lock_state *p_lockstate = reinterpret_cast<lock_state*>(m_lock_memory->m_p_lock_memory + m_lock_offset);

    m_cmd_msg.op_ret = DLOCK_EAGAIN;
    m_lock_memory->update_lock_state(&m_cmd_msg);
    EXPECT_EQ(p_lockstate->fl.m_exclusive, 2);
    EXPECT_EQ(p_lockstate->fl.m_shared, 1);
    EXPECT_EQ(p_lockstate->fl.n_exclusive, 1);
    EXPECT_EQ(p_lockstate->fl.n_shared, 1);
    EXPECT_EQ(p_lockstate->fl.time_out, m_time_out);
}

TEST_F(test_lock_memory, test_update_lock_state_3_invalid_lock_type)
{
    lock_state *p_lockstate = reinterpret_cast<lock_state*>(m_lock_memory->m_p_lock_memory + m_lock_offset);

    m_cmd_msg.lock_type = DLOCK_MAX;
    m_lock_memory->update_lock_state(&m_cmd_msg);
    EXPECT_EQ(p_lockstate->fl.m_exclusive, 0u);
    EXPECT_EQ(p_lockstate->fl.m_shared, 0u);
    EXPECT_EQ(p_lockstate->fl.n_exclusive, 0u);
    EXPECT_EQ(p_lockstate->fl.n_shared, 0u);
    EXPECT_EQ(p_lockstate->fl.time_out, 0u);
}

TEST_F(test_lock_memory, test_update_lock_state_4_invalid_op_code)
{
    lock_state *p_lockstate = reinterpret_cast<lock_state*>(m_lock_memory->m_p_lock_memory + m_lock_offset);

    m_cmd_msg.op_code = OP_CODE_MAX;
    m_lock_memory->update_lock_state(&m_cmd_msg);
    EXPECT_EQ(p_lockstate->fl.m_exclusive, 0u);
    EXPECT_EQ(p_lockstate->fl.m_shared, 0u);
    EXPECT_EQ(p_lockstate->fl.n_exclusive, 0u);
    EXPECT_EQ(p_lockstate->fl.n_shared, 0u);
    EXPECT_EQ(p_lockstate->fl.time_out, 0u);
}

TEST_F(test_lock_memory, test_update_lock_state_5_invalid_lock_offset)
{
    lock_state *p_lockstate = reinterpret_cast<lock_state*>(m_lock_memory->m_p_lock_memory + m_lock_offset);

    m_cmd_msg.lock_offset = LOCK_MEMORY_SIZE - g_lock_size[DLOCK_FAIR] + 1;
    m_lock_memory->update_lock_state(&m_cmd_msg);
    EXPECT_EQ(p_lockstate->fl.m_exclusive, 0u);
    EXPECT_EQ(p_lockstate->fl.m_shared, 0u);
    EXPECT_EQ(p_lockstate->fl.n_exclusive, 0u);
    EXPECT_EQ(p_lockstate->fl.n_shared, 0u);
    EXPECT_EQ(p_lockstate->fl.time_out, 0u);
}

TEST_F(test_lock_memory, test_do_lock_cmd_1_invalid_lock_type)
{
    lock_state ls;

    m_cmd_msg.lock_type = DLOCK_MAX;
    int ret = m_lock_memory->do_lock_cmd(10001, &m_cmd_msg, ls);
    EXPECT_EQ(ret, DLOCK_DONE);
    EXPECT_EQ(m_cmd_msg.op_ret, DLOCK_EINVAL);
    EXPECT_EQ(m_server->m_stats.stats[DEBUG_STATS_EINVAL_LOCK_TYPE], 1u);
}

TEST_F(test_lock_memory, test_do_lock_cmd_2_invalid_op_code)
{
    lock_state ls;

    m_cmd_msg.op_code = OP_CODE_MAX;
    int ret = m_lock_memory->do_lock_cmd(10001, &m_cmd_msg, ls);
    EXPECT_EQ(ret, DLOCK_DONE);
    EXPECT_EQ(m_cmd_msg.op_ret, DLOCK_EINVAL);
    EXPECT_EQ(m_server->m_stats.stats[DEBUG_STATS_EINVAL_LOCK_OP], 1u);
}

TEST_F(test_lock_memory, test_do_lock_cmd_3_invalid_lock_offset)
{
    lock_state ls;

    m_cmd_msg.lock_offset = LOCK_MEMORY_SIZE - g_lock_size[DLOCK_FAIR] + 1;
    int ret = m_lock_memory->do_lock_cmd(10001, &m_cmd_msg, ls);
    EXPECT_EQ(ret, DLOCK_DONE);
    EXPECT_EQ(m_cmd_msg.op_ret, DLOCK_EINVAL);
    EXPECT_EQ(m_server->m_stats.stats[DEBUG_STATS_EINVAL_LOCK_OFFSET], 1u);
}

TEST_F(test_get_lock_memory, test_not_primary_server)
{
    m_lock_memory->m_is_primary = false;
    uint32_t ret_offset = m_lock_memory->get_lock_memory(DLOCK_ATOMIC);
    EXPECT_EQ(ret_offset, UINT_MAX);
}

TEST_F(test_get_lock_memory, test_free_memory_map_empty_1)
{
    m_lock_memory->m_free_memory_map.clear();
    uint32_t ret_offset = m_lock_memory->get_lock_memory(DLOCK_ATOMIC);
    EXPECT_EQ(ret_offset, UINT_MAX);
}

TEST_F(test_get_lock_memory, test_no_enough_memory_1)
{
    m_lock_memory->m_free_memory_map.clear();
    m_lock_memory->m_free_memory_map[8] = 8;
    uint32_t ret_offset = m_lock_memory->get_lock_memory(DLOCK_FAIR);
    EXPECT_EQ(ret_offset, UINT_MAX);
}

TEST_F(test_get_lock_memory, test_get_lock_memory_success)
{
    uint32_t ret_offset = m_lock_memory->get_lock_memory(DLOCK_ATOMIC, 8);
    EXPECT_EQ(ret_offset, 8u);
}

TEST_F(test_get_lock_memory, test_free_memory_map_empty_2)
{
    m_lock_memory->m_free_memory_map.clear();
    uint32_t ret_offset = m_lock_memory->get_lock_memory(DLOCK_ATOMIC, 8);
    EXPECT_EQ(ret_offset, UINT_MAX);
}

TEST_F(test_get_lock_memory, test_no_enough_memory_2)
{
    m_lock_memory->m_free_memory_map.clear();
    m_lock_memory->m_free_memory_map[8] = 32;
    uint32_t ret_offset = m_lock_memory->get_lock_memory(DLOCK_FAIR, 32);
    EXPECT_EQ(ret_offset, UINT_MAX);
}
