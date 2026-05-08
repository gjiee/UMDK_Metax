/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: urpc dfx test
 */

#include "mockcpp/mockcpp.hpp"
#include "gtest/gtest.h"
#include "state.h"
#include "urpc_framework_api.h"
#include "urpc_framework_errno.h"
#include "queue.h"
#include "urpc_admin_cmd.c"
#include "queue_info.h"
#include "queue_info.c"
#include "queue_info_cmd.c"
#include "dfx.c"
#include "unix_server.h"
#include "urpc_bitmap.h"
#include "urpc_admin_param.h"

#include "dfx.h"

static int g_cmd_case[] = {
    'V', 'D', 'c', 'P', 't', 's', 'q', 'a', 'C', 'S'
};

#define CMD_CASE_SIZE sizeof(g_cmd_case) / sizeof(int)

static urpc_log_config_t g_log_cfg;

class dfx_test : public ::testing::Test {
public:
    // SetUP 在每一个 TEST_F 测试开始前执行一次
    void SetUp() override
    {
        g_log_cfg.log_flag = URPC_LOG_FLAG_LEVEL;
        g_log_cfg.level = URPC_LOG_LEVEL_DEBUG;
        ASSERT_EQ(urpc_log_config_set(&g_log_cfg), URPC_SUCCESS);
        urpc_state_set(URPC_STATE_INIT);
    }

    // TearDown 在每一个 TEST_F 测试完成后执行一次
    void TearDown() override
    {
        g_log_cfg.level = URPC_LOG_LEVEL_INFO;
        ASSERT_EQ(urpc_log_config_set(&g_log_cfg), URPC_SUCCESS);
        GlobalMockObject::verify();
    }

    // SetUpTestCase 在所有 TEST_F 测试开始前执行一次
    static void SetUpTestCase()
    {}

    // TearDownTestCase 在所有 TEST_F 测试完成后执行一次
    static void TearDownTestCase()
    {}
};

TEST_F(dfx_test, TestDfxInitErrBranch)
{
    MOCKER(version_cmd_init).stubs().will(returnValue(0));
    MOCKER(stats_cmd_init).stubs().will(returnValue(0));
    MOCKER(queue_info_cmd_init).stubs().will(returnValue(0));
    MOCKER(urpc_perf_cmd_init).stubs().will(returnValue(0));
    MOCKER(dbuf_cmd_init).stubs().will(returnValue(-1));
    ASSERT_EQ(urpc_dfx_init(), -1);
}

TEST_F(dfx_test, TestDfxQueueErrBranch)
{
    urpc_ipc_ctl_head_t req_ctl = {0};
    urpc_ipc_ctl_head_t rsp_ctl = {0};
    char *reply = NULL;

    process_queue_local_all(&req_ctl, NULL, &rsp_ctl, &reply);
    ASSERT_EQ(rsp_ctl.error_code, -EINVAL);

    req_ctl.data_size = sizeof(urpc_queue_cmd_input_t);
    MOCKER(get_queue_trans_info).stubs().will(returnValue(URPC_FAIL));
    process_queue_local_all(&req_ctl, NULL, &rsp_ctl, &reply);
    ASSERT_EQ(rsp_ctl.error_code, URPC_FAIL);

    req_ctl.data_size = 0;
    process_queue_by_client_chid(&req_ctl, NULL, &rsp_ctl, &reply);
    ASSERT_EQ(rsp_ctl.error_code, -EINVAL);

    req_ctl.data_size = sizeof(urpc_queue_cmd_input_t);
    MOCKER(channel_get_queue_trans_info).stubs().will(returnValue(URPC_SUCCESS));
    ASSERT_EQ(rsp_ctl.data_size , 0);

    req_ctl.data_size = 0;
    process_queue_by_server_chid(&req_ctl, NULL, &rsp_ctl, &reply);
    ASSERT_EQ(rsp_ctl.error_code, -EINVAL);

    req_ctl.data_size = sizeof(urpc_queue_cmd_input_t);
    MOCKER(server_channel_get_queue_trans_info).stubs().will(returnValue(URPC_SUCCESS));
    ASSERT_EQ(rsp_ctl.data_size , 0);
}

