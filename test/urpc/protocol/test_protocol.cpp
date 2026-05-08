/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: urpc protocol test
 */
 
#include "gtest/gtest.h"
extern "C" {
    #include "protocol.h"
}

uint8_t g_req_head[] = {
    0b00010010, 0b10000011, 0b00000100, 0b00000000,
    0b00000000, 0b00000000, 0b00000000, 0b00000000,
    0b00000101, 0b00000000, 0b00000000, 0b00000000,
    0b00000110, 0b00000000, 0b00000000, 0b00000000,
    0b00000111, 0b00000000, 0b00000000, 0b00001000
};

uint8_t g_ack_head[] = {
    0b00010010, 0b00000000, 0b00001001, 0b00000000,
    0b00000110, 0b00000000, 0b00000000, 0b00000000,
    0b00000111, 0b00000000, 0b00000000, 0b00000000
};

uint8_t g_rsp_head[] = {
    0b00010010, 0b00001010, 0b00001001, 0b00000000,
    0b00000110, 0b00000000, 0b00000000, 0b00000000,
    0b00000111, 0b00000000, 0b00000000, 0b00000000,
    0b00001011, 0b00000000, 0b00000000, 0b00000000
};

uint8_t g_keepalive_head[] = {
    0b00000000, 0b00000000, 0b00000000, 0b00000000,
    0b00000000, 0b00000000, 0b00000000, 0b00000000,
    0b00000000, 0b00000000, 0b00000000, 0b00000000,
    0b00000000, 0b00000000, 0b00000000, 0b00000000,
    0b00000000, 0b00000000, 0b00000000, 0b00000000,
    0b00000000, 0b00000000, 0b00000000, 0b00000000,
    0b00000000, 0b00000000, 0b00000000, 0b00000000,
    0b00000000, 0b00000000, 0b00000000, 0b00000000,
    0b00000000, 0b00000000, 0b00000000, 0b00000000,
    0b00000000, 0b00000000, 0b00000000, 0b00000000,
    0b00000000, 0b00000000, 0b00000000, 0b00000000,
};

#define VERSION                     0x1
#define TYPE                        0x2
#define ACK                         0x1
#define DMA_CNT                     0x3
#define FUNC                        0x4
#define REQ_TOTAL_SIZE              0x5
#define REQ_ID                      0x6
#define CHANNEL                     0x7
#define FUNC_DEF                    0x8
#define ID_RANGE                    0x9
#define STATUS                      0xA
#define RET_DATA_SIZE               0xB

TEST(ProtoTest, ReqFillParseTest) {
    urpc_req_head_t req_head;
    urpc_req_fill_basic_info(&req_head, ACK, CHANNEL);
    urpc_req_fill_req_info_without_dma(&req_head, FUNC, REQ_TOTAL_SIZE, REQ_ID, 0);

    ASSERT_EQ(URPC_PROTO_VERSION, urpc_req_parse_version(&req_head));
    ASSERT_EQ(URPC_MSG_REQ, urpc_req_parse_type(&req_head));
    ASSERT_EQ(ACK, urpc_req_parse_ack(&req_head));
    ASSERT_EQ(DEFAULT_DMA_COUNT, urpc_req_parse_arg_dma_count(&req_head));
    ASSERT_EQ((uint64_t)FUNC, urpc_req_parse_function(&req_head));
    ASSERT_EQ((uint32_t)REQ_TOTAL_SIZE, urpc_req_parse_req_total_size(&req_head));
    ASSERT_EQ((uint32_t)REQ_ID, urpc_req_parse_req_id(&req_head));
    ASSERT_EQ((uint32_t)CHANNEL, urpc_req_parse_client_channel(&req_head));
    ASSERT_EQ(DEFAULT_FUNCTION_DEFINED, urpc_req_parse_function_defined(&req_head));
}

TEST(ProtoTest, AckFillParseTest) {
    urpc_ack_head_t ack_head;
    urpc_ack_fill_one_req_head(&ack_head, CHANNEL, REQ_ID);

    ASSERT_EQ(URPC_PROTO_VERSION, urpc_ack_parse_version(&ack_head));
    ASSERT_EQ(URPC_MSG_ACK, urpc_ack_parse_type(&ack_head));
    ASSERT_EQ(DEFAULT_REQ_ID_RANGE, urpc_ack_parse_req_id_range(&ack_head));
    ASSERT_EQ((uint32_t)REQ_ID, urpc_ack_parse_req_id(&ack_head));
    ASSERT_EQ((uint32_t)CHANNEL, urpc_ack_parse_client_channel(&ack_head));
}

