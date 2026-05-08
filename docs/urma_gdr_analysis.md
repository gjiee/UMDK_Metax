# URMA GDR (GPUDirect-RDMA 等价) 框架支持分析报告

> 仓库克隆来源: https://gitcode.com/TongX123/umdk
> 对比对象:        linux-rdma/perftest (RDMA perftest, master)
> 关注主题:        在 URMA / UMDK 中加入"类 GDR (GPU Direct RDMA)"框架所需的函数级 patch 清单
> 报告版本:        v1.0
> 适用代码:        umdk 25.12.x（src/urma/...）

---

## 1. 背景与目标

UMDK 是华为灵衢总线（UnifiedBus）配套的用户态/内核态通信开发套件，
其中的 `URMA` 子项目对标 IB Verbs，提供以"段 (segment)"为核心的内存语义远端访问。
仓库内含一个端到端的性能测试工具 `urma_perftest`（位于 `src/urma/tools/urma_perftest/`），
功能和命令行接口对标 RDMA 社区的 `ib_read_lat / ib_write_bw / ib_send_bw / ib_atomic_*` 等。

RDMA 社区的 `perftest` 通过 `--use_cuda / --use_rocm / --use_neuron / --use_mlu / --use_dmabuf`
等参数在主机内存以外，直接把"GPU/加速器 HBM"注册为 RDMA MR 来跑端到端 GDR 测试。
URMA 当前完全不具备这一能力 —— 整个数据通路只接受由 `memalign / ub_hugemalloc` 出来的
普通主机内存。本报告评估"在 URMA 中引入一套类 GDR 框架"所需的修改面，
并给出具体的函数 patch 清单。

---

## 2. urma_perftest 当前内存路径走读

入口与关键调用链（基于源码精确行号）：

```216:216:src/urma/tools/urma_perftest/urma_perftest.c
int main(int argc, char *argv[])
```

主流程：`perftest_parse_args -> establish_connection -> create_ctx -> run_test`，
其中 `create_ctx -> register_mem` 是真正分配 + 注册数据缓冲区的地方。

`register_mem` 实现位置：

```743:865:src/urma/tools/urma_perftest/perftest_resources.c
static int register_mem(perftest_context_t *ctx, perftest_config_t *cfg)
{
    ...
    if (cfg->use_huge_page) {
        ctx->local_buf[i] = ub_hugemalloc(ctx->buf_len, cfg->huge_page, NULL);
    } else {
        ctx->local_buf[i] = memalign(ctx->page_size, ctx->buf_len);
    }
    ...
    seg_cfg.va = (uint64_t)ctx->local_buf[j];
    ...
    ctx->local_tseg[j] = urma_register_seg(ctx->urma_ctx, &seg_cfg);
}
```

可见 URMA 性能测试当前**仅支持主机内存**，且：

1. 内存分配硬编码为 `memalign` 或 `ub_hugemalloc`（巨页）。
2. 数据初始化、`write_dirty` 等都是 CPU 直接 `str[i] = rand_num;` 写入
   （见 `urma_perftest.c:113-137` 中 `write_dirty_thread`）—— 完全假设缓冲区是 CPU 可寻址的。
3. 注册段时 `urma_register_seg` 只看见用户虚拟地址 `va` 和长度 `len`，
   不携带任何"内存类型 (host/GPU)"或"dma-buf fd"信息。

URMA 注册段的 ABI 以及相关结构体：

```954:962:src/urma/lib/urma/core/include/urma_types.h
typedef struct urma_seg_cfg {
    uint64_t va;
    uint64_t len;
    urma_token_id_t *token_id;
    urma_token_t token_value;
    urma_reg_seg_flag_t flag;
    uint64_t user_ctx;
    uint64_t iova;
} urma_seg_cfg_t;
```

```872:895:src/urma/lib/urma/core/include/urma_types.h
typedef union urma_reg_seg_flag {
    struct {
        uint32_t token_policy   : 3;
        uint32_t cacheable      : 1;
        uint32_t dsva           : 1;
        uint32_t access         : 6;
        uint32_t non_pin        : 1;
        uint32_t user_iova      : 1;
        uint32_t token_id_valid : 1;
        uint32_t reserved       : 18;
    } bs;
    uint32_t value;
} urma_reg_seg_flag_t;
```

