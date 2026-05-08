/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 * Description: gtest source file
 * Author: Chen Wen
 * Create: 2025-06-24
 * Note:
 * History:
 */

#ifndef BONDP_GTEST_BASIC_H
#define BONDP_GTEST_BASIC_H

#include <gtest/gtest.h>
#include "mockcpp/mokc.h"
#include "urma_gtest_basic.h"
#include "common.h"

class BondpBasicGTest : public UrmaBasicGTest {
protected:

    void SetUp() override
    {
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
    }
};

#endif