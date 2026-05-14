/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2026. All rights reserved.
 *
 * 文件: perftest_validation.h
 *
 * 这是什么 (What):
 *   urma_perftest 的"数据正确性校验"工具集. 对标 linux-rdma 项目里:
 *
 *     src/validation_common.[ch]
 *     src/host_memory.c::host_validation_init / *_start / *_stop
 *     src/cuda_memory.c::cuda_validation_init / *_start / *_stop
 *
 *   它跟性能本身无关 -- 提供一个低成本的"buffer 里的字节是否就是发端写入
 *   的字节"判断, 用来在加新 GPU 路径 (proxy / peermem / direct) 时一眼看
 *   出"网络/缓存/对齐"链路有没有被改坏.
 *
 * 怎么用 (How):
 *   填充端 (一般是 sender 或 register_mem 完成后):
 *       perftest_validation_fill_host(buf, size, seed);                // host
 *       perftest_validation_fill_gpu (dptr, size, seed);                // device
 *
 *   校验端 (一般是 receiver 拿到 buf 之后):
 *       int bad = perftest_validation_check_host(buf, size, seed);     // host
 *       int bad = perftest_validation_check_gpu (dptr, size, seed);    // device
 *       if (bad)  fprintf(stderr, "validation FAIL %d bytes\n", bad);
 *
 * GPU 变种 (_gpu):
 *   - 需要 perftest_cuda_loader 已初始化 + 已有 ctx;
 *   - fill_gpu 先 host 上生成 pattern, 再 cuMemcpyHtoD;
 *   - check_gpu 先 cuMemcpyDtoH, 再 host 上比对.
 *   - 这与 linux-rdma cuda_memory.c::cuda_memory_copy_host_buffer 思路同构.
 *
 * 给内核老兵的提示:
 *   - pattern 用 xorshift32 生成, 单字节 modulo 256, 不需要 PRNG 库;
 *   - check_* 返回值是 *不匹配字节数*, 0 = PASS, 与 memcmp 相反, 方便统计.
 */

#ifndef PERFTEST_VALIDATION_H
#define PERFTEST_VALIDATION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Host memory ----------------------------------------------------------- */
void   perftest_validation_fill_host (void *buf,           size_t size, uint32_t seed);
size_t perftest_validation_check_host(const void *buf,     size_t size, uint32_t seed);

/* Device memory (CUDA) -------------------------------------------------- */
/*
 * 这两个内部走 perftest_cuda_loader 的 H2D / D2H, 失败 (loader 未 init /
 * cuMemcpy 错) 返回 -1; 成功时 check_gpu 返回不匹配字节数 (0 = PASS).
 */
int    perftest_validation_fill_gpu (void *dptr,           size_t size, uint32_t seed);
long   perftest_validation_check_gpu(void *dptr,           size_t size, uint32_t seed);

#ifdef __cplusplus
}
#endif

#endif /* PERFTEST_VALIDATION_H */