内核 IOCTL 入参（用户态侧 TLV 描述）：

```182:196:src/urma/lib/urma/core/include/urma_cmd.h
typedef struct urma_cmd_register_seg {
    struct {
        uint64_t va;
        uint64_t len;
        uint32_t token_id;
        uint64_t token_id_handle;
        uint32_t token;
        uint32_t flag;
    } in;
    ...
} urma_cmd_register_seg_t;
```

HiSilicon 用户态驱动落到 `urma_cmd_register_seg` 触发内核 `pin_user_pages`：

```91:106:src/urma/hw/udma/udma_u_segment.c
static int udma_exec_register_seg_cmd(urma_context_t *urma_ctx,
                                      urma_seg_cfg_t *seg_cfg,
                                      urma_target_seg_t *urma_tseg)
{
    ...
    ret = urma_cmd_register_seg(urma_ctx, urma_tseg, seg_cfg, &udata);
}
```

**结论 #1**：URMA 注册段的端到端协议（用户态 API / TLV / 内核 ABI / HW 驱动）都是
"假设虚拟地址背后是 CPU 物理页"。要支持 GDR，需要在每一层都插入"是否为设备内存 / dma-buf"的旁路。

---

## 3. RDMA perftest 的 GDR 实现是怎么做的

`linux-rdma/perftest` 用一个**抽象内存后端**架构来支持多类内存：

```c
// src/memory.h
struct memory_ctx {
    int (*init)(struct memory_ctx *ctx);
    int (*destroy)(struct memory_ctx *ctx);
    int (*allocate_buffer)(struct memory_ctx *ctx, int alignment, uint64_t size,
                           int *dmabuf_fd, uint64_t *dmabuf_offset,
                           void **addr, bool *can_init);
    int (*free_buffer)(struct memory_ctx *ctx, int dmabuf_fd, void *addr, uint64_t size);
    void *(*copy_host_to_buffer)(void *dest, const void *src, size_t size);
    void *(*copy_buffer_to_host)(void *dest, const void *src, size_t size);
    void *(*copy_buffer_to_buffer)(void *dest, const void *src, size_t size);
    int  (*validation_init)(struct memory_ctx *ctx,
                            const struct validation_config *cfg);
    int  (*validation_start)(struct memory_ctx *ctx);
    int  (*validation_stop)(struct memory_ctx *ctx,
                            struct data_validation_result *result);
    void (*validation_destroy)(struct memory_ctx *ctx);
};
```

具体后端（节选）：

| 后端 | 文件 | 关键函数 |
| --- | --- | --- |
| Host  | `src/host_memory.c`   | `memalign / mmap / shmget`（含 hugepages） |
| CUDA  | `src/cuda_memory.c`   | `cuMemAlloc / cuMemAllocManaged / cuMemAllocHost`，DMA-BUF 通过 `cuMemGetHandleForAddressRange(..., CU_MEM_RANGE_HANDLE_TYPE_DMA_BUF_FD,...)`，触发 `ibv_reg_dmabuf_mr` |
| ROCm  | `src/rocm_memory.c`   | `hipMalloc`，DMA-BUF 通过 `hsa_amd_portable_export_dmabuf` |
| Neuron| `src/neuron_memory.c` | Trainium SDK |
| MLU   | `src/mlu_memory.c`    | Cambricon |

GPU 端为什么"能直接被 NIC DMA"——RDMA 这一边由两步配合：
1. **用户态**: 把 GPU 设备指针（CUdeviceptr）当作普通 va 传给 verbs。
2. **内核态**: `mlx5/efa/...` 驱动调用 `ib_umem_get(...)` 时，
   - 老路径走 `nvidia-peermem` (peer_memory_client) 注册的回调拿 SG list；
   - 新路径走 dmabuf：用户态 `ibv_reg_dmabuf_mr(pd, offset, len, addr, fd, access)`
     在内核走 `ib_umem_dmabuf_get(...)`，由 `dma_buf_attach + map_attachment`
     直接拿到设备 BAR1 的 sg_table，无需 peermem。

