/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2026. All rights reserved.
 *
 * 文件: perftest_mem_cuda.c
 * 这是什么 (What):
 *   "NVIDIA CUDA + dma-buf" 内存后端的占位实现. 对应 RDMA perftest
 *   src/cuda_memory.c 与 src/cuda_loader.c 的合并骨架.
 *
 * 怎么实现 (How, 给内核老兵):
 *   传统 RDMA over GPU 有两条路, URMA 走 dma-buf 这条:
 *
 *     用户态:
 *        cuInit(0)
 *        cuDeviceGet(&dev, id)
 *        cuCtxCreate(&ctx, ...)
 *        cuMemAlloc(&d_ptr, size)
 *        cuMemGetHandleForAddressRange(&fd, d_ptr, size,
 *               CU_MEM_RANGE_HANDLE_TYPE_DMA_BUF_FD, 0)
 *        urma_register_seg_dmabuf(urma_ctx, &seg_cfg, fd, offset)
 *
 *     内核态 (uburma + ubcore):
 *        在 ioctl 入口拿到 fd ->
 *        dmabuf = dma_buf_get(fd)
 *        attach = dma_buf_attach(dmabuf, &nic->dev)
 *        sgt    = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL)
 *        以 sgt 而非 pin_user_pages 的页表填 NIC MAPT.
 *
 *   你以前在 RDMA peermem 上熟悉的 nvidia_p2p_get_pages 那条路, URMA 不
 *   计划再走第二遍. dma-buf 是上游内核维护的标准 DMA buffer 共享机制.
 *
 * 当前阶段:
 *   - 不 dlopen libcuda; 没有 CUDA SDK 也要能编过.
 *   - 所有钩子都是 stub, 假装"调成功了".
 *   - 真实现会改用 weak symbol 或 dlopen 让构建系统在没 CUDA 时也能链接.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "perftest_memory.h"

/*
 * 私有上下文.
 *
 * 真实现里会有:
 *     CUdevice      dev;
 *     CUcontext     cuctx;
 *     int           gpu_id;
 *     bool          use_dmabuf;
 *     int           driver_version;
 *
 * 现阶段为了不引入 cuda.h, 全部以 void * 占位 + 注释说明.
 */
typedef struct cuda_priv {
    int   gpu_id;          /* TODO: 改为 CUdevice */
    void *cuctx_opaque;    /* TODO: 改为 CUcontext */
    int   use_dmabuf;
    int   driver_version;
} cuda_priv_t;

static int cuda_init(perftest_memory_ops_t *self, perftest_config_t *cfg)
{
    (void)self; (void)cfg;
    /*
     * TODO 真实现:
     *   if (load_cuda_library() != 0) return -1;
     *   CUCHECK(cuInit(0));
     *   CUCHECK(cuDeviceGet(&priv->dev, priv->gpu_id));
     *   CUCHECK(cuCtxCreate(&priv->cuctx, CU_CTX_MAP_HOST, priv->dev));
     *   CUCHECK(cuCtxSetCurrent(priv->cuctx));
     *   priv->use_dmabuf = cfg->use_dmabuf;
     *   cuDriverGetVersion(&priv->driver_version);
     */
    fprintf(stderr, "[perftest-mem][cuda] init: stub ok (no real cuInit)\n");
    return 0;
}

static void cuda_destroy(perftest_memory_ops_t *self)
{
    (void)self;
    /* TODO: cuCtxDestroy(priv->cuctx); free(priv); */
    fprintf(stderr, "[perftest-mem][cuda] destroy: stub ok\n");
}

static int cuda_alloc_buffer(perftest_memory_ops_t *self,
                             uint64_t size, uint64_t alignment,
                             void **out_addr,
                             int *out_dmabuf_fd,
                             uint64_t *out_dmabuf_offset)
{
    (void)self; (void)size; (void)alignment;
    /*
     * TODO 真实现 (与 RDMA perftest cuda_allocate_device_memory_buffer 等价):
     *
     *   CUdeviceptr d_A;
     *   CUCHECK(cuMemAlloc(&d_A, ALIGN_UP(size, 64*1024)));
     *
     *   if (priv->use_dmabuf) {
     *       size_t host_pg = sysconf(_SC_PAGESIZE);
     *       CUdeviceptr aligned = d_A & ~(host_pg - 1);
     *       uint64_t offset = d_A - aligned;
     *       size_t  asize = ALIGN_UP(size + offset, host_pg);
     *       int fd;
     *       CUCHECK(cuMemGetHandleForAddressRange(
     *           &fd, aligned, asize,
     *           CU_MEM_RANGE_HANDLE_TYPE_DMA_BUF_FD, 0));
     *       *out_dmabuf_fd     = fd;
     *       *out_dmabuf_offset = offset;
     *   } else {
     *       *out_dmabuf_fd     = -1;
     *       *out_dmabuf_offset = 0;
     *   }
     *   *out_addr = (void *)d_A;
     *   return 0;
     */
    if (out_addr)          *out_addr = NULL;
    if (out_dmabuf_fd)     *out_dmabuf_fd = -1;     /* 装作主机内存路径走通 */
    if (out_dmabuf_offset) *out_dmabuf_offset = 0;
    fprintf(stderr, "[perftest-mem][cuda] alloc_buffer: stub ok (size=%lu)\n",
            (unsigned long)size);
    return 0;
}

