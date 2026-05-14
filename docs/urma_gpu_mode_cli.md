# URMA — `--gpu-mode` CLI 与 GPU 内存路径教程

> 分支: `cursor/urma-gpu-mode-cli-444d`
> 基线: `cursor/urma-gpu-host-proxy-444d` (上一轮已落地的 read_lat_gpu / write_lat_gpu 与回退分配链)
> 目标: 用 *显式 CLI 参数* `--gpu-mode={proxy,peermem,direct}` 选择 3 种 GPU 内存分配入口; 默认 (不加) = memalign, 行为与原 `read_lat` / `write_lat` 完全一致
> 加入: 真正的 CUDA Driver API 封装 (`perftest_cuda_loader`), linux-rdma 风格的数据校验 (`perftest_validation`), 以及 `--gpu-mode=proxy` 下用 `cuMemcpyDtoH/HtoD` 做 D2H/H2D staging 的接口

---

## 1. 本轮改动总览

| 操作 | 文件 |
| --- | --- |
| **新增** | `src/urma/tools/urma_perftest/perftest_cuda_loader.h` (~90 行) |
| **新增** | `src/urma/tools/urma_perftest/perftest_cuda_loader.c` (~290 行): `dlopen(libcuda.so.1)` + 14 个 driver-API 包装 |
| **新增** | `src/urma/tools/urma_perftest/perftest_validation.h` (~50 行) |
| **新增** | `src/urma/tools/urma_perftest/perftest_validation.c` (~120 行): host / GPU 两套 `fill_pattern` / `check_pattern` |
| **新增文档** | `docs/urma_gpu_mode_cli.md` (本文件) |
| 改 | `perftest_gpu_alloc.h` / `.c`: 新增 `perftest_gpu_mode_t` 枚举 + `set_mode`/`get_mode` / `mode_from_string`; `try_alloc_*` 改成调真 CUDA driver; 重写顶层 dispatcher 改为按 `--gpu-mode` 选档 + memalign fallback |
| 改 | `perftest_gpu_alloc.h` / `.c`: 新增 `perftest_gpu_stage_to_nic` / `perftest_gpu_stage_from_nic` (D2H / H2D 工具函数, 真 CUDA 用 `cuMemcpyDtoH/HtoD`, 无 CUDA 时退化为 memcpy) |
| 改 | `perftest_parameters.h` / `.c`: 加 `cfg->gpu_mode` 字段, `PERFTEST_OPT_GPU_MODE` 长选项, `--gpu-mode` 解析与 usage 帮助 |
| 改 | `perftest_resources.c::register_mem`: `perftest_gpu_set_mode(cfg->gpu_mode)` 在 alloc 前调一次 |
| 改 | `CMakeLists.txt`: 加 cuda_loader / validation 源文件, link `-ldl` |
| 改 | `perftest_gpu_alloc_test.c` / `Makefile.test`: 用新 set_mode 写 6 条 case + 解析 sanity, libc-only 仍 PASS |

---

## 2. 用户视角 — 怎么跑

### 2.1 默认 (不加 `--gpu-mode`)

```bash
urma_perftest read_lat_gpu  -d <urma_dev> [server_ip]
urma_perftest write_lat_gpu -d <urma_dev> [server_ip]
```

行为: `register_mem` 看 `cfg->gpu_mode == NONE`, 直接 `memalign` → URMA `register_seg` 主机内存. 与现存的 `read_lat` / `write_lat` 完全等价, 只是命令名带 `_gpu` 后缀.

### 2.2 Host Proxy 模式 (无 GDR / 无 peermem 也能跑 GPU 内存)

```bash
urma_perftest read_lat_gpu --gpu-mode=proxy -d ubp0 [server_ip]
```

内部流:

```
cuInit / cuCtxCreate (libcuda)
cuMemAlloc(&gpu_ptr, len)
cuMemAllocHost(&shadow, len)     <-- URMA register_seg 注册的是 shadow
... 测试主循环 ...
   perftest_gpu_stage_to_nic   (cuMemcpyDtoH gpu_ptr -> shadow) 在 post 之前
   urma_post_jfs_wr
   poll_jfc
   perftest_gpu_stage_from_nic (cuMemcpyHtoD shadow -> gpu_ptr) 在 CR 之后
```

注: 上述 `stage_to_nic` / `stage_from_nic` API 已经提供, 但本轮**没有**强行插进 `run_read_lat` / `run_write_lat` 主循环 (用户原话: 最小代码原则). 上层 patch 在落 IO loop 时一行调用即可.

