/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2026. All rights reserved.
 *
 * 文件: perftest_memory.h
 * 作用 (What is this?):
 *   这是 URMA 性能测试工具 urma_perftest 拟引入的"可插拔内存后端"抽象头。
 *   对标 RDMA 社区 perftest 中的 src/memory.h —— 在 IB Verbs 测试里通过
 *   struct memory_ctx 把"主机内存 / NVIDIA GPU / AMD GPU / dma-buf 直传"
 *   等不同来源的缓冲区统一成一组回调函数 (allocate_buffer / free_buffer /
 *   copy_*), 让 ib_write_bw 这种工具可以同时跑 host 与 GDR (GPUDirect RDMA)
 *   场景而无需改主流程。
 *
 * 为什么要新增 (Why?):
 *   当前 urma_perftest 在 perftest_resources.c::register_mem() 里硬编码:
 *        local_buf[i] = memalign(...) | ub_hugemalloc(...);
 *        urma_register_seg(ctx, &seg_cfg);   // 只知道主机 va
 *   一旦想跑 GPU memory direct, 整个数据通路都得改 —— CPU 直接 *post_buf =
 *   ... 的 sentinel 写法、write_dirty_thread 里 str[i] = rand_num 都会在
 *   GPU device pointer 上 SIGSEGV. 所以我们先抽象一个 ops, 再把所有读写
 *   buffer 的点路由到 ops, 这样后续真正接 CUDA / ROCm / dma-buf 时主流程
 *   不必再动.
 *
 * 给内核老兵的提示 (Note for kernel devs):
 *   - 这里的 "ops" 就是函数指针表, 类似内核里的 struct file_operations /
 *     struct net_device_ops, 只是这里跑在用户态.
 *   - "dmabuf_fd" 与内核 dma-buf 子系统是同一回事:
 *       用户态 cuMemGetHandleForAddressRange(..., DMA_BUF_FD, ...) 拿到 fd ->
 *       传给驱动 ioctl -> 内核 dma_buf_get(fd) -> dma_buf_attach +
 *       dma_buf_map_attachment 直接拿 sg_table, 绕开 pin_user_pages.
 *   - "GDR" (GPUDirect RDMA) 在 URMA 这条路上更倾向于走 dma-buf, 而不是
 *     像老 NVIDIA 那样另写一个 nvidia-peermem 内核插件.
 *
 * 当前阶段 (Current state):
 *   本次提交只放骨架: 函数体留 TODO 注释, 仅 print 一行 + 返回 0/NULL,
 *   假装"已经成功". 真正的逻辑在后续 commit 里逐步替换.
 *   所有结构体新增字段也"先以注释表达", 不实际加进现有结构 (避免 ABI 与
 *   现网编译断裂), 在 docs/urma_gdr_analysis.md 里有完整清单.
 */

#ifndef PERFTEST_MEMORY_H
#define PERFTEST_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 前向声明: 避免互相 include perftest_parameters.h.
 * 真正的 perftest_config_t 仍然在 perftest_parameters.h 里. */
struct perftest_config;
typedef struct perftest_config perftest_config_t;

/*
 * 枚举: 内存后端类型.
 *
 * 解释 (What):
 *   一次 perftest 运行只挂一个后端. CLI 通过 --use_cuda / --use_rocm /
 *   --use_dmabuf 等指定. 默认是 PERFTEST_MEM_HOST.
 *
 * 怎么用 (How):
 *   - 在 perftest_parse_args 里根据命令行设置 cfg->mem_type;
 *   - 在 perftest_resources.c::register_mem() 里 switch(cfg->mem_type) 调
 *     对应的 perftest_*_mem_create();
 *   - 后续所有访问 buffer 的代码 (write_dirty / sentinel / 校验) 都通过
 *     ops->copy_* 与 ops->write_byte/read_byte 走, 不再直接 memcpy/解引用.
 */
typedef enum perftest_mem_type {
    PERFTEST_MEM_HOST   = 0,  /* memalign / ub_hugemalloc, 现状 */
    PERFTEST_MEM_CUDA   = 1,  /* NVIDIA: cuMemAlloc + dma-buf fd */
    PERFTEST_MEM_ROCM   = 2,  /* AMD:    hipMalloc  + dma-buf fd */
    PERFTEST_MEM_DMABUF = 3,  /* 直接由 --dmabuf-fd 传入, 用于第三方加速器 */
    PERFTEST_MEM_TYPE_MAX
} perftest_mem_type_t;

