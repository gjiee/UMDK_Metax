# URMA — read_lat_gpu / write_lat_gpu 与 GPU 内存自动回退分配链

> 分支: `cursor/urma-gpu-host-proxy-444d`
> 基线: 与上一轮 host-proxy PR 同一分支, 本轮直接在其顶端继续提交
> 目标: 把 host-proxy *融入* urma_perftest 主程序, 新增两条测试命令
>       `read_lat_gpu` / `write_lat_gpu`, 内存分配走
>       **GDR -> peermem -> host-proxy -> memalign** 的自动回退链
> 测试: 5 用例单元测试全部 PASS, 不依赖 URMA / 内核 / CUDA

---

## 1. 这次改了什么 (TL;DR)

| 操作 | 文件 |
| --- | --- |
| **删** | `src/urma/tools/urma_perftest/perftest_gpu_proxy_demo.c` |
| **删** | `src/urma/tools/urma_perftest/Makefile.demo` |
| **删** | `src/urma/tools/urma_perftest/.gitignore` (随 demo 一并失去用途) |
| **新增** | `src/urma/tools/urma_perftest/perftest_gpu_alloc.h` (94 行) |
| **新增** | `src/urma/tools/urma_perftest/perftest_gpu_alloc.c` (~280 行) |
| **新增** | `src/urma/tools/urma_perftest/perftest_gpu_alloc_test.c` (~110 行, 仅测试) |
| **新增** | `src/urma/tools/urma_perftest/Makefile.test` (~30 行, 仅测试) |
| 改 | `perftest_parameters.h`: 新增 `PERFTEST_READ_LAT_GPU` / `PERFTEST_WRITE_LAT_GPU` 与 helper `perftest_cmd_is_gpu()` |
| 改 | `perftest_parameters.c`: 命令字符串注册 + 帮助文本 + `api_type` 映射 + `type` 派生 (从 enum-order ternary 改为显式 switch) |
| 改 | `urma_perftest.c::run_test`: dispatch 两条新 cmd 复用 `run_read_lat` / `run_write_lat` |
| 改 | `perftest_resources.c::register_mem` & `free_memory`: GPU 变种走 `perftest_gpu_alloc_buffer` 与 `perftest_gpu_free_buffer`, 其它分支保持不变 |
| 改 | `perftest_resources.c`: 新增 file-scope `g_perftest_gpu_bufs` 数组追踪 (最小侵入, 不修改 `perftest_context_t`) |
| 改 | `CMakeLists.txt`: 把 `perftest_gpu_alloc.c` 加进 `urma_perftest` 目标 |
| **新增文档** | `docs/urma_gpu_alloc_fallback.md` (本文件) |

总计: 4 个新文件 + 5 个改文件 + 3 个删文件, 全部围绕"加 read_lat_gpu / write_lat_gpu 两个新命令, 只换内存分配, 其余照旧"展开.

---

## 2. 设计思路

### 2.1 用户视角

```bash
# 原来 (上一轮以及更早):
urma_perftest read_lat   -d ubp0 ...
urma_perftest write_lat  -d ubp0 ...

# 现在 (本轮新增, 命令名与参数与上面一一对应):
urma_perftest read_lat_gpu   -d ubp0 ...     # 用 GPU 内存跑 read_lat
urma_perftest write_lat_gpu  -d ubp0 ...     # 用 GPU 内存跑 write_lat
```

只有内存分配方式不同; 其它 CLI 选项 / 主循环 / 数据校验 / 统计打印 全部保留原样.

### 2.2 内存分配的自动回退链

```
perftest_gpu_alloc_buffer(size, align, out)
        |
        v
  +--------+        FAIL (no kernel dma-buf path / register_seg_dmabuf -> ENOTSUP)
  |  GDR   |--------------------------------------------------------------------+
  +--------+                                                                    |
        |                                                                       |
        v                                                                       |
  +---------+        FAIL (no libcuda.so.1 OR no nvidia_peermem.ko / amd_peermem)
  | peermem |--------------------------------------------------------------------+
  +---------+                                                                    |
        |                                                                       |
        v                                                                       |
  +------------+      FAIL (no libcuda.so.1 AND no PERFTEST_GPU_ALLOC_ALLOW_HOST_PROXY_SIM)
  | host-proxy |------------------------------------------------------------------+
  +------------+                                                                  |
        |                                                                         |
        v                                                                         v
  +-----------+    OK                                                       +-----------+
  |  memalign |<-- (fallthrough)                                            |  return 0 |
  +-----------+                                                             +-----------+
```

每一步 try_alloc_* 在 *失败时* 打印一行原因, 在 *成功时* 也打印一行 (含返回的 addr / 大小 / 策略名). 上层 `register_mem` 又打印一行汇总, 用户可一眼看出今天 *实际* 走的是哪一档.

### 2.3 给内核老兵的概念映射

