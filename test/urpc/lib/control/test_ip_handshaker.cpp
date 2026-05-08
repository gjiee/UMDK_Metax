/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: urpc ip handshake test
 */

#include "mockcpp/mockcpp.hpp"
#include "gtest/gtest.h"

#include "cp.h"
#include "ip_handshaker.h"
#include "urma_api.h"
#include "urpc_socket.h"
#include "urpc_framework_errno.h"

class ip_attach_serverTest : public ::testing::Test {
public:
    void SetUp() override
    {}
    void TearDown() override
    {
        GlobalMockObject::verify();
    }
    static void SetUpTestCase()
    {}
    static void TearDownTestCase()
    {
        GlobalMockObject::verify();
    }
};

TEST_F(ip_attach_serverTest, listenCfgCheckTest)
{
    int ret;
    urpc_host_info_t server;
    server.host_type = HOST_TYPE_IPV4;
    strncpy(server.ipv4.ip_addr, "192.168.00.10", URPC_IPV4_SIZE);
    ret = ip_check_listen_cfg(&server);
    EXPECT_EQ(ret, URPC_FAIL);

    strncpy(server.ipv4.ip_addr, "0.0.0.0", URPC_IPV4_SIZE);
    ret = ip_check_listen_cfg(&server);
    EXPECT_EQ(ret, URPC_FAIL);

    strncpy(server.ipv4.ip_addr, "192.168.0.10", URPC_IPV4_SIZE);
    ret = ip_check_listen_cfg(&server);
    EXPECT_EQ(ret, URPC_SUCCESS);

    server.host_type = HOST_TYPE_IPV6;
    strncpy(server.ipv6.ip_addr, "::", URPC_IPV6_SIZE);
    ret = ip_check_listen_cfg(&server);
    EXPECT_EQ(ret, URPC_FAIL);

    strncpy(server.ipv6.ip_addr, "::FGX", URPC_IPV6_SIZE);
    ret = ip_check_listen_cfg(&server);
    EXPECT_EQ(ret, URPC_FAIL);

    strncpy(server.ipv6.ip_addr, "1:1:1::24", URPC_IPV6_SIZE);
    ret = ip_check_listen_cfg(&server);
    EXPECT_EQ(ret, URPC_SUCCESS);
}

TEST_F(ip_attach_serverTest, listenThreadInitFailTest)
{
    int ret;
    urpc_host_info_t server;
    server.host_type = HOST_TYPE_IPV6;
    // can't bind to link local address
    strncpy(server.ipv6.ip_addr, "fe80::1e20:dbff:feaf:3c20", URPC_IPV6_SIZE);
    ret = ip_handshaker_init(&server, NULL);
    EXPECT_EQ(ret, URPC_FAIL);
}