### 2.3 PeerMem 模式 (NVIDIA nvidia_peermem)

```bash
modprobe nvidia_peermem        # 或 amd_peermem
urma_perftest read_lat_gpu --gpu-mode=peermem -d ubp0 [server_ip]
```

内部流:

```
cuMemAlloc(&dptr, len)
URMA register_seg(va=dptr ...)
    └─ kernel ubcore -> peer_memory_client -> nvidia_p2p_get_pages -> NIC MTT
NIC DMA reads/writes GPU memory directly (no host bounce)
```

### 2.4 GDR Direct 模式 (dma-buf)

```bash
urma_perftest read_lat_gpu --gpu-mode=direct -d ubp0 [server_ip]
```

内部流:

```
cuMemAlloc(&dptr, len)
cuMemGetHandleForAddressRange(&fd, dptr, len, DMA_BUF_FD)
urma_register_seg_dmabuf(ctx, &cfg, fd, off)
    └─ kernel ubcore -> dma_buf_attach + dma_buf_map_attachment -> NIC MTT
```

### 2.5 拼写错误 / 不支持的模式

`--gpu-mode=foo` → CLI parser 立刻报错并返回 -1, 进程退出. 不会"静默走默认".

---

## 3. 环境依赖 (Tutorial)

### 3.1 libcuda 是 dlopen 的, 编译时不需要

`perftest_cuda_loader.c` 通过 `dlopen("libcuda.so.1", RTLD_LAZY)` 在运行时拿 CUDA Driver API 符号. **编译 urma_perftest 时不需要 CUDA SDK 头, 也不会去 link `-lcuda`**. CMakeLists 只加了 `-ldl`.

这意味着:

- 在没有 NVIDIA driver 的机器上, urma_perftest *可以正常编译, 可以正常运行*; 但是 `--gpu-mode=proxy/peermem/direct` 走到 cuda_loader_init 时会 `dlopen` 失败, 然后该模式失败 → fallback memalign;
- 在有 driver 的机器上, 同一个二进制就直接拿到真 CUDA, 不需要重编.

### 3.2 各模式所需的实际依赖

| 模式 | libcuda.so.1 | CUDA Driver 版本 | nvidia_peermem (ko) | 内核 ubcore dma-buf 支持 |
| --- | :---: | --- | :---: | :---: |
| `none` (默认) | 不需要 | — | 不需要 | 不需要 |
| `proxy` | **需要** | 任意 (建议 >= 11) | 不需要 | 不需要 |
| `peermem` | **需要** | 任意 | **需要** | 不需要 |
| `direct` | **需要** | **>= 12.2** (需要 `cuMemGetHandleForAddressRange`) | 不需要 | **需要** |

### 3.3 Ubuntu / openEuler 一键命令

Ubuntu 22.04 / 24.04:
```bash
sudo apt update
sudo apt install -y nvidia-driver-535             # 提供 libcuda.so.1
sudo apt install -y nvidia-peer-memory-dkms       # 提供 nvidia_peermem.ko
ldconfig -p | grep libcuda.so.1                   # 验证 libcuda 可见
sudo modprobe nvidia_peermem                       # peermem 模式必需
lsmod | grep nvidia_peermem
```

openEuler 22.03 / 24.03:
```bash
sudo dnf install -y nvidia-driver-libs            # 来自 NVIDIA CUDA repo
sudo dnf install -y dkms kernel-devel
# nvidia_peermem 与 NVIDIA OFED 一起发布:
sudo dnf install -y nvidia-peer-memory
sudo modprobe nvidia_peermem
```

### 3.4 确认 driver 版本是否 >= 12.2 (--gpu-mode=direct 必需)

```bash
nvidia-smi | grep "CUDA Version"
# 输出: CUDA Version: 12.4   ✓
```

或在程序内: cuda_loader_init 时会打一行 `[cuda-loader] perftest_cuda_loader_init: loaded libcuda.so.1, driver version=12.4`.

---

## 4. 关键 API 与 caller 集成点

### 4.1 `perftest_cuda_loader.h` — CUDA Driver API 包装