/*
 * 枚举: GDR 工作模式.
 *
 * 这是什么 (What):
 *   配合 GPU 类后端 (CUDA / ROCm / DMABUF), 决定"NIC 怎么看到 GPU 数据".
 *   两种主路径:
 *
 *     PERFTEST_GDR_DIRECT (真 GDR):
 *         - GPU 用户态运行时 cuMemAlloc / hipMalloc 出来一段显存;
 *         - 调用 cuMemGetHandleForAddressRange(... DMA_BUF_FD ...) 拿到 fd;
 *         - 用户态 urma_register_seg_dmabuf(ctx, &cfg, fd, off);
 *         - 内核 ubcore dma_buf_attach + map_attachment, NIC MAPT 直接指
 *           向 GPU BAR1 物理地址;
 *         - NIC <DMA> GPU, CPU 不参与数据搬运 -> 真正零拷贝.
 *
 *     PERFTEST_GDR_HOST_PROXY (主机代理, 当无 GDR 驱动时使用):
 *         - GPU 仍然用 cuMemAlloc 拿到 d_ptr, 假装"数据在 GPU 上";
 *         - 同时 cuMemAllocHost 一份等大 host pinned shadow;
 *         - 用户态向 URMA 注册的是 host shadow (用普通 urma_register_seg,
 *           不需要 GDR 驱动);
 *         - 每次发送前: cuMemcpyDtoH(shadow, d_ptr, len) 把 GPU 数据"借
 *           道"主机, 然后 NIC DMA shadow;
 *         - 每次接收后: cuMemcpyHtoD(d_ptr, shadow, len) 把数据回灌 GPU.
 *         - 即"主机做中转". 性能比 DIRECT 差一倍多, 但完全不依赖 dma-buf
 *           内核驱动, 是 GDR 不可用环境下的可工作 baseline.
 *
 *   AUTO 与 OFF 是控制位, 不会真的成为 ops->gdr_mode:
 *     AUTO -> 调 urma_query_dmabuf_caps, 设备支持就 DIRECT, 否则 PROXY;
 *     OFF  -> 退化, 完全跑 host (像今天的 perftest 一样).
 *
 * 给内核老兵的提示:
 *   把 PROXY 想成"NIC 没法直接 DMA 加速器内存, 我们用 host 内存做 bounce
 *   buffer", 与你在 dma-mapping 子系统里看到的 swiotlb bounce 基本同构.
 */
typedef enum perftest_gdr_mode {
    PERFTEST_GDR_AUTO       = 0,  /* 探测能力, 能 DIRECT 就 DIRECT, 否则 PROXY */
    PERFTEST_GDR_DIRECT     = 1,  /* 强制 dma-buf 直传 GPU */
    PERFTEST_GDR_HOST_PROXY = 2,  /* 强制走 host shadow */
    PERFTEST_GDR_OFF        = 3,  /* 完全退化为 host 模式 */
    PERFTEST_GDR_MODE_MAX
} perftest_gdr_mode_t;

/*
 * 主结构体: 内存后端 ops 表.
 *
 * 解释 (What):
 *   每个后端都用 perftest_*_mem_create() 工厂函数返回这样一个 ops 结构,
 *   主程序只看 ops, 不关心是 host 还是 GPU. 与内核 file_operations 完全
 *   同构, 区别仅在于这里没有 fd 概念, 第一参数永远传 ops 自身, 让具体
 *   后端可以用 container_of (类似宏定义见 urma_perftest 里的 ub_util)
 *   拿到自己的私有数据.
 */
