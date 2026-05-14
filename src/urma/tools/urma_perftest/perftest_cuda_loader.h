/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2026. All rights reserved.
 *
 * 文件: perftest_cuda_loader.h
 *
 * 这是什么 (What):
 *   urma_perftest 在新增的 --gpu-mode={proxy,peermem,direct} 路径下用来调
 *   *真实 CUDA Driver API* 的一层薄封装. 与 linux-rdma 项目 src/cuda_loader.c
 *   思路一致: 不直接 include cuda.h, 而是 dlopen(libcuda.so.1) + dlsym 拿到
 *   函数指针, 这样:
 *     - 编译时不依赖 CUDA SDK 头文件;
 *     - 运行时没有 libcuda 也不会 SEGV, 只在 init 阶段返回 -1, 由上层决定
 *       fallback 还是退出.
 *
 * 实验环境要求 (Tutorial):
 *   1) NVIDIA Driver 安装 (含 /usr/lib/x86_64-linux-gnu/libcuda.so.1 或
 *      /usr/lib64/libcuda.so.1).
 *      Ubuntu:  apt install nvidia-driver-535 (or newer);
 *      openEuler / RHEL: 来自 NVIDIA repo 的 nvidia-driver-libs.
 *      验证:    ldconfig -p | grep libcuda.so.1
 *   2) (--gpu-mode=peermem 需要) nvidia_peermem.ko 或 amd_peermem.ko 内核模块
 *      已 modprobe; 验证: lsmod | grep peermem.
 *   3) (--gpu-mode=direct 需要) NIC + ubcore + udma 已经支持 dma_buf_attach
 *      (本 repo 的 GDR patch 落地后). CUDA Driver 还要 >= 12.2 才有
 *      cuMemGetHandleForAddressRange 的 DMA_BUF_FD 句柄类型.
 *
 * 给内核老兵的提示:
 *   - cuMemAlloc 在 NVIDIA driver 内部走的是 nvidia-uvm / pci_iommu 类似的
 *     设备页表分配, 与你熟悉的 dma_alloc_coherent 一类语义不同 (CPU 是看
 *     不见 d_ptr 的). 所以 D2H / H2D 必须显式 memcpy.
 *   - dlopen("libcuda.so.1", RTLD_LAZY) 第二个参数 LAZY 让符号在第一次调用
 *     时才解析, 启动开销最小.
 */

#ifndef PERFTEST_CUDA_LOADER_H
#define PERFTEST_CUDA_LOADER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 一次性 dlopen + dlsym. 重复调用安全, 内部带 idempotent 标志.
 * 返回 0 表示后续 perftest_cuda_* 接口可用, 返回 -1 表示 libcuda 不在.
 *
 * 怎么用:
 *   if (perftest_cuda_loader_init() == 0) {
 *       perftest_cuda_ctx_create(0);
 *       ...
 *   } else {
 *       // libcuda missing, fall back to host memalign
 *   }
 */
int  perftest_cuda_loader_init(void);
void perftest_cuda_loader_fini(void);

/* 0 = 未加载, 1 = 已加载. */
int  perftest_cuda_is_loaded(void);

/*
 * 在指定 GPU 上建一个 CUcontext (并 set current). dev_id 来自 CLI 的
 * --gpu-dev (尚未引入), 默认 0.
 *
 * 等价于:
 *   cuInit(0); cuDeviceGet(&dev, id); cuCtxCreate(&ctx, 0, dev); cuCtxSetCurrent(ctx);
 */
int  perftest_cuda_ctx_create(int dev_id);
void perftest_cuda_ctx_destroy(void);

/*
 * 设备内存分配 / 释放 (cuMemAlloc / cuMemFree).
 *   *out_dptr 是 CUdeviceptr, 在用户态只是 64-bit 整数; CPU 不能直接 deref!
 */
int  perftest_cuda_mem_alloc(size_t size, void **out_dptr);
int  perftest_cuda_mem_free(void *dptr);

/*
 * Pinned host 内存分配 / 释放 (cuMemAllocHost / cuMemFreeHost).
 *   *out_hptr 是 host 指针, CPU 可直接读写, 同时被 NIC 看到的物理地址
 *   稳定 (pinned), 这就是 proxy 模式下的 shadow buffer.
 */
int  perftest_cuda_mem_alloc_host(size_t size, void **out_hptr);
int  perftest_cuda_mem_free_host(void *hptr);

/*
 * D2H / H2D 拷贝 (cuMemcpyDtoH / cuMemcpyHtoD).
 *   - perftest_gpu_alloc 中 proxy 模式的 stage_to_nic / stage_from_nic 真
 *     实现就调这两个;
 *   - 验证流 (perftest_validation_*_gpu) 也用它做 fill / check.
 */
int  perftest_cuda_memcpy_d2h(void *host, void *dev, size_t n);
int  perftest_cuda_memcpy_h2d(void *dev, const void *host, size_t n);

/*
 * dma-buf 导出 (cuMemGetHandleForAddressRange with CU_MEM_RANGE_HANDLE_TYPE_DMA_BUF_FD).
 *   --gpu-mode=direct 用. CUDA Driver >= 12.2 才支持, 早于此版本 dlsym 会
 *   失败, 函数返回 -1, 调用方应当 fallback peermem 或 proxy.
 *
 *   out_fd     :  >=0 时为 dma-buf fd, 由 caller 负责 close();
 *   out_offset :  dptr 与按 host page 对齐之后的差值;
 */
int  perftest_cuda_get_dmabuf_fd(void *dptr, size_t size,
                                 int *out_fd, uint64_t *out_offset);

#ifdef __cplusplus
}
#endif

#endif /* PERFTEST_CUDA_LOADER_H */