```c
int  perftest_cuda_loader_init(void);          /* dlopen + dlsym */
void perftest_cuda_loader_fini(void);
int  perftest_cuda_is_loaded(void);

int  perftest_cuda_ctx_create(int dev_id);     /* cuInit/cuDeviceGet/cuCtxCreate */
void perftest_cuda_ctx_destroy(void);

int  perftest_cuda_mem_alloc       (size_t, void **);   /* cuMemAlloc */
int  perftest_cuda_mem_free        (void *);            /* cuMemFree */
int  perftest_cuda_mem_alloc_host  (size_t, void **);   /* cuMemAllocHost */
int  perftest_cuda_mem_free_host   (void *);            /* cuMemFreeHost */
int  perftest_cuda_memcpy_d2h      (void *host, void *dev, size_t n);  /* cuMemcpyDtoH */
int  perftest_cuda_memcpy_h2d      (void *dev, const void *host, size_t n);  /* cuMemcpyHtoD */
int  perftest_cuda_get_dmabuf_fd   (void *dptr, size_t size,
                                    int *out_fd, uint64_t *out_offset); /* DMA_BUF_FD */
```

linux-rdma 项目里这些就是 `src/cuda_loader.c` 的内容, 我们等价镜像.

### 4.2 `perftest_validation.h` — pattern fill / check

```c
void   perftest_validation_fill_host  (void *buf,  size_t size, uint32_t seed);
size_t perftest_validation_check_host (const void *buf, size_t size, uint32_t seed);
int    perftest_validation_fill_gpu   (void *dptr, size_t size, uint32_t seed);
long   perftest_validation_check_gpu  (void *dptr, size_t size, uint32_t seed);
```

GPU 变种内部走 `perftest_cuda_memcpy_h2d / cuMemcpyDtoH`, 与 linux-rdma `cuda_memory_copy_host_buffer` 思路一致.

### 4.3 `perftest_gpu_alloc.h` — 新 API

```c
typedef enum {
    PERFTEST_GPU_MODE_NONE = 0, PERFTEST_GPU_MODE_PROXY,
    PERFTEST_GPU_MODE_PEERMEM,   PERFTEST_GPU_MODE_DIRECT,
} perftest_gpu_mode_t;

void                perftest_gpu_set_mode(perftest_gpu_mode_t);
perftest_gpu_mode_t perftest_gpu_get_mode(void);
const char         *perftest_gpu_mode_name(perftest_gpu_mode_t);
int                 perftest_gpu_mode_from_string(const char *, perftest_gpu_mode_t *);

int  perftest_gpu_alloc_buffer(size_t size, size_t align, perftest_gpu_buffer_t *);
void perftest_gpu_free_buffer (perftest_gpu_buffer_t *);

int  perftest_gpu_stage_to_nic  (perftest_gpu_buffer_t *, size_t len);
int  perftest_gpu_stage_from_nic(perftest_gpu_buffer_t *, size_t len);
```

### 4.4 集成点 (caller)

唯一改动点位于 `perftest_resources.c::register_mem`:

```c
if (perftest_cmd_is_gpu(cfg->cmd)) {
    ...
    perftest_gpu_set_mode((perftest_gpu_mode_t)cfg->gpu_mode);
    fprintf(stderr,
        "%s: GPU variant cmd=%d gpu-mode=%s -> perftest_gpu_alloc_buffer\n",
        __func__, (int)cfg->cmd,
        perftest_gpu_mode_name((perftest_gpu_mode_t)cfg->gpu_mode));
}
```

主流程 `run_read_lat` / `run_write_lat` 一行未改; `perftest_gpu_stage_to_nic / from_nic` 由后续 patch 决定哪个粒度插进 IO 主循环 (per-WR 还是 per-batch).

---

## 5. 测试

### 5.1 最小化单元测试 (cloud agent VM 上即跑)

```bash
$ cd src/urma/tools/urma_perftest
$ make -f Makefile.test
$ ./perftest_gpu_alloc_test
```

6 case + 字符串解析 sanity check, 全部 PASS (本 PR 实测), exit 0.

```
[gpu-alloc-test] run_case: mode=NONE  -> memalign (default behaviour)
[gpu-alloc-test] run_case: mode=PROXY without libcuda/sim -> memalign fallback
[gpu-alloc-test] run_case: mode=PROXY with sim -> HOST_PROXY
[gpu-alloc-test] run_case: mode=PROXY + force HOST_PROXY fail -> memalign
[gpu-alloc-test] run_case: mode=PEERMEM (no cuda) -> memalign fallback
[gpu-alloc-test] run_case: mode=DIRECT (no cuda) -> memalign fallback
[gpu-alloc-test] ALL CASES PASSED
```

### 5.2 真实 urma_perftest

按 §2 与 §3 的环境准备, 然后:

```bash
# server
urma_perftest read_lat_gpu --gpu-mode=proxy -d ubp0
# client
urma_perftest read_lat_gpu --gpu-mode=proxy -d ubp0 <server_ip>
```

预期日志中能看到:

```
[gpu-alloc] perftest_gpu_set_mode: mode = proxy
[cuda-loader] perftest_cuda_loader_init: loaded libcuda.so.1, driver version=12.4
[cuda-loader] perftest_cuda_ctx_create: ok: dev_id=0 name="NVIDIA H100 PCIe"
[cuda-loader] perftest_cuda_mem_alloc: ok: dptr=0x7f... size=8192
[cuda-loader] perftest_cuda_mem_alloc_host: ok: hptr=0x7f... size=8192 (pinned)
[gpu-alloc] try_alloc_host_proxy: OK: gpu_ptr=0x7f... shadow=0x7f... size=8192 have_cuda=1
[gpu-alloc] perftest_gpu_alloc_buffer: DONE via HOST_PROXY
register_mem: GPU variant cmd=8 gpu-mode=proxy -> perftest_gpu_alloc_buffer
...
```

---

## 6. 给内核老兵的概念映射

| 用户态层 | 你熟悉的内核同构 |
| --- | --- |
| `dlopen("libcuda.so.1")` | `request_module + symbol_get` 的用户态对应 |
| `cuMemAlloc` | NVIDIA driver 内部 `nvidia_p2p_alloc` / GPU 页表分配 |
| `cuMemAllocHost` (pinned) | `dma_alloc_coherent` / `kmalloc(GFP_DMA)` |
| `cuMemcpyDtoH` / `HtoD` | `dma_sync_single_for_cpu / _for_device` 风格的 staging |
| `cuMemGetHandleForAddressRange(DMA_BUF_FD)` | `dma_buf_export()` 等价 |
| peer_memory_client (peermem 模式) | 内核 RDMA `ib_umem` 的 peer-memory 回调 |

---

## 7. 与上一轮的差异

| 维度 | 上一轮 (`cursor/urma-gpu-host-proxy-444d`) | 本轮 (`cursor/urma-gpu-mode-cli-444d`) |
| --- | --- | --- |
| 分配策略选择 | 无条件 `GDR → peermem → host_proxy → memalign` 全链尝试 | 由 `--gpu-mode` 显式选 1 档; 失败 fallback memalign |
| 默认行为 (无 `--gpu-mode`) | 自动尝试整个链 | **直接 memalign**, 与原 `read_lat` 一致 |
| CUDA 调用 | host-proxy 用 `memalign` 模拟 GPU 内存 | 提供 `perftest_cuda_loader` 真接 `cuMemAlloc / cuMemAllocHost / cuMemcpyD2H/H2D / cuMemGetHandleForAddressRange` |
| Validation | 无 | `perftest_validation` 提供 linux-rdma 风格 fill / check (host + GPU) |
| 链接 | 不动 | 加 `-ldl` (libcuda 仍是 dlopen, 不强依赖) |
| Demo / Test | 无独立 demo, 有 5 case 单元测试 | 仍无独立 demo, 单元测试扩到 6 case + 字符串 sanity |

---

## 8. 改动统计

```
src/urma/tools/urma_perftest/perftest_cuda_loader.h          | NEW  90 行
src/urma/tools/urma_perftest/perftest_cuda_loader.c          | NEW 290 行
src/urma/tools/urma_perftest/perftest_validation.h           | NEW  50 行
src/urma/tools/urma_perftest/perftest_validation.c           | NEW 120 行
docs/urma_gpu_mode_cli.md                                    | NEW (本文件)

src/urma/tools/urma_perftest/perftest_gpu_alloc.h            |  +60 行
src/urma/tools/urma_perftest/perftest_gpu_alloc.c            | +170 / -80 行
src/urma/tools/urma_perftest/perftest_parameters.h           |  +15 行
src/urma/tools/urma_perftest/perftest_parameters.c           |  +35 行
src/urma/tools/urma_perftest/perftest_resources.c            |  +10 / -5 行
src/urma/tools/urma_perftest/perftest_gpu_alloc_test.c       | rewrite (~80 行)
src/urma/tools/urma_perftest/Makefile.test                   |  +1 行
src/urma/tools/urma_perftest/CMakeLists.txt                  |  +10 行
```

最小代码原则严守: 内存分配以外的逻辑一行未改, 主循环 `run_read_lat` / `run_write_lat` 不动, ABI 不动.