**结论 #2**：URMA 复刻 GDR 的最干净路径就是模仿"DMA-BUF + 抽象内存后端"——
- 用户态加一个 `urma_memory_ops`（类似 `struct memory_ctx`）；
- 给 `urma_register_seg` 加一个等价于 `ibv_reg_dmabuf_mr` 的 `urma_register_dmabuf_seg`；
- 内核 `uburma/ubcore/udma` 用 `dma_buf_*` 替换 `pin_user_pages`，避开 peermem 这条历史包袱。

---

## 4. URMA vs RDMA perftest 内存路径对比

| 关键能力 | RDMA perftest | URMA `urma_perftest` (current) |
| --- | --- | --- |
| 命令行选项 | `--use_cuda[=<id>] / --use_rocm / --use_neuron / --use_mlu / --use_cuda_bus_id / --use_cuda_dmabuf / --cuda_mem_type / --gpu_touch` | 仅 `--use_huge_page / --huge_page_size`；**无任何 GPU/dma-buf 选项** |
| 内存抽象层 | `struct memory_ctx` + `host_memory.c / cuda_memory.c / rocm_memory.c / ...` | 无；`register_mem()` 内联 `memalign / ub_hugemalloc` |
| 设备内存分配 | `cuMemAlloc / hipMalloc / ...` | 无 |
| DMA-BUF 导出 | `cuMemGetHandleForAddressRange(..., CU_MEM_RANGE_HANDLE_TYPE_DMA_BUF_FD, ...)`；ROCm `hsa_amd_portable_export_dmabuf` | 无 |
| 用户态注册接口 | `ibv_reg_mr` (host) / `ibv_reg_dmabuf_mr` (gpu) | 仅 `urma_register_seg(ctx, urma_seg_cfg_t*)` —— **无 dma-buf 入口** |
| Verbs/URMA 旗标 | `IBV_ACCESS_RELAXED_ORDERING` 等；通过 dmabuf API 传 fd/offset | `urma_reg_seg_flag` 占用 1 bit `non_pin` 但**没有 dma-buf 位** |
| 内核 ABI | `IB_USER_VERBS_EX_CMD_REG_DMABUF_MR`，IOCTL 携带 `fd / offset` | `urma_cmd_register_seg.in` 仅 `va/len/token_id/...`，**无 fd/offset** |
| 内核驱动 pin 路径 | `ib_umem_dmabuf_get + dma_buf_attach + dma_buf_map_attachment` | `ubcore` 走 `pin_user_pages_fast`（peer-mem 完全没接） |
| HW driver 写映射表 | mlx5 `mlx5_ib_umem_get` 兼容 dmabuf sgt | hisi udma `udma_u_grant_segment + ummu_grant`，全部以 CPU 物理页为前提 |
| 数据初始化与校验 | 通过 `copy_host_to_buffer / copy_buffer_to_host` 抽象，`cuMemcpy*` 实现 | 直接 `*post_buf = (char)x`、`memset/memcpy` —— **GPU 上会立刻 SIGSEGV** |
| `write_dirty` 干扰流量 | 由 `gpu_touch` kernel 实现 | `for(i...){ str[i] = rand_num; }` —— **GPU 上不可用** |
| 测试覆盖 | host/CUDA/ROCm/Neuron/MLU + dmabuf 矩阵 | 仅 host/hugepage |
| 文档 | `README.md` 专章 GDR | 无 |

---

## 5. URMA 增加 GDR 框架所需的函数级 Patch 清单

下面按"软件层 → 文件 → 函数"逐条列出**最小可工作 (MVP)** 与**完整版本**需要的改动。
带 ★ 的函数为**新增**，不带 ★ 的为**修改既有**。
其中 `urma_register_seg_dmabuf` 这个名字仅作建议，可改为 `urma_register_seg_ex`。

### 5.1 用户态 / `liburma` 公共层

文件: `src/urma/lib/urma/core/include/urma_types.h`

1. **修改 `union urma_reg_seg_flag.bs`**: 占用一个 reserved bit 作为
   `mem_type` 选择位，或新增 3 bit `mem_type` 子字段：
   `URMA_MEM_HOST(0) / URMA_MEM_DMABUF(1) / URMA_MEM_PEERMEM(2)`。
