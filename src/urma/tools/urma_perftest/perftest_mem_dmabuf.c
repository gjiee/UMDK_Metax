/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2026. All rights reserved.
 *
 * 文件: perftest_mem_dmabuf.c
 * 这是什么 (What):
 *   "通用 dma-buf 直传"后端. 对应 RDMA perftest --use_dmabuf 但显式接收
 *   --dmabuf-fd <fd> --dmabuf-offset <off>. 用于:
 *     - 第三方加速器 (Cambricon / Habana / 自研 NPU) 已经在外部分配并导出 fd;
 *     - 与 NCCL / PyTorch 等用户共享同一段 dma-buf;
 *     - CI 里用 udmabuf (drivers/dma-buf/udmabuf.c) 模拟 GDR.
 *
 * 怎么实现 (How):
 *   - alloc_buffer: 不分配, 只把 cfg->dmabuf_fd / cfg->dmabuf_offset 透传出来.
 *     缓冲区地址可以用 mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)
 *     拿到一个 host-visible 视图供 CPU 调试用 (注意有些 dma-buf exporter 不
 *     支持 mmap).
 *   - free_buffer: munmap + close(fd).
 *
 * 给内核老兵的提示:
 *   你完全可以把它当成内核里的 "外部 dma-buf 路径": 用户态拿到 fd,
 *   内核 dma_buf_get(fd) 之后是熟悉的领域.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "perftest_memory.h"

typedef struct dmabuf_priv {
    int      ext_fd;       /* TODO: 由 perftest_config_t 透传 */
    uint64_t ext_offset;
    void    *mmap_addr;
    uint64_t mmap_size;
} dmabuf_priv_t;

static int dmabuf_init(perftest_memory_ops_t *self, perftest_config_t *cfg)
{
    (void)self; (void)cfg;
    /*
     * TODO 真实现:
     *   priv->ext_fd     = cfg->dmabuf_fd;
     *   priv->ext_offset = cfg->dmabuf_offset;
     *   if (priv->ext_fd < 0) return -1;
     */
    fprintf(stderr, "[perftest-mem][dmabuf] init: stub ok\n");
    return 0;
}

static void dmabuf_destroy(perftest_memory_ops_t *self)
{
    (void)self;
    /* TODO: free(priv) */
    fprintf(stderr, "[perftest-mem][dmabuf] destroy: stub ok\n");
}

static int dmabuf_alloc_buffer(perftest_memory_ops_t *self,
                               uint64_t size, uint64_t alignment,
                               void **out_addr,
                               int *out_dmabuf_fd,
                               uint64_t *out_dmabuf_offset)
{
    (void)self; (void)size; (void)alignment;
    /*
     * TODO 真实现:
     *   void *p = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED,
     *                  priv->ext_fd, priv->ext_offset);
     *   if (p == MAP_FAILED) p = NULL; // 某些 exporter 不支持 mmap, 那就只跑设备侧
     *   *out_addr          = p;
     *   *out_dmabuf_fd     = priv->ext_fd;
     *   *out_dmabuf_offset = priv->ext_offset;
     */
    if (out_addr)          *out_addr = NULL;
    if (out_dmabuf_fd)     *out_dmabuf_fd = -1;
    if (out_dmabuf_offset) *out_dmabuf_offset = 0;
    fprintf(stderr, "[perftest-mem][dmabuf] alloc_buffer: stub ok\n");
    return 0;
}

static void dmabuf_free_buffer(perftest_memory_ops_t *self,
                               void *addr, int dmabuf_fd, uint64_t size)
{
    (void)self; (void)addr; (void)dmabuf_fd; (void)size;
    /* TODO: if (addr) munmap(addr, size); 不主动 close(fd), 由 caller 决定 */
    fprintf(stderr, "[perftest-mem][dmabuf] free_buffer: stub ok\n");
}

static void *dmabuf_copy_h2b(perftest_memory_ops_t *self,
                             void *dst, const void *src, size_t n)
{
    (void)self; (void)src;
    /* 仅当 dma-buf exporter 支持 mmap 时才能 memcpy, 否则放弃数据校验. */
    fprintf(stderr, "[perftest-mem][dmabuf] copy_h2b stub: %zu bytes\n", n);
    return dst;
}

