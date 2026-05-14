/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2026. All rights reserved.
 *
 * 文件: perftest_gpu_alloc_test.c
 *
 * 这是什么 (What):
 *   urma_perftest 新增的 perftest_gpu_alloc.[ch] 的 *最小化* 单元测试.
 *   只验证: 在自由组合的"强制失败"位掩码下, 4 步回退链都能落到预期策略, 且
 *   每条 fallback 路径都能正确分配 & 释放, 不发生段错误 / 泄漏 (按 ASan 验).
 *
 *   该测试 *只依赖 libc + perftest_gpu_alloc.c*, 不需要 urma 库, 不需要内核
 *   头, 不需要 CUDA / ROCm SDK. 用同目录下 Makefile.test 编:
 *
 *     $ make -f Makefile.test
 *     $ ./perftest_gpu_alloc_test
 *
 * 为什么不放进 test/urma/ ?
 *   - test/urma/ 走 cmake + gtest + ubcore, 重依赖, 与本测试"无环境也跑"的
 *     目标冲突.
 *   - 此最小测试服务于"在 cloud agent / CI 上快速验证 read_lat_gpu /
 *     write_lat_gpu 不会因为环境差异崩溃"这一保底诉求.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "perftest_gpu_alloc.h"

/* 简洁 print 宏, 与被测代码一致用 __func__. */
#define T_LOG(fmt, ...) \
    fprintf(stderr, "[gpu-alloc-test] %s: " fmt "\n", __func__, ##__VA_ARGS__)

#define EXPECT(cond, msg) do {                                          \
    if (!(cond)) {                                                      \
        fprintf(stderr,                                                 \
            "[gpu-alloc-test] FAIL %s:%d  (%s)  %s\n",                  \
            __FILE__, __LINE__, #cond, msg);                            \
        failures++;                                                     \
    }                                                                   \
} while (0)

static int failures;

/*
 * 单条用例: 强制前 N 级失败, 期望最终落到 expected 策略.
 *   force_mask : 位 (1<<level) 置 1 表示让该级 try_alloc 强制失败
 *   expected   : 落地策略, 由 print 比对即可
 */
static void run_case(const char *label, unsigned force_mask,
                     perftest_gpu_alloc_strategy_t expected)
{
    perftest_gpu_buffer_t buf;
    int rc;

    T_LOG("--- case: %s  force_mask=0x%x  expect=%s ---",
          label, force_mask, perftest_gpu_alloc_strategy_name(expected));

    perftest_gpu_alloc_set_force_fail_mask(force_mask);
    rc = perftest_gpu_alloc_buffer(8192, 4096, &buf);

    EXPECT(rc == 0, "perftest_gpu_alloc_buffer should never fail (memalign is unconditional)");
    EXPECT(buf.strategy == expected, "landed strategy mismatches expected");
    EXPECT(buf.addr != NULL, "addr must be non-NULL");
    EXPECT(buf.size == 8192, "size must be carried through");

    /* 简单地写一字节, 证明返回的 addr 可写 (host 可见). */
    if (buf.addr) {
        ((unsigned char *)buf.addr)[0] = 0x7F;
        EXPECT(((unsigned char *)buf.addr)[0] == 0x7F, "addr should be writable");
    }

    perftest_gpu_free_buffer(&buf);
    EXPECT(buf.addr == NULL, "after free, struct should be zeroed");
    EXPECT(buf.strategy == PERFTEST_GPU_ALLOC_NONE, "strategy zeroed too");
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    T_LOG("starting minimal fallback-chain unit test (libc only)");

    /*
     * 在 cloud agent 上 GDR 与 peermem 都不可用, host-proxy 默认也不可用
     * (没有 libcuda.so.1), 所以"不强制失败"应当落到 MEMALIGN.
     */
    run_case("env-natural (no GDR, no peermem, no libcuda)",
             0,
             PERFTEST_GPU_ALLOC_MEMALIGN);

    /*
     * 解锁 host-proxy 的 simulation 模式, GDR / peermem 仍然不可用 ->
     * 期望落 HOST_PROXY.
     */
    setenv("PERFTEST_GPU_ALLOC_ALLOW_HOST_PROXY_SIM", "1", 1);
    run_case("host-proxy simulation enabled",
             0,
             PERFTEST_GPU_ALLOC_HOST_PROXY);

    /*
     * 强制 host-proxy 也失败 (即使 sim 开着), 回到 MEMALIGN.
     */
    run_case("force host-proxy fail -> memalign",
             1u << PERFTEST_GPU_ALLOC_HOST_PROXY,
             PERFTEST_GPU_ALLOC_MEMALIGN);

    /*
     * 同时强制 host-proxy + 假装 GDR 与 peermem 已失败 (本来就失败),
     * 仍然能落 MEMALIGN. 这一组验证"最坏情况依然成功".
     */
    run_case("force all three GPU levels -> memalign",
             (1u << PERFTEST_GPU_ALLOC_GDR)        |
             (1u << PERFTEST_GPU_ALLOC_PEERMEM)    |
             (1u << PERFTEST_GPU_ALLOC_HOST_PROXY),
             PERFTEST_GPU_ALLOC_MEMALIGN);

    /*
     * 清掩码 + 再跑 sim 一次, 验证 set_force_fail_mask(0) 真的重置.
     */
    perftest_gpu_alloc_set_force_fail_mask(0);
    run_case("clear mask -> back to HOST_PROXY (sim still on)",
             0,
             PERFTEST_GPU_ALLOC_HOST_PROXY);

    if (failures == 0) {
        fprintf(stderr, "\n[gpu-alloc-test] ALL CASES PASSED\n");
        return 0;
    }
    fprintf(stderr, "\n[gpu-alloc-test] %d FAILURE(S)\n", failures);
    return 1;
}