2. **修改 `struct urma_seg_cfg`**: 追加 `int dmabuf_fd; uint64_t dmabuf_offset;`
   两个字段 (注意 ABI 兼容：放在结构尾部，且新增对应 `URMA_SEG_CFG_FLAG_DMABUF` 标志)。
3. ★ 新类型 `typedef enum urma_mem_type { URMA_MEM_HOST, URMA_MEM_DMABUF, URMA_MEM_DEVICE_PEER } urma_mem_type_t;`

文件: `src/urma/lib/urma/core/include/urma_api.h`

4. ★ `urma_target_seg_t *urma_register_seg_dmabuf(urma_context_t *ctx,
   urma_seg_cfg_t *seg_cfg, int dmabuf_fd, uint64_t offset);`
5. ★ `urma_status_t urma_query_dmabuf_caps(urma_device_t *dev,
   urma_dmabuf_caps_t *out);` —— 返回 `support_dmabuf / support_peer / max_size / supported_alignments`。

文件: `src/urma/lib/urma/core/include/urma_provider.h`

6. **追加 `urma_ops` 函数指针**:
   - ★ `urma_target_seg_t *(*register_seg_dmabuf)(urma_context_t *ctx, urma_seg_cfg_t *seg_cfg, int dmabuf_fd, uint64_t offset);`
   - ★ `urma_status_t (*query_dmabuf_caps)(urma_device_t *dev, urma_dmabuf_caps_t *caps);`

文件: `src/urma/lib/urma/core/include/urma_cmd.h`

7. **修改 `struct urma_cmd_register_seg.in`**: 在末尾追加
   ```c
   int32_t dmabuf_fd;        /* -1 if not dmabuf */
   uint64_t dmabuf_offset;
   ```
   并新增 `URMA_CMD_REGISTER_SEG_DMABUF` 命令号或者复用本命令 + flag 位区分。

文件: `src/urma/lib/urma/core/urma_cmd_tlv.h` & `urma_cmd_tlv.c`

8. **追加 TLV 类型** `REGISTER_SEG_IN_DMABUF_FD / REGISTER_SEG_IN_DMABUF_OFFSET`，
   并在 `urma_ioctl_register_seg` 里 `ATTR(...)` 写出。

文件: `src/urma/lib/urma/core/urma_cmd.c`

9. **修改 `urma_cmd_register_seg`** 把上面两个新字段透传到 IOCTL，`-1` 表示主机内存路径。

文件: `src/urma/lib/urma/core/urma_cp_api.c`

10. ★ 实现 `urma_register_seg_dmabuf`，内部判断 `ctx->ops->register_seg_dmabuf`
    是否非空，否则返回 `URMA_ENOTSUPP`。

### 5.2 用户态 / HiSilicon HW 驱动 `udma`

文件: `src/urma/hw/udma/udma_u_segment.c`

11. ★ 新增 `udma_u_register_seg_dmabuf(urma_context_t *ctx, urma_seg_cfg_t *seg_cfg,
    int dmabuf_fd, uint64_t offset)`，调用 `urma_cmd_register_seg` 把 fd/offset
    透传给内核（`ummu_grant` 这一步要旁路：dma-buf 由内核侧建立 sgt，用户态不再
    `pin_user_pages` 等价物）。
12. **修改 `udma_u_check_seg_cfg`**：当 `mem_type==DMABUF` 时跳过对 `va` 的对齐校验
    （dma-buf 偏移可以非 4K 对齐）。
13. **修改 `udma_u_init_seg_cfg`**：`seg->va = seg_cfg->va` 仍保留作为"逻辑地址"，
    但增加 `seg->dmabuf_fd`、`seg->dmabuf_offset` 拷贝。
14. **修改 `udma_u_unregister_seg`**：dma-buf 路径下不调用 `udma_u_ungrant_seg`，
    改为只发 `urma_cmd_unregister_seg` 让内核 `dma_buf_detach`。
15. **修改 `urma_ops`** 注册表（一般在 `udma_u_init.c` / 类似入口）：补 `register_seg_dmabuf`。

文件: `src/urma/hw/udma/udma_u_ctl.c` (或 query 入口)

16. **修改 `udma_u_query_device`**：把内核返回的 `support_dmabuf` 位填到
    `urma_device_attr_t.dev_cap.feature`（需要在 `union urma_device_feat` 里加 1 bit）。