TEST_F(dfx_test, TestDfxQueueCmdErrBranch)
{
    queue_trans_info_t *qti = (queue_trans_info_t *)(uintptr_t)calloc(
        1, sizeof(queue_trans_info_t) + 1 * sizeof(queue_trans_resource_spec_t));
    queue_info_queue_type_t type = QUEUE_INFO_TYPE_LOCAL;
    queue_info_queue_usage_t usage = QUEUE_INFO_USAGE_NORMAL;

    qti->trans_spec[0].uasid = URPC_U32_FAIL;
    qti->trans_spec[0].tpn = URPC_U32_FAIL;
    print_queue_trans_info_line(UINT32_MAX, type, usage, qti, 0);
    ASSERT_EQ(qti->trans_spec_cnt, 0);

    qti->trans_spec[0].tpn = 0;
    print_queue_trans_info_line(UINT32_MAX, type, usage, qti, 0);
    ASSERT_EQ(qti->trans_spec_cnt, 0);

    qti->trans_spec[0].uasid = 0;
    qti->trans_spec[0].tpn = URPC_U32_FAIL;
    print_queue_trans_info_line(UINT32_MAX, type, usage, qti, 0);
    ASSERT_EQ(qti->trans_spec_cnt, 0);

    qti->trans_spec[0].tpn = 0;
    print_queue_trans_info_line(UINT32_MAX, type, usage, qti, 0);
    ASSERT_EQ(qti->trans_spec_cnt, 0);

    qti->trans_spec[0].uasid = URPC_U32_FAIL;
    qti->trans_spec[0].tpn = URPC_U32_FAIL;
    print_queue_trans_info_line(0, type, usage, qti, 0);
    ASSERT_EQ(qti->trans_spec_cnt, 0);

    qti->trans_spec[0].uasid = URPC_U32_FAIL;
    qti->trans_spec[0].tpn = 0;
    print_queue_trans_info_line(0, type, usage, qti, 0);
    ASSERT_EQ(qti->trans_spec_cnt, 0);

    qti->trans_spec[0].uasid = 0;
    qti->trans_spec[0].tpn = URPC_U32_FAIL;
    print_queue_trans_info_line(0, type, usage, qti, 0);
    ASSERT_EQ(qti->trans_spec_cnt, 0);


    qti->trans_spec[0].uasid = 0;
    qti->trans_spec[0].tpn = 0;
    print_queue_trans_info_line(0, type, usage, qti, 0);
    ASSERT_EQ(qti->trans_spec_cnt, 0);

    free(qti);
}

static void set_case(urpc_admin_config_t *cfg, char c)
{
    urpc_bitmap_t bitmap = (urpc_bitmap_t)(uintptr_t)&cfg->bitmap;
    switch (c) {
        case 'V':
            urpc_bitmap_set1(bitmap, URPC_CMD_BITS_VERSION);
            break;
        case 'P':
            urpc_bitmap_set1(bitmap, URPC_CMD_BITS_PERF);
            break;
        case 's':
            urpc_bitmap_set1(bitmap, URPC_CMD_BITS_STATS);
            break;
        case 'a':
            urpc_bitmap_set1(bitmap, URPC_CMD_BITS_QUEUE_INFO);
            break;
        case 'C':
            urpc_bitmap_set1(bitmap, URPC_CMD_BITS_CLIENT_CHANNEL);
            break;
        case 'S':
            urpc_bitmap_set1(bitmap, URPC_CMD_BITS_SERVER_CHANNEL);
            break;
        case 'q':
            urpc_bitmap_set1(bitmap, URPC_CMD_BITS_QUEUE_ID);
            break;
        case 't':
            urpc_bitmap_set1(bitmap, URPC_CMD_BITS_THRESH);
            break;
        case 'D':
            urpc_bitmap_set1(bitmap, URPC_CMD_BITS_DBUF);
            break;
        case 'c':
            urpc_bitmap_set1(bitmap, URPC_CMD_BITS_CHANNEL);
            break;
        default:
            break;
    }
}

TEST_F(dfx_test, TestDfxCmdNormalCase)
{
    urpc_admin_config_t cfg_V = {0};
    set_case(&cfg_V, 'V');
    ASSERT_EQ(admin_cfg_check(&cfg_V), 0);

    urpc_admin_config_t cfg_D = {0};
    set_case(&cfg_D, 'D');
    ASSERT_EQ(admin_cfg_check(&cfg_D), 0);

    urpc_admin_config_t cfg_c = {0};
    set_case(&cfg_c, 'c');
    ASSERT_EQ(admin_cfg_check(&cfg_c), 0);
}