typedef struct perftest_memory_ops {
    /*
     * init: 后端启动时打开设备 (CUDA: cuInit / cuCtxCreate; ROCm: hipInit;
     * Host: 无操作). 返回 0 成功, -1 失败.
     *
     * TODO (实现要点):
     *   - host backend: 直接 return 0;
     *   - cuda backend: 走 dlopen("libcuda.so") + cuInit(0) + cuDeviceGet
     *                   + cuCtxCreate + cuCtxSetCurrent;
     *   - rocm backend: dlopen("libamdhip64.so") + hipInit + hipSetDevice.
     */
    int  (*init)(struct perftest_memory_ops *self, perftest_config_t *cfg);

    /*
     * destroy: 释放上面 init 拿到的资源. 与 init 严格配对.
     */
    void (*destroy)(struct perftest_memory_ops *self);

    /*
     * alloc_buffer: 按对齐分配大小为 size 的缓冲区, 同时(可选)导出 dma-buf fd.
     *
     * 输出参数:
     *   *out_addr           = 缓冲区首地址 (对 CUDA 来说是 CUdeviceptr 转 void*)
     *   *out_dmabuf_fd      = -1 表示这是主机内存; 否则是 dma-buf 文件描述符
     *   *out_dmabuf_offset  = dma-buf 内偏移 (CUDA 对齐到 host page 时常 != 0)
     *
     * 后续注册段会:
     *   if (out_dmabuf_fd >= 0)
     *      urma_register_seg_dmabuf(ctx, &seg_cfg, fd, offset);
     *   else
     *      urma_register_seg(ctx, &seg_cfg);
     */
    int  (*alloc_buffer)(struct perftest_memory_ops *self,
                         uint64_t size, uint64_t alignment,
                         void **out_addr,
                         int *out_dmabuf_fd,
                         uint64_t *out_dmabuf_offset);

    /*
     * free_buffer: 与 alloc_buffer 配对. dma-buf fd 由后端 close().
     */
    void (*free_buffer)(struct perftest_memory_ops *self,
                        void *addr, int dmabuf_fd, uint64_t size);

    /*
     * copy_host_to_buffer / copy_buffer_to_host / copy_buffer_to_buffer:
     *
     *   主程序里所有"用 CPU 直接 memcpy / *p = x"的代码点都得改成调这三个 hook.
     *   - host 后端: 直接 memcpy.
     *   - cuda 后端: cuMemcpyHtoD / cuMemcpyDtoH / cuMemcpyDtoD.
     *   - rocm 后端: hipMemcpyHostToDevice / hipMemcpyDeviceToHost / hipMemcpyDeviceToDevice.
     */
    void *(*copy_host_to_buffer)(struct perftest_memory_ops *self,
                                 void *dst, const void *src, size_t n);
    void *(*copy_buffer_to_host)(struct perftest_memory_ops *self,
                                 void *dst, const void *src, size_t n);
    void *(*copy_buffer_to_buffer)(struct perftest_memory_ops *self,
                                   void *dst, const void *src, size_t n);

    /*
     * write_byte / read_byte: latency 测试用 sentinel 字节做 polling.
     * GPU 模式必须分配一个小 host shadow 来扮演 sentinel, 否则 *poll_buf
     * 会发生 GPU faulted access. 这两个 hook 就是给 latency 主循环做这件事.
     */
    void   (*write_byte)(struct perftest_memory_ops *self, void *addr,
                         uint64_t off, uint8_t v);
    uint8_t (*read_byte)(struct perftest_memory_ops *self, const void *addr,
                         uint64_t off);

    /*
     * negotiate_gdr_mode:
     *   当 cfg->gdr_mode == PERFTEST_GDR_AUTO 时, init() 之后由主程序调一次,
     *   后端内部:
     *     1) 调用 urma_query_dmabuf_caps(dev, &caps) 看 NIC + 内核侧能力;
     *     2) 看 GPU 运行时能不能导出 dma-buf (cuda: driver_version >= 12.2,
     *        rocm: hsa_amd_portable_export_dmabuf 可用);
     *     3) 两端都 OK -> self->gdr_mode = PERFTEST_GDR_DIRECT;
     *     4) 否则 -> self->gdr_mode = PERFTEST_GDR_HOST_PROXY, 同时打印
     *        "GDR not available on this device, falling back to host-proxy".
     *
     *   返回 0 = 成功, 协商完毕 self->gdr_mode 已写好;
     *   返回 -1 = 致命错误 (例如 cfg 强制 DIRECT 但驱动不支持).
     *
     *   host 后端没有"GDR"概念, 直接返回 0, gdr_mode 留 OFF.
     */
    int  (*negotiate_gdr_mode)(struct perftest_memory_ops *self, void *urma_dev_opaque);

    /*
     * stage_to_nic_buffer / stage_from_nic_buffer:
     *   这两个钩子专门为 PROXY 模式设计, DIRECT/host 模式下是 noop.
     *
     *   stage_to_nic_buffer: 在每次 post_jfs_wr 前调用一次, 用来:
     *       cuMemcpyDtoH(shadow_addr, gpu_addr, len);  // 把 GPU 数据先拷到 NIC 看得见的 host shadow
     *
     *   stage_from_nic_buffer: 在每次 recv 完成后调用, 用来:
     *       cuMemcpyHtoD(gpu_addr, shadow_addr, len);  // 把 NIC 收到的数据回灌 GPU
     *
     *   两个 hook 的入参语义: shadow_addr 是 ops->alloc_buffer 返回的 out_addr
     *   (它就是 URMA 注册的那段地址), gpu_addr 是后端内部保存的 GPU 设备指针
     *   (PROXY 模式下从 priv->gpu_ptr 拿).
     *
     *   这是真正能让"无 GDR 驱动也跑 GPU 数据"的关键入口. 它把"GPU<->NIC"的
     *   语义装到了 perftest 的 IO 路径里, 上层不需要再分 if/else.
     */
    int  (*stage_to_nic_buffer)(struct perftest_memory_ops *self,
                                void *shadow_addr, void *gpu_addr, size_t len);
    int  (*stage_from_nic_buffer)(struct perftest_memory_ops *self,
                                  void *gpu_addr, void *shadow_addr, size_t len);

    /* 后端类型与私有数据 (实现里强转为自己的 ctx). */
    perftest_mem_type_t  type;
    perftest_gdr_mode_t  gdr_mode;   /* 由 negotiate_gdr_mode() 写入 */
    void                *priv;
} perftest_memory_ops_t;

