/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2026. All rights reserved.
 *
 * 文件: perftest_mem_gpu_proxy.c
 *
 * 这是什么 (What):
 *   这是 URMA 性能测试工具 urma_perftest 的"非 GDR 环境下的 GPU 内存"后端的
 *   *最小可工作实现*. 它解决一个非常具体的问题:
 *
 *      系统上有 GPU, 但 NIC / 内核 ubcore / udma 还没有实现 dma-buf 路径
 *      (即没有 GDR 驱动), 此时如何让 urma_perftest 仍然"以 GPU 内存为应
 *      用视角"地跑起来, 而不是被迫退化为纯 host 内存?
 *
 *   答案就是 "host-proxy" 模式:
 *
 *      +-------------+   stage_to_nic       +-----------------+
 *      | GPU memory  |---(cuMemcpyDtoH)---->| host shadow buf |
 *      | (application|                      | (URMA registers |
 *      |  visible)   |<--(cuMemcpyHtoD)-----|  this region)   |
 *      +-------------+   stage_from_nic     +-----------------+
 *                                                |    ^
 *                                          NIC   |    |
 *                                          DMA   v    |
 *                                              network
 *
 *   * 应用 (perftest) 把 GPU 指针当作自己的"逻辑 buffer";
 *   * URMA 注册的是 host shadow (走老的 urma_register_seg, 不需要 GDR);
 *   * 每次发送前: cuMemcpyDtoH(shadow, gpu_buf, len) 把 GPU 数据搬到 host;
 *   * 每次接收后: cuMemcpyHtoD(gpu_buf, shadow, len) 把数据回灌 GPU.
 *
 *   性能比真正的 GDR 直传差一个 PCIe roundtrip, 但完全不依赖内核 dma-buf,
 *   兼容任何已有的 ubcore + udma. 与 NCCL 在 NET_DISABLE_GDR=1 下的 fallback
 *   通路语义一致, 与 Linux DMA 子系统的 swiotlb bounce 思路同构.
 *
 * 为什么"最小":
 *   按本轮要求, 本文件 *不依赖* CUDA / ROCm / kernel:
 *     - "GPU 内存" 用 memalign() 模拟. 真实部署中替换为 cuMemAlloc / hipMalloc;
 *     - "host pinned shadow" 用 memalign() 真分配 (无 cuMemAllocHost 同样工作,
 *        只是少了 wire-speed pinning, 但语义一致);
 *     - stage_to_nic / stage_from_nic 用 memcpy() 真搬数据. 真实部署中替换为
 *        cuMemcpyDtoH / cuMemcpyHtoD; 一行换替, 上层逻辑完全不变.
 *
 *   GDR 路径 (urma_register_seg_dmabuf) 在本后端**显式旁路**:
 *     - negotiate_gdr_mode() 直接强制 self->gdr_mode = HOST_PROXY,
 *       不调任何 dma-buf API, 完全不触碰内核;
 *     - alloc_buffer() 总是返回 dmabuf_fd = -1, 让上层 register_mem 走老的
 *       urma_register_seg() 路径.
 *
 * 给内核老兵的提示 (Note for kernel devs):
 *   - 你可以把这个后端想象成"用户态的 swiotlb": NIC 看不到 GPU 物理内存, 那
 *     就准备一段 host 内存做 bounce, 进入/退出 IO 临界区时各 memcpy 一次;
 *   - 真实 CUDA 部署里, host shadow 推荐用 cuMemAllocHost (相当于 dma_alloc_
 *     coherent, 是 pinned + 对 device 可见的), 但本骨架不需要这一层;
 *   - 这里所有 fprintf 用 __func__ 自动标注函数名, 跟内核 pr_info 风格一致,
 *     方便 strace -e write 或 grep 排错.
 *
 * 编译 (Build):
 *   随 urma_perftest 一起编 (已挂进 CMakeLists.txt),
 *   或者作为独立 demo 编: 见 perftest_gpu_proxy_demo.c 与同目录 Makefile.demo.
 *
 * 当前阶段:
 *   本文件是 *真正可工作* 的实现, 不是 stub. 每一行业务代码都附中文注释,
 *   并通过 __func__ 打印进入痕迹便于追踪.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <malloc.h>   /* memalign */

#include "perftest_memory.h"