TEST_F(dfx_test, TestDfxCmdGroupCase)
{
    urpc_admin_config_t cfg_P = {0};
    set_case(&cfg_P, 'P');
    ASSERT_EQ(admin_cfg_check(&cfg_P), 0);

    urpc_admin_config_t cfg_P_t = {0};
    set_case(&cfg_P_t, 'P');
    set_case(&cfg_P_t, 't');
    ASSERT_EQ(admin_cfg_check(&cfg_P_t), 0);

    urpc_admin_config_t cfg_s = {0};
    set_case(&cfg_s, 's');
    ASSERT_EQ(admin_cfg_check(&cfg_s), 0);

    urpc_admin_config_t cfg_s_q = {0};
    set_case(&cfg_s_q , 's');
    set_case(&cfg_s_q , 'q');
    ASSERT_EQ(admin_cfg_check(&cfg_s_q), 0);

    urpc_admin_config_t cfg_a = {0};
    set_case(&cfg_a, 'a');
    ASSERT_EQ(admin_cfg_check(&cfg_a), 0);

    urpc_admin_config_t cfg_a_C = {0};
    set_case(&cfg_a_C, 'a');
    set_case(&cfg_a_C, 'C');
    ASSERT_EQ(admin_cfg_check(&cfg_a_C), 0);

    urpc_admin_config_t cfg_a_S = {0};
    set_case(&cfg_a_S, 'a');
    set_case(&cfg_a_S, 'S');
    ASSERT_EQ(admin_cfg_check(&cfg_a_S), 0);

    urpc_admin_config_t cfg_a_q = {0};
    set_case(&cfg_a_q, 'a');
    set_case(&cfg_a_q, 'q');
    ASSERT_EQ(admin_cfg_check(&cfg_a_q), 0);
}

TEST_F(dfx_test, TestDfxCmdInvalidCase)
{
    int normal_case_array2[][2] = {
        {'V', 'D'}, {'V', 'c'}, {'D', 'c'},
        {'c', 'P'}, {'c', 't'}, {'c', 's'}, {'c', 'q'}, {'c', 'a'}, {'c', 'C'}, {'c', 'S'}, {'c', 'q'},
        {'P', 's'}, {'P', 'q'}, {'P', 'a'}, {'P', 'C'}, {'P', 'S'}, {'P', 'q'},
        {'s', 't'}, {'P', 'a'}, {'P', 'C'}, {'P', 'S'}

    };
    for (int i = 0; i < sizeof(normal_case_array2) / sizeof(normal_case_array2[0]); i++) {
        urpc_admin_config_t cfg = {0};
        set_case(&cfg, normal_case_array2[i][0]);
        set_case(&cfg, normal_case_array2[i][1]);
        ASSERT_EQ(admin_cfg_check(&cfg), -1);
    }

    for (int i = 0; i < CMD_CASE_SIZE - 2; i++) {
        for (int j = i + 1; j < CMD_CASE_SIZE - 1; j++) {
            for (int k = j + 1; k < CMD_CASE_SIZE; k++) {
                urpc_admin_config_t cfg = {0};
                set_case(&cfg, g_cmd_case[i]);
                set_case(&cfg, g_cmd_case[j]);
                set_case(&cfg, g_cmd_case[k]);
                ASSERT_EQ(admin_cfg_check(&cfg), -1);
            }
        }
    }

    for (int i = 0; i < CMD_CASE_SIZE - 3; i++) {
        for (int j = i + 1; j < CMD_CASE_SIZE - 2; j++) {
            for (int k = j + 1; k < CMD_CASE_SIZE - 1; k++) {
                for (int l = k + 1; l < CMD_CASE_SIZE; l++) {
                    urpc_admin_config_t cfg = {0};
                    set_case(&cfg, g_cmd_case[i]);
                    set_case(&cfg, g_cmd_case[j]);
                    set_case(&cfg, g_cmd_case[k]);
                    set_case(&cfg, g_cmd_case[l]);
                    ASSERT_EQ(admin_cfg_check(&cfg), -1);
                }
            }
        }
    }
}