TEST(ProtoTest, RspFillParseTest) {
    urpc_rsp_head_t rsp_head;
    urpc_rsp_fill_basic_info(&rsp_head, STATUS, CHANNEL, 0);
    urpc_rsp_fill_one_req_info(&rsp_head, REQ_ID, RET_DATA_SIZE, NULL);

    ASSERT_EQ(URPC_PROTO_VERSION, urpc_rsp_parse_version(&rsp_head));
    ASSERT_EQ(URPC_MSG_RSP, urpc_rsp_parse_type(&rsp_head));
    ASSERT_EQ(STATUS, urpc_rsp_parse_status(&rsp_head));
    ASSERT_EQ(DEFAULT_REQ_ID_RANGE, urpc_rsp_parse_req_id_range(&rsp_head));
    ASSERT_EQ((uint32_t)REQ_ID, urpc_rsp_parse_req_id(&rsp_head));
    ASSERT_EQ((uint32_t)CHANNEL, urpc_rsp_parse_client_channel(&rsp_head));
    ASSERT_EQ((uint32_t)RET_DATA_SIZE, urpc_rsp_parse_response_total_size(&rsp_head));
}

TEST(ProtoTest, ReqEndianConversionTest) {
    urpc_req_head_t *req_head = (urpc_req_head_t *)g_req_head;
    ASSERT_EQ(VERSION, urpc_req_parse_version(req_head));
    ASSERT_EQ(TYPE, urpc_req_parse_type(req_head));
    ASSERT_EQ(ACK, urpc_req_parse_ack(req_head));
    ASSERT_EQ(DMA_CNT, urpc_req_parse_arg_dma_count(req_head));
    ASSERT_EQ((uint64_t)FUNC, urpc_req_parse_function(req_head));
    ASSERT_EQ((uint32_t)REQ_TOTAL_SIZE, urpc_req_parse_req_total_size(req_head));
    ASSERT_EQ((uint32_t)REQ_ID, urpc_req_parse_req_id(req_head));
    ASSERT_EQ((uint32_t)CHANNEL, urpc_req_parse_client_channel(req_head));
    ASSERT_EQ(FUNC_DEF, urpc_req_parse_function_defined(req_head));
}

TEST(ProtoTest, AckEndianConversionTest) {
    urpc_ack_head_t *ack_head = (urpc_ack_head_t *)g_ack_head;
    ASSERT_EQ(VERSION, urpc_ack_parse_version(ack_head));
    ASSERT_EQ(TYPE, urpc_ack_parse_type(ack_head));
    ASSERT_EQ(ID_RANGE, urpc_ack_parse_req_id_range(ack_head));
    ASSERT_EQ((uint32_t)REQ_ID, urpc_ack_parse_req_id(ack_head));
    ASSERT_EQ((uint32_t)CHANNEL, urpc_ack_parse_client_channel(ack_head));
}

TEST(ProtoTest, RspEndianConversionTest) {
    urpc_rsp_head_t *rsp_head = (urpc_rsp_head_t *)g_rsp_head;
    ASSERT_EQ(VERSION, urpc_rsp_parse_version(rsp_head));
    ASSERT_EQ(TYPE, urpc_rsp_parse_type(rsp_head));
    ASSERT_EQ(STATUS, urpc_rsp_parse_status(rsp_head));
    ASSERT_EQ(ID_RANGE, urpc_rsp_parse_req_id_range(rsp_head));
    ASSERT_EQ((uint32_t)REQ_ID, urpc_rsp_parse_req_id(rsp_head));
    ASSERT_EQ((uint32_t)CHANNEL, urpc_rsp_parse_client_channel(rsp_head));
    ASSERT_EQ((uint32_t)RET_DATA_SIZE, urpc_rsp_parse_response_total_size(rsp_head));
}

TEST(ProtoTest, KeepaliveHeadFillParseTest)
{
    urpc_keepalive_head_t *keepalive_head = (urpc_keepalive_head_t *)g_keepalive_head;
    ASSERT_EQ(keepalive_head->is_rsp, 0);
    uint8_t is_rsp = 1;
    urpc_keepalive_fill_rsp(keepalive_head, is_rsp);
    ASSERT_EQ(keepalive_head->is_rsp, is_rsp);
    urpc_keepalive_fill_rsp(keepalive_head, 0);
}