### 5.3 内核态 `uburma / ubcore`（**位于 openEuler 内核仓**，不在 umdk 仓内，但必须配套提交）

> 说明：umdk 这个仓是用户态实现，但内核 ABI 必须先在 openEuler kernel 的
> `drivers/ub/urma/uburma`、`drivers/ub/urma/ubcore` 里加好，才能让用户态拿到
> 新字段。下列函数同样要 patch 一份内核侧 PR。

文件 (kernel): `drivers/ub/urma/uburma/uburma_cmd.c`

17. **修改 `uburma_cmd_register_seg`**：解析新加的 TLV 字段 `dmabuf_fd / dmabuf_offset`，
    分发到 `ubcore_register_seg_dmabuf`。

文件 (kernel): `drivers/ub/urma/ubcore/ubcore_segment.c`

18. ★ 新增 `ubcore_register_seg_dmabuf(struct ubcore_device *dev,
    struct ubcore_seg_cfg *cfg, int fd, u64 offset)`，内部走：
    ```c
    dmabuf = dma_buf_get(fd);
    attach = dma_buf_attach(dmabuf, &dev->dev);
    sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
    ```
    生成"以 sg_table 为骨架"的 ubcore segment 描述。
19. ★ 新增 `ubcore_unregister_seg_dmabuf` —— 配套 `dma_buf_unmap_attachment +
    dma_buf_detach + dma_buf_put`。
20. **修改 `ubcore_register_seg`**：保留原 `pin_user_pages` 路径，
    根据 `cfg->flag.bs.mem_type` 分流到 dmabuf 版本。

文件 (kernel): `drivers/ub/hisi-ub/kernelspace/udma/`

21. **修改 `udma_register_segment`**（HW MTT/MAPT 写入函数）：当 segment 是
    dmabuf 时，从 sg_table 而非 pinned page array 构建 MAPT，每段 sg
    都通过 `ummu` 接口注入。
22. **修改 `udma_modify_segment / udma_unregister_segment`**：dmabuf 路径下
    `move_notify` 回调，`dma_buf_pin / dma_buf_unpin`（如果上层模型选 pinned dmabuf）。

### 5.4 `urma_perftest` 工具改造

文件: `src/urma/tools/urma_perftest/perftest_parameters.h`

23. **新增枚举** `enum perftest_mem_type { MEM_HOST, MEM_CUDA, MEM_ROCM, MEM_DMABUF }`
    与下列 CLI 选项常量：`PERFTEST_OPT_USE_CUDA / PERFTEST_OPT_CUDA_DEV_ID /
    PERFTEST_OPT_USE_ROCM / PERFTEST_OPT_USE_DMABUF / PERFTEST_OPT_GPU_TOUCH`。
24. **修改 `struct perftest_config`**：追加
    ```c
    enum perftest_mem_type mem_type;
    int      gpu_dev_id;
    char    *gpu_bus_id;
    bool     use_dmabuf;
    int      dmabuf_fd;        // 运行时分配
    uint64_t dmabuf_offset;
    ```

文件: `src/urma/tools/urma_perftest/perftest_parameters.c`

25. **修改 `perftest_parse_args`**：新增 `getopt_long` 长选项解析。
26. **修改 `print_cfg`**：打印当前内存类型，方便结果归档。
27. **修改 `check_local_cfg / check_remote_cfg`**：`use_jfce / write_dirty / use_huge_page`
    与 GPU 模式互斥校验。

文件: `src/urma/tools/urma_perftest/perftest_resources.h`

28. ★ 新增 `struct perftest_memory_ops`（仿 `memory_ctx`），含
    `init / destroy / alloc_buffer / free_buffer /
     copy_host_to_buffer / copy_buffer_to_host / copy_buffer_to_buffer`。
29. **修改 `struct perftest_context`**：把 `void **local_buf` 旁加上
    `struct perftest_memory_ops *mem_ops; void *mem_priv;
     int *dmabuf_fds; uint64_t *dmabuf_offsets;`。

文件: `src/urma/tools/urma_perftest/perftest_resources.c`