/*
 * 工厂函数: 每个后端一个.
 *
 * 调用时机: perftest_resources.c::create_ctx 的最前面, 拿到 ops 后再
 *           走 register_mem.
 *
 * 返回值: 失败返回 NULL.
 *
 * 释放: 调用 ops->destroy(ops) 之后由调用方 free(ops).
 */
perftest_memory_ops_t *perftest_host_mem_create(perftest_config_t *cfg);
perftest_memory_ops_t *perftest_cuda_mem_create(perftest_config_t *cfg);
perftest_memory_ops_t *perftest_rocm_mem_create(perftest_config_t *cfg);
perftest_memory_ops_t *perftest_dmabuf_mem_create(perftest_config_t *cfg);

/*
 * perftest_gpu_proxy_create / 访问器
 *
 * 这是什么 (What):
 *   "GPU 内存 + 无 GDR 驱动" 场景下的最小可工作后端 (perftest_mem_gpu_proxy.c).
 *   它显式旁路 GDR (不调 urma_register_seg_dmabuf), 也不依赖 CUDA / ROCm /
 *   内核 dma-buf 驱动, 因此可以在普通开发机上跑 demo 验证.
 *
 * 这两个访问器 (get_gpu_ptr / get_size) 只给独立的 demo 使用, 让 demo 在
 * 没有真实 perftest 主循环的情况下也能"假装自己是 GPU 应用", 直接读写
 * GPU buffer 然后看 stage_* 的效果. 真实 urma_perftest 不需要它们.
 */
perftest_memory_ops_t *perftest_gpu_proxy_create(perftest_config_t *cfg);
void                  *perftest_gpu_proxy_get_gpu_ptr(perftest_memory_ops_t *ops);
size_t                 perftest_gpu_proxy_get_size(perftest_memory_ops_t *ops);

/*
 * 便利分发器: 根据 cfg->mem_type 调对应的 *_create.
 *   未来 register_mem() 的第一行就是
 *       ctx->mem_ops = perftest_memory_create(cfg);
 *   其它代码不需要 if/else.
 */
perftest_memory_ops_t *perftest_memory_create(perftest_config_t *cfg);
void                   perftest_memory_destroy(perftest_memory_ops_t *ops);

#endif /* PERFTEST_MEMORY_H */