/* ---------------------------------------------------------------------------
 * 私有数据结构
 *
 * 解释:
 *   GPU host-proxy 后端的核心是"GPU 指针 + 影子指针"成对存在, 所以 priv 里
 *   要把它们都记下来. alloc_buffer 时候同时分配, free_buffer 时候同时释放.
 *
 *   gpu_ptr  : 应用层逻辑上认为"在 GPU 上"的缓冲区. 真实部署用 cuMemAlloc,
 *              本骨架用 memalign 模拟; 上层 perftest 不直接访问它, 而是通过
 *              copy_* 与 stage_* hooks.
 *   shadow   : URMA 实际注册的那块 host buffer. 上层 perftest 通过
 *              alloc_buffer 的 out_addr 拿到的就是它.
 *   size     : 分配的字节数, free 时校验对称.
 *   gpu_id   : 占位字段, 真实 CUDA 里是 CUdevice; 这里只用来在 print 里打出.
 * --------------------------------------------------------------------------- */
typedef struct gpu_proxy_priv {
    void   *gpu_ptr;
    void   *shadow;
    size_t  size;
    int     gpu_id;
} gpu_proxy_priv_t;

#define GPU_PROXY_DEFAULT_ALIGN 4096   /* 模拟"GPU page", 64KB 起步也可以 */