30. **重构 `register_mem`**：
    - 调用 `mem_ops->alloc_buffer(...)` 取得 `addr`、`dmabuf_fd`、`dmabuf_offset`；
    - 当 `mem_type == HOST` 时维持现有 `urma_register_seg`；
    - 否则走新 API `urma_register_seg_dmabuf(ctx, &seg_cfg, fd, offset)`。
31. **重构 `unregister_mem`**：选择 `mem_ops->free_buffer`。
32. **修改 `free_memory`**：dmabuf/cuda 路径不能 `free()`，要走 `mem_ops`。
33. **修改 `init_device`**：通过 `urma_query_device` 检查
    `dev_attr.dev_cap.feature.bs.support_dmabuf`，否则报错退出。

文件 (新建): `src/urma/tools/urma_perftest/perftest_mem_host.c`

34. ★ `perftest_mem_ops_t *perftest_host_mem_create(perftest_config_t *)` —— 用
    `memalign / ub_hugemalloc` 实现 `alloc_buffer`，`memcpy` 实现 copy hooks。

文件 (新建): `src/urma/tools/urma_perftest/perftest_mem_cuda.c`

35. ★ `perftest_mem_ops_t *perftest_cuda_mem_create(perftest_config_t *)`：
    `cuInit / cuDeviceGet / cuCtxCreate / cuMemAlloc /
     cuMemGetHandleForAddressRange(..., CU_MEM_RANGE_HANDLE_TYPE_DMA_BUF_FD)`。
36. ★ `perftest_cuda_copy_*`：用 `cuMemcpyHtoD / cuMemcpyDtoH / cuMemcpyDtoD`
    填充原本由 CPU 直接做的初始化 / `write_dirty` / 校验等。

文件 (新建): `src/urma/tools/urma_perftest/perftest_mem_rocm.c`

37. ★ 同上，使用 `hipMalloc` + `hsa_amd_portable_export_dmabuf`。

文件 (新建可选): `src/urma/tools/urma_perftest/perftest_mem_dmabuf.c`

38. ★ 仅做 thin wrapper：把外部传进来的 `--dmabuf-fd <fd> --dmabuf-offset <off>`
    透传给 `urma_register_seg_dmabuf`，便于 CI 与第三方加速器对接。

文件: `src/urma/tools/urma_perftest/urma_perftest.c`

39. **修改 `write_dirty_thread`**：通过 `mem_ops->copy_host_to_buffer / copy_buffer_to_buffer`
    取代 `str[i] = rand_num;`，否则 GPU 模式下立刻段错误。
40. **修改 `prepare_test`/`run_test`**：所有用户态读写 `local_buf` 之前都套 `mem_ops`。

文件: `src/urma/tools/urma_perftest/perftest_run_test.c`

41. **审计所有 `(char *)ctx->local_buf[...]` 解引用点**（已在第 2 节列出 ~20 处），
    把"拉低延迟用 polling sentinel" 的 `*post_buf = ...; while (*poll_buf != x);`
    改造为：
    - host 模式保留快路径；
    - GPU 模式分配一小段 host pinned shadow 作为 sentinel；
    - 数据缓冲仍走 GPU。

### 5.5 文档与构建

42. **修改 `src/urma/tools/urma_perftest/CMakeLists.txt`**：当配置打开 `URMA_WITH_CUDA`/`URMA_WITH_ROCM`
    时分别 link `cuda / cudart / hip` 并把对应的 `perftest_mem_*.c` 编译进可执行体。
43. **修改 `umdk.spec`**：新增 `--with cuda`、`--with rocm` build option，CI 镜像里把对应 SDK 加进
    `BuildRequires`。
44. **新增 `doc/en/urma/URMA_GDR_Guide.md` 与中文版**：模式矩阵、运行命令、
    与 `nvidia-peermem`/dmabuf-pin 的兼容性说明、与 NCCL/PyTorch DMA-BUF 用例对接。

---

## 6. MVP 函数 patch 清单（按提交顺序汇总）

下面是若以"最小可上线"为目标的提交分解，每一行对应一个独立 commit，
方便 review：

