/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2026. All rights reserved.
 *
 * 文件: perftest_validation.c
 *
 * 这是什么 (What):
 *   perftest_validation.h 的实现. 用最简单的 xorshift32 PRNG 生成确定性的
 *   字节流, fill / check 用同一 seed 即可重现.
 *
 * 设计:
 *   - 不引入 libcrypto / openssl, 仅 libc;
 *   - GPU 变种 (_gpu) 内部用 perftest_cuda_loader.h 做 H2D/D2H, 若 loader
 *     未 init 直接 return -1;
 *   - 所有 print 用 __func__ 自带前缀.
 *
 * 怎么扩展:
 *   - 想换成更强的 hash 校验 (例如 CRC64) 时, 替换 fill/check 内部循环即可,
 *     接口不动.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "perftest_cuda_loader.h"
#include "perftest_validation.h"

#define V_LOG(fmt, ...) \
    fprintf(stderr, "[validation] %s: " fmt "\n", __func__, ##__VA_ARGS__)

/*
 * 简单 xorshift32: 与 linux-rdma validation_common.c 相同的"廉价 PRNG"风格.
 * 32-bit 周期 2^32-1, 测试数据足够; 真要做密码学强度的校验, 后续可以替换.
 */
static inline uint32_t xs32_step(uint32_t *s)
{
    uint32_t x = *s ? *s : 0xA5A5A5A5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

void perftest_validation_fill_host(void *buf, size_t size, uint32_t seed)
{
    uint8_t *p = (uint8_t *)buf;
    uint32_t s = seed;
    size_t   i;

    if (buf == NULL || size == 0) {
        V_LOG("invalid arg buf=%p size=%zu", buf, size);
        return;
    }
    for (i = 0; i < size; i++) {
        p[i] = (uint8_t)(xs32_step(&s) & 0xFFu);
    }
    V_LOG("filled %zu bytes seed=0x%x", size, seed);
}

size_t perftest_validation_check_host(const void *buf, size_t size, uint32_t seed)
{
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t s = seed;
    size_t   i;
    size_t   bad = 0;

    if (buf == NULL || size == 0) {
        V_LOG("invalid arg buf=%p size=%zu", buf, size);
        return size; /* treat as 'all bad' */
    }
    for (i = 0; i < size; i++) {
        uint8_t want = (uint8_t)(xs32_step(&s) & 0xFFu);
        if (p[i] != want) {
            bad++;
        }
    }
    V_LOG("checked %zu bytes seed=0x%x mismatched=%zu", size, seed, bad);
    return bad;
}

/* ---- GPU variants ----------------------------------------------------- */
/*
 * 共用一个 host 临时 buffer, 大小受 size 限制; 真实部署里 size 可能很大
 * (BW 测试 8MB), 直接 malloc 即可. 校验路径不在 hot loop, 一次性开销.
 */

int perftest_validation_fill_gpu(void *dptr, size_t size, uint32_t seed)
{
    uint8_t *tmp;
    int      rc;

    if (!perftest_cuda_is_loaded()) {
        V_LOG("FAIL: cuda loader not initialised");
        return -1;
    }
    tmp = (uint8_t *)malloc(size);
    if (tmp == NULL) {
        V_LOG("ENOMEM tmp=%zu", size);
        return -1;
    }
    perftest_validation_fill_host(tmp, size, seed);
    rc = perftest_cuda_memcpy_h2d(dptr, tmp, size);
    free(tmp);
    if (rc != 0) {
        V_LOG("FAIL: cuMemcpyHtoD rc=%d", rc);
        return -1;
    }
    V_LOG("ok: dptr=%p size=%zu seed=0x%x", dptr, size, seed);
    return 0;
}

long perftest_validation_check_gpu(void *dptr, size_t size, uint32_t seed)
{
    uint8_t *tmp;
    size_t   bad;
    int      rc;

    if (!perftest_cuda_is_loaded()) {
        V_LOG("FAIL: cuda loader not initialised");
        return -1;
    }
    tmp = (uint8_t *)malloc(size);
    if (tmp == NULL) {
        V_LOG("ENOMEM tmp=%zu", size);
        return -1;
    }
    rc = perftest_cuda_memcpy_d2h(tmp, dptr, size);
    if (rc != 0) {
        free(tmp);
        V_LOG("FAIL: cuMemcpyDtoH rc=%d", rc);
        return -1;
    }
    bad = perftest_validation_check_host(tmp, size, seed);
    free(tmp);
    return (long)bad;
}
