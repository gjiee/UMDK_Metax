/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2026. All rights reserved.
 *
 * 文件: perftest_gpu_alloc.c
 *
 * 这是什么 (What):
 *   read_lat_gpu / write_lat_gpu 测试用的"GPU 内存分配 + 自动回退"实现.
 *
 *   入口 perftest_gpu_alloc_buffer() 依次尝试 4 条路径, 每一步打印自身
 *   函数名 (__func__) 与原因, 任一步成功即返回:
 *
 *      try_alloc_gdr()         尝试 dma-buf 直传 GPU. 当前骨架下
 *                              urma_register_seg_dmabuf 返回 ENOTSUP,
 *                              所以本地无 GDR 时直接失败.
 *      try_alloc_peermem()     尝试 nvidia-peermem 风格 (cuMemAlloc + NIC
 *                              通过 kernel peer-mem 看 GPU). 检测
 *                              /proc/modules 与 libcuda.so, 缺一即失败.
 *      try_alloc_host_proxy()  尝试 host-proxy: cuMemAlloc 做 GPU buffer,
 *                              memalign 做 host shadow. 没 libcuda 也失败.
 *      try_alloc_memalign()    主机 memalign, 兜底, 永远成功.
 *
 *   设计目的:
 *     - 真实环境拿到最好可用的 GPU 加速;
 *     - 没有 GDR / peermem / CUDA 也能跑测试, 不至于因为加 _gpu 后缀就退化
 *       为"该命令不可用".
 *
 * 给内核老兵的提示:
 *   - 这一层完全是用户态分配器, 不会调 ioctl. 真正与 NIC 通信仍由 URMA
 *     的 register_seg 完成 (或 GDR 路径下的 register_seg_dmabuf).
 *   - 强制失败 mask (perftest_gpu_alloc_set_force_fail_mask) 仅用于单元
 *     测试, 用法见 perftest_gpu_alloc_test.c.
 *
 * 当前阶段:
 *   - 真正的 cuMemAlloc / nvidia_peermem 探测仅做"特征检测" (dlopen +
 *     /proc/modules), 不实际链接 libcuda. 这样不依赖 CUDA SDK 也能编;
 *   - HOST_PROXY 与 MEMALIGN 不依赖任何外部库, 100% 在用户态完成.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <errno.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "perftest_gpu_alloc.h"

#define GA_LOG(fmt, ...) \
    fprintf(stderr, "[gpu-alloc] %s: " fmt "\n", __func__, ##__VA_ARGS__)

/* 强制失败位掩码, 仅供测试. 默认 0. */
static unsigned g_force_fail_mask = 0;

void perftest_gpu_alloc_set_force_fail_mask(unsigned mask)
{
    g_force_fail_mask = mask;
    GA_LOG("force_fail_mask=0x%x", mask);
}

static int forced_fail(perftest_gpu_alloc_strategy_t s)
{
    return (g_force_fail_mask >> (unsigned)s) & 0x1u;
}

const char *perftest_gpu_alloc_strategy_name(perftest_gpu_alloc_strategy_t s)
{
    switch (s) {
        case PERFTEST_GPU_ALLOC_GDR:        return "GDR";
        case PERFTEST_GPU_ALLOC_PEERMEM:    return "PEERMEM";
        case PERFTEST_GPU_ALLOC_HOST_PROXY: return "HOST_PROXY";
        case PERFTEST_GPU_ALLOC_MEMALIGN:   return "MEMALIGN";
        case PERFTEST_GPU_ALLOC_NONE:
        default:                            return "NONE";
    }
}

/* ===========================================================================
 * step 1: GDR (dma-buf direct)
 *
 * 真实路径:
 *   cuMemAlloc(&d_ptr, size);
 *   cuMemGetHandleForAddressRange(&fd, d_ptr, size, DMA_BUF_FD, 0);
 *   urma_register_seg_dmabuf(ctx, &cfg, fd, off);
 *
 * 当前骨架: urma_register_seg_dmabuf 已被显式旁路 (BYPASS), 永远返回
 * EOPNOTSUPP, 因此本步在当前部署中必然失败. 没有内核 dma-buf 改动前
 * 不会成功.
 * =========================================================================== */
static int try_alloc_gdr(size_t size, size_t alignment,
                         perftest_gpu_buffer_t *out)
{
    (void)size; (void)alignment; (void)out;

    if (forced_fail(PERFTEST_GPU_ALLOC_GDR)) {
        GA_LOG("forced fail by test mask");
        return -1;
    }

    /*
     * 真实现要点:
     *   1) dlopen("libcuda.so.1"); 取 cuMemAlloc / cuMemGetHandleForAddressRange;
     *   2) 调用 urma_register_seg_dmabuf(); 若返回非 NULL -> 填 out, ret 0;
     *
     * 当前 urma_register_seg_dmabuf 总返回 NULL+EOPNOTSUPP (见
     * urma_seg_dmabuf.c BYPASS 注释), 所以这条路径必然走不通.
     */
    GA_LOG("FAIL: kernel dma-buf path not wired "
           "(urma_register_seg_dmabuf -> EOPNOTSUPP, bypass mode)");
    return -1;
}

/* ===========================================================================
 * step 2: peermem (nvidia-peermem style)
 *
 * 真实路径:
 *   modprobe nvidia_peermem (or amd_peermem);
 *   cuMemAlloc(&d_ptr, size);
 *   urma_register_seg(ctx, {.va=d_ptr, .len=size, ...});
 *     -> 内核 ubcore 通过 peer_memory_client 注册回调 (nvidia_p2p_get_pages)
 *        拿 GPU page list.
 *
 * 当前骨架: 通过 dlopen + /proc/modules 检测. 任一缺失即失败.
 * =========================================================================== */
static int has_kernel_module(const char *name)
{
    FILE *fp = fopen("/proc/modules", "r");
    char  line[256];
    int   found = 0;
    if (fp == NULL) return 0;
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strncmp(line, name, strlen(name)) == 0 &&
            (line[strlen(name)] == ' ' || line[strlen(name)] == '\t')) {
            found = 1;
            break;
        }
    }
    fclose(fp);
    return found;
}