static void cuda_free_buffer(perftest_memory_ops_t *self,
                             void *addr, int dmabuf_fd, uint64_t size)
{
    (void)self; (void)addr; (void)dmabuf_fd; (void)size;
    /*
     * TODO 真实现:
     *   if (dmabuf_fd >= 0) close(dmabuf_fd);
     *   cuMemFree((CUdeviceptr)addr);
     */
    fprintf(stderr, "[perftest-mem][cuda] free_buffer: stub ok\n");
}

/*
 * 三个 copy_* 在真实现里就是:
 *   cuMemcpyHtoD / cuMemcpyDtoH / cuMemcpyDtoD
 * 加错误码转换. 现阶段只 print + 返回 dst.
 */
static void *cuda_copy_h2b(perftest_memory_ops_t *self,
                           void *dst, const void *src, size_t n)
{
    (void)self; (void)src;
    fprintf(stderr, "[perftest-mem][cuda] copy_h2b stub: %zu bytes -> dev\n", n);
    return dst;
}

static void *cuda_copy_b2h(perftest_memory_ops_t *self,
                           void *dst, const void *src, size_t n)
{
    (void)self; (void)src;
    fprintf(stderr, "[perftest-mem][cuda] copy_b2h stub: %zu bytes <- dev\n", n);
    return dst;
}

static void *cuda_copy_b2b(perftest_memory_ops_t *self,
                           void *dst, const void *src, size_t n)
{
    (void)self; (void)src;
    fprintf(stderr, "[perftest-mem][cuda] copy_b2b stub: %zu bytes dev->dev\n", n);
    return dst;
}

static void cuda_write_byte(perftest_memory_ops_t *self, void *addr,
                            uint64_t off, uint8_t v)
{
    (void)self; (void)addr; (void)off; (void)v;
    /*
     * 注意: latency 测试的 sentinel 字节不能直接写到 GPU 显存!
     * 真实现会让后端在 init 时额外 cuMemAllocHost 一小块 host pinned memory,
     * 这个 host shadow 由 NIC 与 CPU 共同访问做 polling. write_byte/read_byte
     * 走 host shadow, 数据 buffer 仍走 GPU.
     */
    fprintf(stderr, "[perftest-mem][cuda] write_byte stub off=%lu v=%u\n",
            (unsigned long)off, (unsigned)v);
}

static uint8_t cuda_read_byte(perftest_memory_ops_t *self, const void *addr,
                              uint64_t off)
{
    (void)self; (void)addr; (void)off;
    fprintf(stderr, "[perftest-mem][cuda] read_byte stub off=%lu -> 0\n",
            (unsigned long)off);
    return 0;
}

perftest_memory_ops_t *perftest_cuda_mem_create(perftest_config_t *cfg)
{
    (void)cfg;
    perftest_memory_ops_t *ops = (perftest_memory_ops_t *)calloc(1, sizeof(*ops));
    if (!ops) {
        fprintf(stderr, "[perftest-mem][cuda] create: oom\n");
        return NULL;
    }
    cuda_priv_t *priv = (cuda_priv_t *)calloc(1, sizeof(*priv));
    if (!priv) {
        free(ops);
        return NULL;
    }
    /* TODO: 从 cfg 拷贝 gpu_id / use_dmabuf 等. */
    ops->type                  = PERFTEST_MEM_CUDA;
    ops->priv                  = priv;
    ops->init                  = cuda_init;
    ops->destroy               = cuda_destroy;
    ops->alloc_buffer          = cuda_alloc_buffer;
    ops->free_buffer           = cuda_free_buffer;
    ops->copy_host_to_buffer   = cuda_copy_h2b;
    ops->copy_buffer_to_host   = cuda_copy_b2h;
    ops->copy_buffer_to_buffer = cuda_copy_b2b;
    ops->write_byte            = cuda_write_byte;
    ops->read_byte             = cuda_read_byte;
    fprintf(stderr, "[perftest-mem][cuda] create: stub ok\n");
    return ops;
}