| # | Commit | 作用域 | 主要新增/修改函数 |
| ---: | --- | --- | --- |
| 1 | `urma: extend reg_seg ABI for dmabuf` | `liburma/core` + kernel uABI 头 | 修 `urma_seg_cfg` / `urma_reg_seg_flag` / `urma_cmd_register_seg`，新增 TLV 类型 |
| 2 | `urma: add urma_register_seg_dmabuf user API` | `liburma/core` | ★ `urma_register_seg_dmabuf`、`urma_query_dmabuf_caps` |
| 3 | `urma/udma: implement register_seg_dmabuf provider op` | `hw/udma` | ★ `udma_u_register_seg_dmabuf` 等 |
| 4 | `kernel/uburma+ubcore: dmabuf path for segment` | kernel (配套 PR) | ★ `ubcore_register_seg_dmabuf`、`ubcore_unregister_seg_dmabuf` |
| 5 | `urma_perftest: introduce pluggable memory ops` | `tools/urma_perftest` | ★ `perftest_memory_ops_t`、`perftest_host_mem_create` |
| 6 | `urma_perftest: add CUDA backend (--use_cuda)` | `tools/urma_perftest` | ★ `perftest_cuda_mem_create / cuda_alloc_buffer / cuda_copy_*` |
| 7 | `urma_perftest: add ROCm backend (--use_rocm)` | `tools/urma_perftest` | ★ `perftest_rocm_mem_create` |
| 8 | `urma_perftest: route write_dirty / sentinels via memory ops` | `tools/urma_perftest` | 修 `write_dirty_thread / run_*_lat / run_*_bw` |
| 9 | `doc: URMA GDR guide` | `doc/` | 新文档 |

提交 1–4 是协议层契约，必须最先合入；5–8 是工具改造；9 是文档。
内核侧 patch（提交 4 对应 kernel PR）必须先在 openEuler kernel 主线/特性分支落地，
否则用户态构建会因 IOCTL 头缺字段而失败。

---

## 7. 风险与依赖

- **Peer-memory vs DMA-BUF 路径选择**：为了避免维护类似 `nvidia-peermem` 的内核回调
  插件，建议 URMA 只走 dma-buf。需要 NVIDIA driver ≥ 535、CUDA ≥ 12.2、ROCm ≥ 6.0
  才能在用户态拿到 dma-buf fd；老环境下只能退化为主机内存模式。
- **`ummu_grant` 的语义**：HiSilicon 的 IOMMU 抽象当前以 `va + len` 为输入，
  dma-buf 路径下需要确认 `ummu_grant_dmabuf(sgt)` 类似入口是否存在；
  若不存在需要在 ummu 内核驱动里新增对应回调。
- **`non_pin` 位与 dma-buf 的互斥**：当前 `urma_reg_seg_flag.bs.non_pin = 1` 表示
  HMM 风格的可换出页，与 pinned dma-buf 语义冲突，需要在 5.1 第 1 步显式约束。
- **token / access 模型**：URMA 的 segment 通过 token 远端验证，dma-buf 需要确认
  访问位 `URMA_ACCESS_READ/WRITE/ATOMIC` 在 GPU BAR 上是否被 IOMMU 正确 enforce。
- **`infinite_print / write_dirty` 等观测路径**：必须改为通过 mem_ops 复制，
  否则 GPU 模式下首次访问 sentinel 即 SIGSEGV。
- **CI**：需要带 GPU 的 runner，建议在 openEuler 测试矩阵里新增
  `urma-cuda-runner`、`urma-rocm-runner`。

---

## 8. 总体工作量

按照"软件层"维度估算（**仅描述工作量级别，不给出日历周期**）：

| 工作项 | 难度 | 说明 |
| --- | --- | --- |
| ABI / TLV / IOCTL 扩展（5.1） | 中 | 触及 user-kernel 边界，需要 ABI 兼容设计 |
| HW 驱动 `udma` 适配（5.2） | 中 | 主要是 dma-buf sgt 替代 pinned pages，HW MAPT 写入逻辑改造 |
| 内核 `uburma + ubcore`（5.3） | **高** | 必须在 openEuler kernel 仓另开 PR，需 ummu/ubcore 协同 |
| `urma_perftest` 改造（5.4） | 中-高 | 内存抽象 + 数据访问点重写 ~20 处 |
| 文档 / CI（5.5） | 低 | 但 GPU CI 节点是关键依赖 |