| 用户态分配器层 | 你熟悉的内核同构 |
| --- | --- |
| `try_alloc_gdr`        | `dma_map_resource` + `pci_p2pdma` 路径 |
| `try_alloc_peermem`    | 经过 `peer_memory_client` 注册回调 (nvidia_p2p_get_pages) 的页表 |
| `try_alloc_host_proxy` | `dma_alloc_coherent` 配合 device side 的 bounce 拷贝 |
| `try_alloc_memalign`   | `__get_free_pages` 内核常规页 |
| 自动回退链             | `dma_alloc_attrs()` 在 IOMMU/SWIOTLB/普通页之间的级联尝试 |

---

## 3. 为什么删掉上一轮的 demo

上一轮的 `perftest_gpu_proxy_demo.c` + `Makefile.demo` 是单独的 *进程*, 不走 urma_perftest 主流程. 本轮的目标 (用户原话): "*将 proxy 融入原本的*", 把功能放进 urma_perftest 自身的命令体系里, 因此:

- demo 二进制 + 它的 Makefile 都删除;
- 上一轮 demo 的"演示性"被 *融入测试用例* `perftest_gpu_alloc_test.c`, 它就在同目录跑, 验证回退链而不演示完整 IO 链;
- 用户实际使用就是 `urma_perftest read_lat_gpu` / `urma_perftest write_lat_gpu`, 不再需要任何"demo 程序".

---

## 4. perftest_gpu_alloc API

```c
typedef enum perftest_gpu_alloc_strategy {
    PERFTEST_GPU_ALLOC_NONE       = 0,
    PERFTEST_GPU_ALLOC_GDR        = 1,
    PERFTEST_GPU_ALLOC_PEERMEM    = 2,
    PERFTEST_GPU_ALLOC_HOST_PROXY = 3,
    PERFTEST_GPU_ALLOC_MEMALIGN   = 4,
} perftest_gpu_alloc_strategy_t;

typedef struct perftest_gpu_buffer {
    void                          *addr;            /* va to register_seg */
    size_t                         size;
    int                            dmabuf_fd;       /* >=0 only for GDR */
    uint64_t                       dmabuf_offset;
    void                          *gpu_ptr;         /* HOST_PROXY: simulated GPU buf */
    perftest_gpu_alloc_strategy_t  strategy;
} perftest_gpu_buffer_t;

int  perftest_gpu_alloc_buffer(size_t size, size_t alignment,
                               perftest_gpu_buffer_t *out);
void perftest_gpu_free_buffer(perftest_gpu_buffer_t *buf);

const char *perftest_gpu_alloc_strategy_name(perftest_gpu_alloc_strategy_t s);
void        perftest_gpu_alloc_set_force_fail_mask(unsigned mask);
```

调用方 (urma_perftest 里的 `register_mem`) 只关心 `out->addr`. dmabuf_fd / gpu_ptr / strategy 由 free 路径自动按 strategy 区分释放.

---

## 5. 关键代码片段

### 5.1 register_mem 的新分支

```c
/* perftest_resources.c, in register_mem() */
if (perftest_cmd_is_gpu(cfg->cmd)) {
    if (g_perftest_gpu_bufs == NULL) {
        g_perftest_gpu_bufs = calloc(cfg->jettys, sizeof(*g_perftest_gpu_bufs));
        ...
    }
    fprintf(stderr,
        "%s: GPU variant cmd=%d, dispatching to perftest_gpu_alloc_buffer "
        "(GDR -> peermem -> host-proxy -> memalign)\n", __func__, (int)cfg->cmd);
}

for (i = 0; i < cfg->jettys; i++) {
    if (perftest_cmd_is_gpu(cfg->cmd)) {
        if (i < seg_num) {
            if (perftest_gpu_alloc_buffer(ctx->buf_len, ctx->page_size,
                                          &g_perftest_gpu_bufs[i]) != 0) {
                goto free_memory;
            }
            ctx->local_buf[i] = g_perftest_gpu_bufs[i].addr;
            fprintf(stderr, "%s: jetty=%u strategy=%s addr=%p len=%lu\n",
                    __func__, i,
                    perftest_gpu_alloc_strategy_name(g_perftest_gpu_bufs[i].strategy),
                    g_perftest_gpu_bufs[i].addr, (unsigned long)ctx->buf_len);
        } else {
            ctx->local_buf[i] = ctx->local_buf[0];
        }
    } else if (cfg->use_huge_page) {
        ... (原逻辑保持不变)
    } else {
        ... (原 memalign 逻辑保持不变)
    }
}
```

### 5.2 run_test 的 dispatch

```c
switch (cfg->cmd) {
    case PERFTEST_READ_LAT:
    case PERFTEST_READ_LAT_GPU:    /* 与 READ_LAT 完全相同的主循环 */
        ret = run_read_lat(ctx, cfg);
        break;
    case PERFTEST_WRITE_LAT:
    case PERFTEST_WRITE_LAT_GPU:   /* 与 WRITE_LAT 完全相同的主循环 */
        ret = run_write_lat(ctx, cfg);
        break;
    ...
}
```

### 5.3 type 派生改写

