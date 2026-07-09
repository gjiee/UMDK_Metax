/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2026. All rights reserved.
 *
 * 文件: perftest_mem_cuda.c
 * 这是什么 (What):
 *   "NVIDIA CUDA + dma-buf" 内存后端的占位实现. 对应 RDMA perftest
 *   src/cuda_memory.c 与 src/cuda_loader.c 的合并骨架.
 *
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
/*
 * 私有上下文.
 *
 * 真实现里会有更多字段, 这里以注释列出关键的两个分支:
 *
 *   DIRECT 模式:
 *      d_ptr           : CUdeviceptr  -- GPU 设备指针, 同时也是 perftest 上层
 *                                        看到的 "buffer 地址" (alloc_buffer 出参).
 *      dmabuf_fd, off  : 由 cuMemGetHandleForAddressRange 拿到, perftest 注册
 *                        段时通过 urma_register_seg_dmabuf 透传.
 *      shadow_addr     : 不存在.
 *
 *   HOST_PROXY 模式 (无 GDR 驱动时):
 *      d_ptr           : 仍然 cuMemAlloc 出来, 模拟"应用数据在 GPU 上";
 *      shadow_addr     : cuMemAllocHost 出的 pinned host buffer, perftest 上
 *                        层看到的 "buffer 地址" (alloc_buffer 出参), URMA 注
 *                        册的就是它;
 *      stage_to_nic    : cuMemcpyDtoH(shadow_addr, d_ptr, len) 在每次发送前;
 *      stage_from_nic  : cuMemcpyHtoD(d_ptr, shadow_addr, len) 在每次接收后;
 *      dmabuf_fd       : -1 (URMA 走 host 路径).
 */