static int try_alloc_peermem(size_t size, size_t alignment,
                             perftest_gpu_buffer_t *out)
{
    void *libcuda = NULL;
    (void)size; (void)alignment; (void)out;

    if (forced_fail(PERFTEST_GPU_ALLOC_PEERMEM)) {
        GA_LOG("forced fail by test mask");
        return -1;
    }

    libcuda = dlopen("libcuda.so.1", RTLD_LAZY);
    if (libcuda == NULL) {
        GA_LOG("FAIL: libcuda.so.1 not present (%s)", dlerror());
        return -1;
    }
    /*
     * 即使 libcuda 在, 还要看 nvidia_peermem 内核模块. ubcore 通过
     * peer_memory_client 注册回调, 没装这个模块就算 cuMemAlloc 出来也
     * 没法被 NIC 直接 DMA.
     */
    if (!has_kernel_module("nvidia_peermem") &&
        !has_kernel_module("amd_peermem")) {
        GA_LOG("FAIL: no peer-mem kernel module loaded (nvidia_peermem / amd_peermem)");
        dlclose(libcuda);
        return -1;
    }
    /*
     * 真实现这里 cuMemAlloc + urma_register_seg(va=cu_ptr). 当前骨架
     * 没必要拉 cuda.h, 直接报"探测到但暂未实现", 让 fallback 继续走.
     */
    GA_LOG("FAIL: peer-mem detected but cuMemAlloc wiring not implemented in skeleton");
    dlclose(libcuda);
    return -1;
}

/* ===========================================================================
 * step 3: host-proxy
 *
 * 真实路径:
 *   cuMemAlloc(&d_ptr, size);             // GPU device memory
 *   posix_memalign(&shadow, align, size); // host shadow
 *   urma_register_seg(ctx, {.va=shadow ...});
 *   每次 IO 前 cuMemcpyDtoH(shadow, d_ptr, size);
 *   每次 IO 后 cuMemcpyHtoD(d_ptr, shadow, size);
 *
 * 当前骨架: 无 libcuda 即失败 (与 peermem 同样的探测). 若 libcuda 在,
 * 但本骨架不真链接, 仍然失败 -- 这样在 cloud agent 这种纯 host 环境上,
 * 第 3 步会失败, 第 4 步 memalign 成功. 测试 print 链得到完整展示.
 *
 * 若 PERFTEST_GPU_ALLOC_ALLOW_HOST_PROXY_SIM=1 环境变量被设置, 我们仍走
 * "用 memalign 模拟 GPU + 单独 shadow" 的语义, 让验证用得上 host-proxy
 * 的对端 (与上一轮 demo 行为一致).
 * =========================================================================== */
