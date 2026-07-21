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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 新增: --gpu-mode CLI 选项的语义.
 *
 *   NONE    -- 默认值, 不加 --gpu-mode 时使用. 直接走 memalign, 完全不
 *              touch GPU / dma-buf / peermem / libcuda. 与原 read_lat /
 *              write_lat 行为一致.
 *   PROXY   -- 主机分配 shadow (pinned host) + GPU 分配 device buffer,
 *              URMA 注册 shadow, 每次 IO 之前/之后通过 cuMemcpyDtoH/HtoD
 *              做 staging (linux-rdma 风格 "host proxy"). 不需要内核
 *              GDR / peermem 驱动. cfg->gpu_mode 为该值时, register_mem
 *              内部调 perftest_gpu_alloc_buffer 即可.
 *   PEERMEM -- 直接 cuMemAlloc, 把 GPU 设备指针交给 URMA register_seg;
 *              内核侧需要 nvidia_peermem.ko (或 amd_peermem) 已 modprobe,
 *              ubcore 通过 peer_memory_client 拿 GPU 页表.
 *   DIRECT  -- cuMemAlloc + cuMemGetHandleForAddressRange(DMA_BUF_FD),
 *              交给 urma_register_seg_dmabuf. 需要 CUDA Driver >= 12.2 与
 *              ubcore + udma 已支持 dma_buf_attach.
 *
 *   每个值对应 perftest_gpu_alloc_strategy_t 中一档. PROXY 选定时仍允许
 *   失败 fallback 到 memalign (libcuda 不在的情况); PEERMEM / DIRECT 同理.
 */
typedef enum perftest_gpu_mode {
    PERFTEST_GPU_MODE_NONE    = 0,
    PERFTEST_GPU_MODE_PROXY   = 1,
    PERFTEST_GPU_MODE_PEERMEM = 2,
    PERFTEST_GPU_MODE_DIRECT  = 3,
} perftest_gpu_mode_t;

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
    void                          *gpu_ptr;         /* HOST_PROXY: GPU buf */
    /*
     * PEERMEM / GDR: local_buf 是真实 GPU VA, CPU 不能直接解引用做
     * write_lat 的 flag 字节轮询/写入 (会 SIGSEGV). 这里单独申请一小段
     * host 暂存, 通过 1 字节 cuMemcpyDtoH/HtoD 与 GPU flag 同步.
     * HOST_PROXY / MEMALIGN / 原生 host 路径不分配, 保持原生态.
     */
    void                          *cpu_flag;        /* host scratch, or NULL */
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

/*
 * 选择由 CLI --gpu-mode 决定的入口策略. 在 register_mem 调
 * perftest_gpu_alloc_buffer *之前* 设置一次. 不调 = NONE = 默认 memalign.
 *
 *   NONE   : 不调 GPU 路径, 直接 memalign;
 *   PROXY  : 先试 try_alloc_host_proxy, 失败 fallback memalign;
 *   PEERMEM: 先试 try_alloc_peermem,   失败 fallback memalign;
 *   DIRECT : 先试 try_alloc_gdr,       失败 fallback memalign.
 */
void                perftest_gpu_set_mode(perftest_gpu_mode_t m);
perftest_gpu_mode_t perftest_gpu_get_mode(void);
const char         *perftest_gpu_mode_name(perftest_gpu_mode_t m);

/* 把 string ("proxy" / "peermem" / "direct") 解析成 mode.
 * 失败返回 -1, 成功返回 0, 通过 out 写值. CLI 解析使用. */
int                 perftest_gpu_mode_from_string(const char *s,
                                                  perftest_gpu_mode_t *out);

/*
 * D2H / H2D staging helpers, --gpu-mode=proxy 下用. 由 caller 在每次
 * post_jfs_wr 前 / poll_jfc 后调一次. 对 GPU mode = NONE / PEERMEM / DIRECT
 * 这条函数是 noop. 内部会判 buf->strategy.
 */
int  perftest_gpu_stage_to_nic  (struct perftest_gpu_buffer *buf, size_t len);
int  perftest_gpu_stage_from_nic(struct perftest_gpu_buffer *buf, size_t len);
int perftest_gpu_stage_from_nic_range(perftest_gpu_buffer_t *buf,
                                      size_t off, size_t len);
int perftest_gpu_stage_to_nic_range(perftest_gpu_buffer_t *buf,
                                    size_t off, size_t len);

/*
 * write_lat flag 辅助 (仅 PEERMEM / GDR):
 *   needs_cpu_flag: 是否必须走 host 暂存 + cuMemcpy, 不能直接 *poll_buf;
 *   flag_load/store: 用 buf->cpu_flag 做 1 字节 DtoH / HtoD.
 * 其它 strategy 返回 false; load/store 不应被调用.
 */
bool perftest_gpu_needs_cpu_flag(const perftest_gpu_buffer_t *buf);
int  perftest_gpu_flag_load(perftest_gpu_buffer_t *buf, void *gpu_flag_va,
                            char *out);
int  perftest_gpu_flag_store(perftest_gpu_buffer_t *buf, void *gpu_flag_va,
                             char val);

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