static void *dmabuf_copy_b2h(perftest_memory_ops_t *self,
                             void *dst, const void *src, size_t n)
{
    (void)self; (void)src;
    fprintf(stderr, "[perftest-mem][dmabuf] copy_b2h stub: %zu bytes\n", n);
    return dst;
}

static void *dmabuf_copy_b2b(perftest_memory_ops_t *self,
                             void *dst, const void *src, size_t n)
{
    (void)self; (void)src;
    fprintf(stderr, "[perftest-mem][dmabuf] copy_b2b stub: %zu bytes\n", n);
    return dst;
}

static void dmabuf_write_byte(perftest_memory_ops_t *self, void *addr,
                              uint64_t off, uint8_t v)
{
    (void)self; (void)addr; (void)off; (void)v;
    fprintf(stderr, "[perftest-mem][dmabuf] write_byte stub off=%lu v=%u\n",
            (unsigned long)off, (unsigned)v);
}

static uint8_t dmabuf_read_byte(perftest_memory_ops_t *self, const void *addr,
                                uint64_t off)
{
    (void)self; (void)addr; (void)off;
    fprintf(stderr, "[perftest-mem][dmabuf] read_byte stub off=%lu -> 0\n",
            (unsigned long)off);
    return 0;
}

perftest_memory_ops_t *perftest_dmabuf_mem_create(perftest_config_t *cfg)
{
    (void)cfg;
    perftest_memory_ops_t *ops = (perftest_memory_ops_t *)calloc(1, sizeof(*ops));
    if (!ops) {
        fprintf(stderr, "[perftest-mem][dmabuf] create: oom\n");
        return NULL;
    }
    dmabuf_priv_t *priv = (dmabuf_priv_t *)calloc(1, sizeof(*priv));
    if (!priv) {
        free(ops);
        return NULL;
    }
    priv->ext_fd = -1;
    ops->type                  = PERFTEST_MEM_DMABUF;
    ops->priv                  = priv;
    ops->init                  = dmabuf_init;
    ops->destroy               = dmabuf_destroy;
    ops->alloc_buffer          = dmabuf_alloc_buffer;
    ops->free_buffer           = dmabuf_free_buffer;
    ops->copy_host_to_buffer   = dmabuf_copy_h2b;
    ops->copy_buffer_to_host   = dmabuf_copy_b2h;
    ops->copy_buffer_to_buffer = dmabuf_copy_b2b;
    ops->write_byte            = dmabuf_write_byte;
    ops->read_byte             = dmabuf_read_byte;
    fprintf(stderr, "[perftest-mem][dmabuf] create: stub ok\n");
    return ops;
}

/*
 * 顶层分发器: 依据 cfg->mem_type 调对应工厂. 当前没法从 cfg 里读到字段
 * (那是 ABI 改动, 见 docs/urma_gdr_analysis.md §5.4), 所以默认走 host.
 *
 * 真实现里:
 *     switch (cfg->mem_type) {
 *     case PERFTEST_MEM_HOST:   return perftest_host_mem_create(cfg);
 *     case PERFTEST_MEM_CUDA:   return perftest_cuda_mem_create(cfg);
 *     case PERFTEST_MEM_ROCM:   return perftest_rocm_mem_create(cfg);
 *     case PERFTEST_MEM_DMABUF: return perftest_dmabuf_mem_create(cfg);
 *     default: return NULL;
 *     }
 */
perftest_memory_ops_t *perftest_memory_create(perftest_config_t *cfg)
{
    fprintf(stderr,
        "[perftest-mem] create dispatcher: stub -> host (cfg->mem_type unused)\n");
    return perftest_host_mem_create(cfg);
}

void perftest_memory_destroy(perftest_memory_ops_t *ops)
{
    if (!ops) return;
    if (ops->destroy) ops->destroy(ops);
    if (ops->priv)    free(ops->priv);
    free(ops);
}
