/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: urpc little endian test
 */

#include "gtest/gtest.h"

#ifdef __BYTE_ORDER
#undef __BYTE_ORDER
#endif
#define __BYTE_ORDER __BIG_ENDIAN
extern "C" {
    #include "endian_utils.h"
    #include "protocol.h"
}

TEST(ProtoTest, Little_endian_conversion_Filed16_0xff00) {
    uint16_t result;

    result = proto_filed16_put(g_ho_field16);
    ASSERT_EQ(g_no_field16, result);

    result = proto_filed16_get(g_no_field16);
    ASSERT_EQ(g_ho_field16, result);
}

TEST(ProtoTest, Little_endian_conversion_Filed24_0x00ff00ff) {
    uint32_t result;

    result = proto_filed24_put(g_ho_field24);
    ASSERT_EQ(g_no_field24 & THREE_BYTES_MASK, result);

    result = proto_filed24_get(g_no_field24);
    ASSERT_EQ(g_ho_field24 & THREE_BYTES_MASK, result);
}

TEST(ProtoTest, Little_endian_conversion_Filed32_0xff00ff00) {
    uint32_t result;

    result = proto_filed32_put(g_ho_field32);
    ASSERT_EQ(g_no_field32, result);

    result = proto_filed32_get(g_no_field32);
    ASSERT_EQ(g_ho_field32, result);
}

TEST(ProtoTest, Little_endian_conversion_Filed48_0xff00ff00ff00) {
    uint64_t result;

    result = proto_filed48_put(g_ho_field48);
    ASSERT_EQ(g_no_field48 & SIX_BYTES_MASK, result);

    result = proto_filed48_get(g_no_field48);
    ASSERT_EQ(g_ho_field48 & SIX_BYTES_MASK, result);
}

TEST(ProtoTest, Little_endian_conversion_Filed64_0xff00ff00ff00ff00) {
    uint64_t result;

    result = proto_filed64_put(g_ho_field64);
    ASSERT_EQ(g_no_field64, result);

    result = proto_filed64_get(g_no_field64);
    ASSERT_EQ(g_ho_field64, result);
}