旧:
```c
cfg->type = cfg->cmd > PERFTEST_ATOMIC_LAT ? PERFTEST_BW : PERFTEST_LAT;
```

新 (显式 switch, 避免新 enum 值放在 `*_BW` 后面被误判为 BW):
```c
switch (cfg->cmd) {
    case PERFTEST_READ_BW:
    case PERFTEST_WRITE_BW:
    case PERFTEST_SEND_BW:
    case PERFTEST_ATOMIC_BW:
        cfg->type = PERFTEST_BW;
        break;
    default:
        cfg->type = PERFTEST_LAT;
        break;
}
```

---

## 6. 测试

### 6.1 单元测试 (无依赖, cloud agent 上即可跑)

```bash
$ cd src/urma/tools/urma_perftest
$ make -f Makefile.test
$ ./perftest_gpu_alloc_test
```

5 个用例:

| # | 场景 | force_mask | 期望落点 | 实测 |
| ---: | --- | --- | --- | --- |
| 1 | 自然环境 (无 GDR / 无 peermem / 无 libcuda) | 0 | MEMALIGN | PASS |
| 2 | 打开 host-proxy sim 模式 | 0 | HOST_PROXY | PASS |
| 3 | 强制 host-proxy 失败 | `1<<HOST_PROXY` | MEMALIGN | PASS |
| 4 | 强制 GDR/peermem/host-proxy 全失败 | `0xE` | MEMALIGN | PASS |
| 5 | 清掩码 + sim 还开着, 回到 HOST_PROXY | 0 | HOST_PROXY | PASS |

终行: `[gpu-alloc-test] ALL CASES PASSED`, exit 0.

### 6.2 真实 urma_perftest 跑法

```bash
# server 端
urma_perftest read_lat_gpu -d <urma_dev>

# client 端
urma_perftest read_lat_gpu -d <urma_dev> <server_ip>
```

期望日志中会出现 (节选):

```
register_mem: GPU variant cmd=8, dispatching to perftest_gpu_alloc_buffer (GDR -> peermem -> host-proxy -> memalign)
[gpu-alloc] perftest_gpu_alloc_buffer: requested size=8192 align=4096 force_fail_mask=0x0
[gpu-alloc] try_alloc_gdr: FAIL: kernel dma-buf path not wired
[gpu-alloc] try_alloc_peermem: FAIL: libcuda.so.1 not present
[gpu-alloc] try_alloc_host_proxy: FAIL: libcuda.so.1 not present and PERFTEST_GPU_ALLOC_ALLOW_HOST_PROXY_SIM!=1
[gpu-alloc] try_alloc_memalign: OK: addr=0x... size=8192 (plain host memory fallback)
[gpu-alloc] perftest_gpu_alloc_buffer: DONE via MEMALIGN
register_mem: jetty=0 strategy=MEMALIGN addr=0x... len=8192
```

之后的 latency 主循环输出与 `read_lat` 完全相同.

---

## 7. 兼容性 / 未来改造

- 真接入 CUDA / ROCm: 改 `try_alloc_peermem` 与 `try_alloc_host_proxy`, 把 `dlopen("libcuda.so.1")` 后的真 `cuMemAlloc` / `cuMemAllocHost` 加上, 上层 API 不动.
- 真启用 GDR: `urma_register_seg_dmabuf` 的 BYPASS 解除 (内核 ubcore + udma 落 dma-buf 路径), `try_alloc_gdr` 改为真分配. CLI 不变.
- 加 `--gdr_mode={auto,direct,proxy,off}` 与 `--gpu_dev_id`: 文档化在 `docs/urma_gdr_analysis.md` §5.4, 当前命令行未启用 (user: "额外的参数不用管").

---

## 8. 改动统计

```
src/urma/tools/urma_perftest/perftest_gpu_alloc.h          | NEW    94 行
src/urma/tools/urma_perftest/perftest_gpu_alloc.c          | NEW   280 行
src/urma/tools/urma_perftest/perftest_gpu_alloc_test.c     | NEW   110 行
src/urma/tools/urma_perftest/Makefile.test                 | NEW    30 行
docs/urma_gpu_alloc_fallback.md                            | NEW   (本文件)

src/urma/tools/urma_perftest/perftest_parameters.h         |  +20 行
src/urma/tools/urma_perftest/perftest_parameters.c         |  +25 / -1 行
src/urma/tools/urma_perftest/urma_perftest.c               |   +2 行
src/urma/tools/urma_perftest/perftest_resources.c          |  +60 / -10 行
src/urma/tools/urma_perftest/CMakeLists.txt                |   +5 行

src/urma/tools/urma_perftest/perftest_gpu_proxy_demo.c     | DELETE
src/urma/tools/urma_perftest/Makefile.demo                 | DELETE
src/urma/tools/urma_perftest/.gitignore                    | DELETE
```

最小代码原则严守: 只改"内存分配", 不动 run_*_lat 主循环、不动 CLI 解析以外的参数处理、不动 perftest_context_t 结构 (用 file-scope 数组追踪 GPU buffer 描述符).
