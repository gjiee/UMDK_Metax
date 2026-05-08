/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 * File Name     : example_common.h
 * Description   : example common functions
 * History       : create file & add functions
 * 1.Date        : 2021-06-15
 * Author        : wangyue
 * Modification  : Created file
 */

#include <sys/socket.h>
#include <linux/limits.h>
#include <arpa/inet.h>
#include <climits>
#include <cstdio>

#include "dlock_types.h"

using namespace dlock;

#define IP_CHAR_MAX_LEN 16
#define URMA_EID_STR_MIN_LEN 3
#define URMA_IPV4_MAP_IPV6_PREFIX 0x0000ffff

void u32_to_eid(uint32_t ipv4, dlock_eid_t *eid)
{
    eid->in4.reserved = 0;
    eid->in4.prefix = htobe32(URMA_IPV4_MAP_IPV6_PREFIX);
    eid->in4.addr = htobe32(ipv4);
}

int str_to_urma_eid(const char *buf, dlock_eid_t *eid)
{
    unsigned long ret;
    uint32_t ipv4;

    if (buf == nullptr || strnlen(buf, IP_CHAR_MAX_LEN) <= URMA_EID_STR_MIN_LEN || eid == nullptr) {
        printf("Invalid argument.\n");
        return -1;
    }

    // ipv6 addr or eid
    if (inet_pton(AF_INET6, buf, eid) > 0) {
        return 0;
    }

    // ipv4 addr: xx.xx.xx.xx
    if (inet_pton(AF_INET, buf, &ipv4) > 0) {
        u32_to_eid(be32toh(ipv4), eid);
        return 0;
    }

    // ipv4 value: 0x12345  or abcdef or 12345
    ret = strtoul(buf, nullptr, 0);
    if (ret > 0u && ret != ULONG_MAX) {
        u32_to_eid(static_cast<uint32_t>(ret), eid);
        return 0;
    }

    printf("format error: %s", buf);
    return -1;
}