/* 简洁的 log 宏: 自动带 __func__. 内核老兵可类比 pr_info_ratelimited. */
#define PROXY_LOG(fmt, ...) \
    fprintf(stderr, "[gpu-proxy] %s: " fmt "\n", __func__, ##__VA_ARGS__)

/* ===========================================================================
 * init / destroy
 *
 * 真实 CUDA 版应做的事:
 *   cuInit(0);
 *   cuDeviceGet(&dev, priv->gpu_id);
 *   cuCtxCreate(&ctx, 0, dev);
 *   cuCtxSetCurrent(ctx);
 *
 * 这里因为不引入 CUDA 头, 只做必要的字段初始化.
 * =========================================================================== */
static int gpu_proxy_init(perftest_memory_ops_t *self, perftest_config_t *cfg)
{
    gpu_proxy_priv_t *priv = (gpu_proxy_priv_t *)self->priv;

    (void)cfg;
    if (priv == NULL) {
        PROXY_LOG("priv is NULL");
        return -1;
    }
    priv->gpu_ptr = NULL;
    priv->shadow  = NULL;
    priv->size    = 0;
    /*
     * 真实环境里 gpu_id 应当来自 cfg->gpu_dev_id (CLI --use_cuda=ID).
     * 当前 cfg 还没加这个字段, 占位 0 即可.
     */
    priv->gpu_id  = 0;

    PROXY_LOG("ok (simulated GPU id=%d, no CUDA/HIP linked)", priv->gpu_id);
    return 0;
}

static void gpu_proxy_destroy(perftest_memory_ops_t *self)
{
    /*
     * destroy 与 init 严格配对, 真实 CUDA 里要 cuCtxDestroy.
     * 这里只确保没有残留的 buffer; 资源清理在 free_buffer 完成.
     */
    gpu_proxy_priv_t *priv = (gpu_proxy_priv_t *)self->priv;
    if (priv && (priv->gpu_ptr || priv->shadow)) {
        PROXY_LOG("WARN: buffer not freed before destroy (gpu=%p shadow=%p)",
                  priv->gpu_ptr, priv->shadow);
    }
    PROXY_LOG("ok");
}

/* ===========================================================================
 * negotiate_gdr_mode
 *
 * 这是 GDR 框架里的"决策点". 本后端的语义就是"我永远走 host-proxy, 不论
 * NIC 能不能 dma-buf", 所以这里 *显式旁路* GDR API:
 *   - 不调 urma_query_dmabuf_caps()
 *   - 不调 urma_register_seg_dmabuf()
 *   - 强制 self->gdr_mode = HOST_PROXY
 *
 * 这样上层即使误传 --gdr-mode=direct, 本后端也只会走代理, 行为可预期.
 * 内核完全不参与, 测试可以在无 GDR 驱动的开发机上跑.
 * =========================================================================== */
static int gpu_proxy_negotiate(perftest_memory_ops_t *self, void *urma_dev_opaque)
{
    (void)urma_dev_opaque;
    self->gdr_mode = PERFTEST_GDR_HOST_PROXY;
    PROXY_LOG("forced gdr_mode=HOST_PROXY (GDR path bypassed, kernel not involved)");
    return 0;
}

/* ===========================================================================
 * alloc_buffer / free_buffer
 *
 * 关键行为:
 *   - 同时分配 GPU 缓冲区 (这里 memalign 模拟) 与 host shadow;
 *   - out_addr 返回 *shadow*, 因为 URMA register_seg 注册的是 host 端;
 *   - out_dmabuf_fd = -1, out_dmabuf_offset = 0, 让上层走老的注册路径.
 *
 * 真实 CUDA 版应做:
 *   cuMemAlloc(&d_ptr, size);                 // GPU device memory
 *   cuMemAllocHost(&shadow, size);            // pinned host memory
 *   *out_addr = shadow;
 *   priv->gpu_ptr = (void *)d_ptr;
 *   priv->shadow  = shadow;
 *
 * 真实 ROCm 版应做:
 *   hipMalloc(&d_ptr, size);
 *   hipHostMalloc(&shadow, size, hipHostMallocDefault);
 *
 * 失败处理:
 *   任何一步失败都要回滚已分配资源, 否则容易内存泄漏.
 * =========================================================================== */
static int gpu_proxy_alloc_buffer(perftest_memory_ops_t *self,
                                  uint64_t size, uint64_t alignment,
                                  void **out_addr,
                                  int *out_dmabuf_fd,
                                  uint64_t *out_dmabuf_offset)
{
    gpu_proxy_priv_t *priv = (gpu_proxy_priv_t *)self->priv;
    size_t align;

    if (priv == NULL || out_addr == NULL || out_dmabuf_fd == NULL ||
        out_dmabuf_offset == NULL) {
        PROXY_LOG("invalid arg");
        errno = EINVAL;
        return -1;
    }

    align = alignment ? (size_t)alignment : GPU_PROXY_DEFAULT_ALIGN;

    /* 1) 分配"GPU"缓冲区 (本骨架: memalign 模拟). */
    priv->gpu_ptr = memalign(align, (size_t)size);
    if (priv->gpu_ptr == NULL) {
        PROXY_LOG("ENOMEM allocating gpu_ptr size=%lu align=%zu",
                  (unsigned long)size, align);
        errno = ENOMEM;
        return -1;
    }
    /*
     * 给"GPU 数据"打个特征值, 方便 demo 看到数据真的搬运过.
     * 真实部署不需要这步, 用户应用自己写 GPU 数据.
     */
    memset(priv->gpu_ptr, 0xA5, (size_t)size);

    /* 2) 分配 host pinned shadow. */
    priv->shadow = memalign(align, (size_t)size);
    if (priv->shadow == NULL) {
        free(priv->gpu_ptr);
        priv->gpu_ptr = NULL;
        PROXY_LOG("ENOMEM allocating shadow size=%lu", (unsigned long)size);
        errno = ENOMEM;
        return -1;
    }
    memset(priv->shadow, 0, (size_t)size);

    priv->size         = (size_t)size;
    *out_addr          = priv->shadow;   /* URMA 注册这块 host buffer */
    *out_dmabuf_fd     = -1;             /* 旁路 GDR 路径 */
    *out_dmabuf_offset = 0;

    PROXY_LOG("ok: gpu_ptr=%p shadow=%p size=%lu align=%zu dmabuf_fd=-1",
              priv->gpu_ptr, priv->shadow, (unsigned long)size, align);
    return 0;
}

static void gpu_proxy_free_buffer(perftest_memory_ops_t *self,
                                  void *addr, int dmabuf_fd, uint64_t size)
{
    gpu_proxy_priv_t *priv = (gpu_proxy_priv_t *)self->priv;

    (void)dmabuf_fd;
    /*
     * 上层传进来的 addr 就是当初 alloc_buffer 返回的 shadow. 我们也顺手把
     * 对应的 gpu_ptr 一起释放.
     */
    if (priv == NULL) {
        PROXY_LOG("priv NULL, leaking addr=%p size=%lu", addr, (unsigned long)size);
        return;
    }
    if (addr != priv->shadow) {
        PROXY_LOG("WARN: addr=%p != tracked shadow=%p (continuing)",
                  addr, priv->shadow);
    }

    PROXY_LOG("releasing gpu_ptr=%p shadow=%p size=%lu",
              priv->gpu_ptr, priv->shadow, (unsigned long)priv->size);

    free(priv->gpu_ptr); priv->gpu_ptr = NULL;
    free(priv->shadow);  priv->shadow  = NULL;
    priv->size = 0;
}

/* ===========================================================================
 * copy_host_to_buffer / copy_buffer_to_host / copy_buffer_to_buffer
 *
 * "buffer" 在本后端 = host shadow (因为 URMA 注册的就是 shadow).
 * 因此这三个 hook 实际上就是普通 memcpy, 与 host 后端完全一致.
 *
 * 真实 CUDA 版:
 *   copy_host_to_buffer  -> 走 memcpy 到 shadow (因为 shadow 是 host 端)
 *                           或者 cuMemcpyHtoD 到 GPU (如果上层希望直接 push)
 *   copy_buffer_to_host  -> 类似
 *   copy_buffer_to_buffer-> memcpy on shadow
 *
 * 为简单起见, 这里全部 memcpy on shadow. 如果上层 perftest 要校验, 数据
 * 应当先经过 stage_* 同步到 GPU, 再读 GPU.
 * =========================================================================== */
static void *gpu_proxy_copy_h2b(perftest_memory_ops_t *self,
                                void *dst, const void *src, size_t n)
{
    (void)self;
    PROXY_LOG("memcpy %zu bytes dst=%p src=%p", n, dst, src);
    return memcpy(dst, src, n);
}

static void *gpu_proxy_copy_b2h(perftest_memory_ops_t *self,
                                void *dst, const void *src, size_t n)
{
    (void)self;
    PROXY_LOG("memcpy %zu bytes dst=%p src=%p", n, dst, src);
    return memcpy(dst, src, n);
}

static void *gpu_proxy_copy_b2b(perftest_memory_ops_t *self,
                                void *dst, const void *src, size_t n)
{
    (void)self;
    PROXY_LOG("memcpy %zu bytes dst=%p src=%p", n, dst, src);
    return memcpy(dst, src, n);
}

/* ===========================================================================
 * write_byte / read_byte
 *
 * latency 测试 sentinel 字节. 在 host-proxy 模式下完全可以直接读写 shadow
 * (因为它是 host 普通内存), 不需要绕道 GPU.
 * =========================================================================== */
static void gpu_proxy_write_byte(perftest_memory_ops_t *self, void *addr,
                                 uint64_t off, uint8_t v)
{
    (void)self;
    ((volatile uint8_t *)addr)[off] = v;
}

static uint8_t gpu_proxy_read_byte(perftest_memory_ops_t *self, const void *addr,
                                   uint64_t off)
{
    (void)self;
    return ((const volatile uint8_t *)addr)[off];
}

/* ===========================================================================
 * stage_to_nic_buffer (cuMemcpyDtoH 等价)
 *
 * 调用时机: perftest 主循环里 *每次 post_jfs_wr 之前* 调一次.
 * 作用: 把 "GPU buffer" 当前内容拷贝到 "host shadow", NIC 随后 DMA shadow.
 *
 * 真实 CUDA: cuMemcpyDtoH(shadow_addr, (CUdeviceptr)gpu_addr, len)
 * 真实 ROCm: hipMemcpy(shadow_addr, gpu_addr, len, hipMemcpyDeviceToHost)
 *
 * 本骨架: 直接 memcpy. 因为"GPU buffer"也是 host memalign 出来的,
 * 这次 memcpy 字节级等价 cuMemcpyDtoH 的结果.
 * =========================================================================== */
static int gpu_proxy_stage_to_nic(perftest_memory_ops_t *self,
                                  void *shadow_addr, void *gpu_addr, size_t len)
{
    gpu_proxy_priv_t *priv = (gpu_proxy_priv_t *)self->priv;

    if (self->gdr_mode != PERFTEST_GDR_HOST_PROXY) {
        /* DIRECT 模式下 NIC 直接读 GPU, 这条 hook 应为 noop. */
        return 0;
    }
    if (priv == NULL || shadow_addr == NULL || gpu_addr == NULL || len == 0) {
        PROXY_LOG("invalid arg shadow=%p gpu=%p len=%zu",
                  shadow_addr, gpu_addr, len);
        return -1;
    }
    if (len > priv->size) {
        PROXY_LOG("len=%zu exceeds allocated size=%zu", len, priv->size);
        return -1;
    }
    /*
     * Note: 真 CUDA 在这里要做 cuStreamSynchronize 才能确保前序 kernel 写完.
     * 主机模拟没有 stream, memcpy 立即同步.
     */
    memcpy(shadow_addr, gpu_addr, len);
    PROXY_LOG("staged %zu bytes  gpu(%p) -> shadow(%p) [simulating cuMemcpyDtoH]",
              len, gpu_addr, shadow_addr);
    return 0;
}

/* ===========================================================================
 * stage_from_nic_buffer (cuMemcpyHtoD 等价)
 *
 * 调用时机: perftest 主循环里 *每次 recv CQE 处理完之后* 调一次.
 * 作用: 把 "NIC 写到 host shadow" 的数据回灌到 GPU.
 *
 * 真实 CUDA: cuMemcpyHtoD((CUdeviceptr)gpu_addr, shadow_addr, len)
 * 真实 ROCm: hipMemcpy(gpu_addr, shadow_addr, len, hipMemcpyHostToDevice)
 * =========================================================================== */
static int gpu_proxy_stage_from_nic(perftest_memory_ops_t *self,
                                    void *gpu_addr, void *shadow_addr, size_t len)
{
    gpu_proxy_priv_t *priv = (gpu_proxy_priv_t *)self->priv;

    if (self->gdr_mode != PERFTEST_GDR_HOST_PROXY) {
        return 0;
    }
    if (priv == NULL || shadow_addr == NULL || gpu_addr == NULL || len == 0) {
        PROXY_LOG("invalid arg gpu=%p shadow=%p len=%zu",
                  gpu_addr, shadow_addr, len);
        return -1;
    }
    if (len > priv->size) {
        PROXY_LOG("len=%zu exceeds allocated size=%zu", len, priv->size);
        return -1;
    }
    memcpy(gpu_addr, shadow_addr, len);
    PROXY_LOG("staged %zu bytes  shadow(%p) -> gpu(%p) [simulating cuMemcpyHtoD]",
              len, shadow_addr, gpu_addr);
    return 0;
}

/* ===========================================================================
 * 对外访问器: 让 demo / perftest 可以拿到内部的 GPU 指针.
 *
 * 真实 CUDA 部署用不到这个, 因为应用层自己持有 CUdeviceptr. 这里仅为 demo
 * 能直接初始化 / 校验 "GPU 数据".
 * =========================================================================== */
void *perftest_gpu_proxy_get_gpu_ptr(perftest_memory_ops_t *self)
{
    gpu_proxy_priv_t *priv;
    if (self == NULL) {
        return NULL;
    }
    priv = (gpu_proxy_priv_t *)self->priv;
    return priv ? priv->gpu_ptr : NULL;
}

size_t perftest_gpu_proxy_get_size(perftest_memory_ops_t *self)
{
    gpu_proxy_priv_t *priv;
    if (self == NULL) {
        return 0;
    }
    priv = (gpu_proxy_priv_t *)self->priv;
    return priv ? priv->size : 0;
}

/* ===========================================================================
 * 工厂函数
 *
 * 这是上层唯一需要知道的入口. 主程序里:
 *
 *     perftest_memory_ops_t *ops = perftest_gpu_proxy_create(cfg);
 *     ops->init(ops, cfg);
 *     ops->negotiate_gdr_mode(ops, urma_dev);   // 强制 HOST_PROXY
 *     ops->alloc_buffer(ops, size, 4096, &shadow, &fd, &off);
 *     ... 进入 IO 主循环 ...
 *
 * 名字与 perftest_cuda_mem_create 平级, 但本后端不依赖 CUDA. 真实 CUDA 后端
 * 上线后, 可以让 perftest_cuda_mem_create() 内部根据 negotiate 决定挂哪一套
 * hook -- DIRECT 用真 CUDA stream + dma-buf, PROXY 走 gpu_proxy_* 这套.
 * =========================================================================== */
perftest_memory_ops_t *perftest_gpu_proxy_create(perftest_config_t *cfg)
{
    perftest_memory_ops_t *ops;
    gpu_proxy_priv_t *priv;

    (void)cfg;
    ops = (perftest_memory_ops_t *)calloc(1, sizeof(*ops));
    if (ops == NULL) {
        PROXY_LOG("ENOMEM allocating ops");
        return NULL;
    }
    priv = (gpu_proxy_priv_t *)calloc(1, sizeof(*priv));
    if (priv == NULL) {
        PROXY_LOG("ENOMEM allocating priv");
        free(ops);
        return NULL;
    }

    ops->type                  = PERFTEST_MEM_CUDA; /* 复用 CUDA 类型 */
    ops->gdr_mode              = PERFTEST_GDR_HOST_PROXY;
    ops->priv                  = priv;
    ops->init                  = gpu_proxy_init;
    ops->destroy               = gpu_proxy_destroy;
    ops->alloc_buffer          = gpu_proxy_alloc_buffer;
    ops->free_buffer           = gpu_proxy_free_buffer;
    ops->copy_host_to_buffer   = gpu_proxy_copy_h2b;
    ops->copy_buffer_to_host   = gpu_proxy_copy_b2h;
    ops->copy_buffer_to_buffer = gpu_proxy_copy_b2b;
    ops->write_byte            = gpu_proxy_write_byte;
    ops->read_byte             = gpu_proxy_read_byte;
    ops->negotiate_gdr_mode    = gpu_proxy_negotiate;
    ops->stage_to_nic_buffer   = gpu_proxy_stage_to_nic;
    ops->stage_from_nic_buffer = gpu_proxy_stage_from_nic;

    PROXY_LOG("ok (no-CUDA, no-kernel, host-proxy only)");
    return ops;
}