static int try_alloc_host_proxy(size_t size, size_t alignment,
                                perftest_gpu_buffer_t *out)
{
    void *libcuda;
    const char *sim;

    if (forced_fail(PERFTEST_GPU_ALLOC_HOST_PROXY)) {
        GA_LOG("forced fail by test mask");
        return -1;
    }

    libcuda = dlopen("libcuda.so.1", RTLD_LAZY);
    sim = getenv("PERFTEST_GPU_ALLOC_ALLOW_HOST_PROXY_SIM");

    if (libcuda == NULL && (sim == NULL || strcmp(sim, "1") != 0)) {
        GA_LOG("FAIL: libcuda.so.1 not present and PERFTEST_GPU_ALLOC_ALLOW_HOST_PROXY_SIM!=1");
        return -1;
    }
    if (libcuda) {
        /*
         * 真实现这里 cuMemAlloc(&gpu_ptr, size). 当前骨架不强行拉 cuda.h,
         * 这里以 memalign 模拟 GPU buffer (=与上一轮 perftest_mem_gpu_proxy
         * 行为一致). 单独再 memalign 一份 host shadow.
         */
        GA_LOG("libcuda present but skeleton uses memalign-sim for GPU; "
               "real build will replace this with cuMemAlloc");
    } else {
        GA_LOG("simulation mode requested (no libcuda)");
    }

    out->gpu_ptr = memalign(alignment, size);
    out->addr    = memalign(alignment, size);  /* host shadow */
    if (out->gpu_ptr == NULL || out->addr == NULL) {
        free(out->gpu_ptr); out->gpu_ptr = NULL;
        free(out->addr);    out->addr    = NULL;
        if (libcuda) dlclose(libcuda);
        GA_LOG("FAIL: ENOMEM");
        return -1;
    }
    memset(out->gpu_ptr, 0, size);
    memset(out->addr,    0, size);
    out->size          = size;
    out->dmabuf_fd     = -1;     /* host-proxy 不走 dma-buf */
    out->dmabuf_offset = 0;
    out->strategy      = PERFTEST_GPU_ALLOC_HOST_PROXY;

    GA_LOG("OK: gpu_ptr=%p shadow=%p size=%zu (URMA registers shadow)",
           out->gpu_ptr, out->addr, size);
    if (libcuda) dlclose(libcuda);
    return 0;
}

/* ===========================================================================
 * step 4: memalign 兜底
 *
 * 这就是当前 perftest_resources.c 的默认分配路径. 永远工作 (除非系统 OOM).
 * 走到这一步意味着上面三步都没成功, perftest 仍能跑, 只是数据真的就在
 * host 上, 与原 read_lat / write_lat 完全一致.
 * =========================================================================== */
static int try_alloc_memalign(size_t size, size_t alignment,
                              perftest_gpu_buffer_t *out)
{
    out->addr = memalign(alignment, size);
    if (out->addr == NULL) {
        GA_LOG("FAIL: ENOMEM allocating %zu bytes", size);
        return -1;
    }
    memset(out->addr, 0, size);
    out->size          = size;
    out->dmabuf_fd     = -1;
    out->dmabuf_offset = 0;
    out->gpu_ptr       = NULL;
    out->strategy      = PERFTEST_GPU_ALLOC_MEMALIGN;

    GA_LOG("OK: addr=%p size=%zu (plain host memory fallback)",
           out->addr, size);
    return 0;
}

/* ===========================================================================
 * 公共入口
 * =========================================================================== */
int perftest_gpu_alloc_buffer(size_t size, size_t alignment,
                              perftest_gpu_buffer_t *out)
{
    if (out == NULL || size == 0) {
        GA_LOG("invalid arg out=%p size=%zu", (void *)out, size);
        errno = EINVAL;
        return -1;
    }
    if (alignment < 4096) {
        alignment = 4096;
    }
    memset(out, 0, sizeof(*out));

    GA_LOG("requested size=%zu align=%zu force_fail_mask=0x%x",
           size, alignment, g_force_fail_mask);

    if (try_alloc_gdr(size, alignment, out) == 0) {
        GA_LOG("DONE via %s", perftest_gpu_alloc_strategy_name(out->strategy));
        return 0;
    }
    if (try_alloc_peermem(size, alignment, out) == 0) {
        GA_LOG("DONE via %s", perftest_gpu_alloc_strategy_name(out->strategy));
        return 0;
    }
    if (try_alloc_host_proxy(size, alignment, out) == 0) {
        GA_LOG("DONE via %s", perftest_gpu_alloc_strategy_name(out->strategy));
        return 0;
    }
    if (try_alloc_memalign(size, alignment, out) == 0) {
        GA_LOG("DONE via %s", perftest_gpu_alloc_strategy_name(out->strategy));
        return 0;
    }

    GA_LOG("FATAL: all 4 strategies failed (likely OOM)");
    return -1;
}

void perftest_gpu_free_buffer(perftest_gpu_buffer_t *buf)
{
    if (buf == NULL) return;

    GA_LOG("releasing strategy=%s addr=%p gpu_ptr=%p size=%zu fd=%d",
           perftest_gpu_alloc_strategy_name(buf->strategy),
           buf->addr, buf->gpu_ptr, buf->size, buf->dmabuf_fd);

    switch (buf->strategy) {
        case PERFTEST_GPU_ALLOC_GDR:
            /* 真实现: close(buf->dmabuf_fd); cuMemFree(buf->addr); */
            if (buf->dmabuf_fd >= 0) {
                /* 占位 */
            }
            free(buf->addr);
            break;
        case PERFTEST_GPU_ALLOC_PEERMEM:
            /* 真实现: cuMemFree(buf->addr); */
            free(buf->addr);
            break;
        case PERFTEST_GPU_ALLOC_HOST_PROXY:
            /* 真实现 GPU 端 cuMemFree(buf->gpu_ptr); */
            free(buf->gpu_ptr);
            free(buf->addr);
            break;
        case PERFTEST_GPU_ALLOC_MEMALIGN:
            free(buf->addr);
            break;
        default:
            break;
    }
    memset(buf, 0, sizeof(*buf));
}