typedef struct cuda_priv {
    int   gpu_id;          /* TODO: 改为 CUdevice */
    void *cuctx_opaque;    /* TODO: 改为 CUcontext */
    int   use_dmabuf;      /* CLI 显式要求 dma-buf */
    int   driver_version;
    /*
     * 下面这几个字段 PROXY 模式下要用到, 当前是 stub, 不分配也不操作:
     */
    void *d_ptr_opaque;    /* TODO: CUdeviceptr cuMemAlloc 结果 */
    void *shadow_addr;     /* TODO: cuMemAllocHost 结果 */
    size_t buffer_size;
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
    (void)alignment;
    /*
     * 两种 GDR 模式的真实现 (二选一, self->gdr_mode 由 negotiate 决定):
     *
     * (1) PERFTEST_GDR_DIRECT (NIC 直接 DMA GPU):
     *
     *       CUdeviceptr d_A;
     *       CUCHECK(cuMemAlloc(&d_A, ALIGN_UP(size, 64*1024)));
     *
     *       size_t host_pg = sysconf(_SC_PAGESIZE);
     *       CUdeviceptr aligned = d_A & ~(host_pg - 1);
     *       uint64_t off = d_A - aligned;
     *       size_t   alen = ALIGN_UP(size + off, host_pg);
     *       int      fd;
     *       CUCHECK(cuMemGetHandleForAddressRange(
     *           &fd, aligned, alen,
     *           CU_MEM_RANGE_HANDLE_TYPE_DMA_BUF_FD, 0));
     *
     *       priv->d_ptr_opaque = (void *)d_A;
     *       *out_addr          = (void *)d_A;     // GPU 设备指针, NIC 直接 DMA
     *       *out_dmabuf_fd     = fd;
     *       *out_dmabuf_offset = off;
     *
     * (2) PERFTEST_GDR_HOST_PROXY (主机代理):
     *
     *       CUdeviceptr d_A;
     *       CUCHECK(cuMemAlloc(&d_A, size));        // 应用数据"假装"在 GPU 上
     *       void *shadow;
     *       CUCHECK(cuMemAllocHost(&shadow, size)); // pinned host shadow
     *
     *       priv->d_ptr_opaque = (void *)d_A;
     *       priv->shadow_addr  = shadow;
     *       priv->buffer_size  = size;
     *
     *       *out_addr          = shadow;          // *** URMA 注册的是 shadow ***
     *       *out_dmabuf_fd     = -1;              // host 路径, 不走 dma-buf
     *       *out_dmabuf_offset = 0;
     *
     *   PROXY 模式下:
     *     - 上层看见的 "local_buf" 就是 host shadow, 一切 register_seg /
     *       write_dirty / sentinel polling 都能照旧工作.
     *     - 每个 IO 迭代之前, 主程序应当调 stage_to_nic_buffer 把 GPU ->
     *       shadow 拷一次; 接收完成后调 stage_from_nic_buffer 把 shadow
     *       -> GPU 拷回去.
     *     - 这两次 cuMemcpy 就是 "host proxy" 相对真 GDR 的多余开销.
     */
    if (out_addr)          *out_addr = NULL;
    if (out_dmabuf_fd)     *out_dmabuf_fd = -1;
    if (out_dmabuf_offset) *out_dmabuf_offset = 0;
    fprintf(stderr,
            "[perftest-mem][cuda] alloc_buffer stub: size=%lu mode=%d (0=AUTO 1=DIRECT 2=PROXY 3=OFF)\n",
            (unsigned long)size, (int)self->gdr_mode);
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

static int cuda_negotiate(perftest_memory_ops_t *self, void *urma_dev_opaque)
{
    /*
     * 这个函数是 GDR 框架的"决策点". 真实现里:
     *
     *   1) GPU 一侧 (CUDA) 必须能导出 dma-buf:
     *        cuDeviceGetAttribute(&supported, CU_DEVICE_ATTRIBUTE_DMA_BUF_SUPPORTED, dev);
     *        if (!supported) -> can_direct = false;
     *
     *   2) URMA / NIC 一侧必须能 import dma-buf:
     *        urma_dmabuf_caps_t caps;
     *        urma_query_dmabuf_caps((urma_device_t *)urma_dev_opaque, &caps);
     *        if (!caps.support_dmabuf) -> can_direct = false;
     *
     *   3) 决定模式:
     *        - 用户显式 cfg->gdr_mode == DIRECT: 必须 can_direct, 否则 -1;
     *        - 用户显式 cfg->gdr_mode == PROXY:  无条件走 PROXY;
     *        - cfg->gdr_mode == AUTO:           can_direct ? DIRECT : PROXY.
     *
     *   4) 把结果写回 self->gdr_mode 让 alloc_buffer / stage_* 看到.
     *
     * 当前 stub 行为: 因为 urma_query_dmabuf_caps 已经返回 support=0
     * (见 urma_seg_dmabuf.c stub), 这里就模拟 AUTO -> PROXY.
     */
    (void)urma_dev_opaque;
    self->gdr_mode = PERFTEST_GDR_HOST_PROXY;
    fprintf(stderr,
            "[perftest-mem][cuda] negotiate: driver dma-buf NOT supported -> "
            "host-proxy fallback (all data staged via cuMemcpyDtoH/HtoD)\n");
    return 0;
}

static int cuda_stage_to_nic(perftest_memory_ops_t *self, void *shadow_addr,
                             void *gpu_addr, size_t len)
{
    (void)shadow_addr; (void)gpu_addr;
    /*
     * PROXY 路径下, 在每次 post send 之前调.
     *   真实现: cuMemcpyDtoH(shadow_addr, (CUdeviceptr)gpu_addr, len);
     * DIRECT 模式下, NIC 直接读 GPU, 不调这个; 但即便误调也是 noop.
     */
    if (self->gdr_mode != PERFTEST_GDR_HOST_PROXY) {
        return 0;
    }
    fprintf(stderr,
            "[perftest-mem][cuda] stage_to_nic stub: would cuMemcpyDtoH(shadow,gpu,%zu)\n",
            len);
    return 0;
}

static int cuda_stage_from_nic(perftest_memory_ops_t *self, void *gpu_addr,
                               void *shadow_addr, size_t len)
{
    (void)gpu_addr; (void)shadow_addr;
    /*
     * PROXY 路径下, 在每次 recv 完成后调.
     *   真实现: cuMemcpyHtoD((CUdeviceptr)gpu_addr, shadow_addr, len);
     */
    if (self->gdr_mode != PERFTEST_GDR_HOST_PROXY) {
        return 0;
    }
    fprintf(stderr,
            "[perftest-mem][cuda] stage_from_nic stub: would cuMemcpyHtoD(gpu,shadow,%zu)\n",
            len);
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
    ops->gdr_mode              = PERFTEST_GDR_AUTO;   /* 等 negotiate 改写 */
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
    ops->negotiate_gdr_mode    = cuda_negotiate;
    ops->stage_to_nic_buffer   = cuda_stage_to_nic;
    ops->stage_from_nic_buffer = cuda_stage_from_nic;
    fprintf(stderr, "[perftest-mem][cuda] create: stub ok\n");
    return ops;
}
