/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2026. All rights reserved.
 *
 * 文件: perftest_gpu_alloc.h
 *
 * 这是什么 (What):
 *   urma_perftest 在新增的 read_lat_gpu / write_lat_gpu 测试命令下使用的
 *   "GPU 内存分配 + 自动回退" API. 上层 register_mem() 调一次就拿到一段
 *   "尽量接近 GPU 内存语义"的缓冲区, 内部依次尝试:
 *
 *     1. GDR (dma-buf 直传)        -- 需要 NIC 内核 ubcore 支持 dma_buf_attach
 *     2. peermem (nvidia-peermem)  -- 需要 nvidia_peermem.ko 与 cuMemAlloc
 *     3. host-proxy                -- 需要 GPU 用户态运行时, NIC 不需要 GDR
 *     4. memalign 兜底             -- 完全主机内存, 永远成功
 *
 *   任一步成功即返回, 调用方只需关心 out->addr 与 out->dmabuf_fd, 其他
 *   字段供后续 free 用. 这样 read_lat_gpu / write_lat_gpu 在任意环境
 *   (无 GDR / 无 peermem / 无 CUDA) 都能跑起来, 与原 read_lat / write_lat
 *   行为一致, 只是内存分配策略不同.
 *
 * 这对你 (内核老兵) 意味着什么:
 *   - 想象内核里 dma_alloc_attrs(): 先试 IOMMU 直通, 不行再 swiotlb,
 *     最后 fall back 普通页. 本 API 同构, 只是分层在用户态.
 *   - 每一步都通过 fprintf + __func__ 打印, 类似 dev_info_once 的诊断风格.
 *
 * 接入点:
 *   perftest_resources.c::register_mem() 在判定 cfg->cmd 为 GPU 变种时调
 *   perftest_gpu_alloc_buffer(); destroy 路径配对调 perftest_gpu_free_buffer().
 */

#ifndef PERFTEST_GPU_ALLOC_H
#define PERFTEST_GPU_ALLOC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 分配策略, 与上面的 4 步一一对应. */
typedef enum perftest_gpu_alloc_strategy {
    PERFTEST_GPU_ALLOC_NONE       = 0,   /* 还未尝试 / 失败 */
    PERFTEST_GPU_ALLOC_GDR        = 1,
    PERFTEST_GPU_ALLOC_PEERMEM    = 2,
    PERFTEST_GPU_ALLOC_HOST_PROXY = 3,
    PERFTEST_GPU_ALLOC_MEMALIGN   = 4,
} perftest_gpu_alloc_strategy_t;

/*
 * 分配结果. 上层只需把 .addr 喂给 urma_register_seg() (再加 dmabuf_fd 走
 * urma_register_seg_dmabuf 时), 其它字段对 caller 透明.
 */
typedef struct perftest_gpu_buffer {
    void                          *addr;            /* va to register_seg */
    size_t                         size;
    int                            dmabuf_fd;       /* >=0 only for GDR */
    uint64_t                       dmabuf_offset;
    void                          *gpu_ptr;         /* HOST_PROXY: simulated GPU buf */
    perftest_gpu_alloc_strategy_t  strategy;
} perftest_gpu_buffer_t;

/*
 * 分配 size 字节的"GPU 内存", alignment 控制起始地址对齐 (>= 4096).
 * 失败 (4 步都没成功, 极少发生; 主要是 OOM) 返回 -1, 否则返回 0 且
 * out 内容有效.
 */
int  perftest_gpu_alloc_buffer(size_t size, size_t alignment,
                               perftest_gpu_buffer_t *out);
void perftest_gpu_free_buffer(perftest_gpu_buffer_t *buf);

/* 给 print / 报告用的策略名. */
const char *perftest_gpu_alloc_strategy_name(perftest_gpu_alloc_strategy_t s);

/*
 * 调试用强制失败位掩码 (按 enum 值 1<<n). 默认 0 = 不强制. 测试代码用它
 * 验证每一级 fallback 都被正确触发.
 *
 *   bit 1 = force GDR        fail
 *   bit 2 = force PEERMEM    fail
 *   bit 3 = force HOST_PROXY fail
 *
 * MEMALIGN 不能 force-fail, 它是最后的保命路径.
 */
void perftest_gpu_alloc_set_force_fail_mask(unsigned mask);

#ifdef __cplusplus
}
#endif

#endif /* PERFTEST_GPU_ALLOC_H */