总体改动覆盖约 **6 个用户态 .c 文件 + 4 个新建后端 .c + 4 个公共头**，
以及 **3 个内核 .c 文件**（位于 openEuler kernel 仓），共计 **新增约 40+ 函数 / 修改约 15 函数**。

---

## 9. 附录 — 拟新增的用户态头草案

```c
/* src/urma/lib/urma/core/include/urma_api.h —— 新增片段（仅示意） */

typedef enum urma_mem_type {
    URMA_MEM_HOST = 0,
    URMA_MEM_DMABUF = 1,
    URMA_MEM_DEVICE_PEER = 2, /* reserved for legacy peermem */
} urma_mem_type_t;

typedef struct urma_dmabuf_caps {
    uint8_t  support_dmabuf;
    uint8_t  support_dynamic;       /* dma_buf_pin/unpin support */
    uint64_t max_seg_size;
    uint32_t supported_alignments;  /* bitmap: 4K/64K/2M/1G */
} urma_dmabuf_caps_t;

urma_target_seg_t *urma_register_seg_dmabuf(urma_context_t *ctx,
                                            urma_seg_cfg_t *seg_cfg,
                                            int dmabuf_fd,
                                            uint64_t offset);

urma_status_t urma_query_dmabuf_caps(urma_device_t *dev,
                                     urma_dmabuf_caps_t *caps);
```

```c
/* src/urma/tools/urma_perftest/perftest_memory.h —— 新增（示意） */

struct perftest_memory_ops;

typedef struct perftest_memory_ops {
    int   (*init)(struct perftest_memory_ops *ops, perftest_config_t *cfg);
    void  (*destroy)(struct perftest_memory_ops *ops);
    int   (*alloc_buffer)(struct perftest_memory_ops *ops, uint64_t size,
                          uint64_t alignment,
                          void **out_addr,
                          int *out_dmabuf_fd,
                          uint64_t *out_dmabuf_offset);
    void  (*free_buffer)(struct perftest_memory_ops *ops, void *addr,
                         int dmabuf_fd, uint64_t size);
    void  (*copy_host_to_buffer)(struct perftest_memory_ops *ops, void *dst,
                                 const void *src, size_t n);
    void  (*copy_buffer_to_host)(struct perftest_memory_ops *ops, void *dst,
                                 const void *src, size_t n);
    void  (*copy_buffer_to_buffer)(struct perftest_memory_ops *ops, void *dst,
                                   const void *src, size_t n);
    /* sentinel helpers used by latency tests */
    void  (*write_byte)(struct perftest_memory_ops *ops, void *addr,
                        uint64_t off, uint8_t v);
    uint8_t (*read_byte)(struct perftest_memory_ops *ops, const void *addr,
                         uint64_t off);
    void *priv;
    enum perftest_mem_type type;
} perftest_memory_ops_t;

perftest_memory_ops_t *perftest_host_mem_create(perftest_config_t *);
perftest_memory_ops_t *perftest_cuda_mem_create(perftest_config_t *);
perftest_memory_ops_t *perftest_rocm_mem_create(perftest_config_t *);
perftest_memory_ops_t *perftest_dmabuf_mem_create(perftest_config_t *);
```

---

## 10. 一句话结论

URMA 当前**完全没有**类 GDR 的接口。要追平 RDMA 社区
`perftest --use_cuda --use_dmabuf` 的水平，至少需要：

1. **协议层**：扩展 `urma_seg_cfg + urma_reg_seg_flag + uburma IOCTL` 携带
   `dmabuf_fd / offset / mem_type`；
2. **API 层**：新增 `urma_register_seg_dmabuf` 与 `urma_query_dmabuf_caps`；
3. **驱动层**：HiSilicon udma 与 ubcore 内核侧用 `dma_buf_attach + map_attachment`
   接管"pin 用户页"逻辑；
4. **工具层**：在 `urma_perftest` 中引入 `perftest_memory_ops` 抽象，并实现
   `host / cuda / rocm / 直传 dmabuf` 四个后端，重写所有直接读写
   `local_buf` 的代码点。

参考第 5、6 节 Patch 清单与 9 节示意头文件即可作为下一阶段开发与代码评审的入口。
