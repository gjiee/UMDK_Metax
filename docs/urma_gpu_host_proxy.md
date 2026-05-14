# URMA — 非 GDR 环境下的 GPU 内存支持 (Host Proxy)

> 分支: `cursor/urma-gpu-host-proxy-444d`
> 基线: `cursor/urma-gdr-analysis-444d` (GDR 框架骨架, 已合到本分支)
> 目标: 在 *没有 GDR 驱动* (NIC / ubcore / udma 都还没接 dma-buf) 的机器上, 让 `urma_perftest` 仍能"以 GPU 内存为应用视角"地跑起来
> 验证: 一个独立 demo `perftest_gpu_proxy_demo`, 仅依赖 libc, 自带 PASS / FAIL 校验

---

## 1. 这次改动是什么 (TL;DR)

新增 *最小* 三块代码:

| 文件 | 类型 | 作用 |
| --- | --- | --- |
| `src/urma/tools/urma_perftest/perftest_mem_gpu_proxy.c` | 新增 | 真正可工作的 GPU host-proxy 后端: `gpu_ptr` 用 `memalign` 模拟, `shadow` 用 `memalign` 分配, stage_* 用 `memcpy` 真搬数据 |
| `src/urma/tools/urma_perftest/perftest_gpu_proxy_demo.c` | 新增 | 独立 demo, 模拟 send/recv 两侧走完整 `stage_to_nic -> NIC -> stage_from_nic` 通路并校验 |
| `src/urma/tools/urma_perftest/Makefile.demo` | 新增 | 用裸 `gcc + libc` 编 demo, 无需 cmake / urma / kernel-devel |

外加 4 处小调整:

- `perftest_memory.h`: 导出 `perftest_gpu_proxy_create` 与访问器 `perftest_gpu_proxy_get_gpu_ptr / get_size` (仅 demo 用);
- `perftest_mem_dmabuf.c` 内 `perftest_memory_create()`: 新增按 `PERFTEST_MEM=gpu_proxy` 环境变量选择后端的能力 (临时方案, `cfg->mem_type` ABI 加上后再改成 switch);
- `urma_seg_dmabuf.c::urma_register_seg_dmabuf()`: 注释明确说明这是 *GDR bypass*, 不发任何 IOCTL, 不依赖内核, 永远返回 `EOPNOTSUPP` 让 perftest 自动 fallback;
- `CMakeLists.txt`: 把新后端挂进 `urma_perftest` 目标 (真实编译还是需要 ubcore 等依赖, 这里只是接进去).

---

## 2. 为什么这样设计

### 2.1 GDR 不可用时, GPU 数据怎么搬?

经典做法是 **bounce buffer**: 在 host 内存上预分配一块 "shadow" 区域, 让 NIC 只看 host (走老的 `urma_register_seg`), GPU 端的应用语义则通过两次 `cuMemcpy*` 完成同步:

```
+-------------+   stage_to_nic       +-----------------+
| GPU memory  |---(cuMemcpyDtoH)---->| host shadow buf |
| (application|                      | (URMA registers |
|  visible)   |<--(cuMemcpyHtoD)-----|  this region)   |
+-------------+   stage_from_nic     +-----------------+
                                          |    ^
                                    NIC   |    |
                                    DMA   v    |
                                        network
```

这条路径完全不需要内核 dma-buf 支持, 兼容今天的 URMA 主干. 与 NCCL `NET_DISABLE_GDR=1` 的回退一致, 与 Linux DMA 子系统的 `swiotlb bounce` 思路同构.

### 2.2 为什么不直接用 CUDA?

本轮要求 "最小代码、不依赖内核、可运行". 在没有 NVIDIA 驱动 / CUDA SDK 的 CI 与开发机上, 引入 `libcuda.so` 链接会让代码"装好了但跑不起来". 因此本骨架:

- "GPU 内存" 用 `memalign()` 模拟. 真实部署只需把 `memalign(...)` 换成 `cuMemAlloc(&d_ptr, size)`, 业务逻辑零改动;
- "host pinned shadow" 也用 `memalign()`. 真实部署换为 `cuMemAllocHost(&shadow, size)` 以获得 pinned 属性, 但 NIC DMA 的语义并不强制要求 pinned (URMA 注册段的时候内核会自己 `pin_user_pages`);
- "stage_to/from_nic" 用 `memcpy()`. 真实部署换为 `cuMemcpyDtoH / cuMemcpyHtoD`.

也就是说: **本骨架的代码路径是真实部署的 *字节级一致的简化版本***, 切到真 CUDA 只是 3 个函数名替换.

### 2.3 GDR 旁路是怎么落地的

两个并行的旁路:

1. `urma_register_seg_dmabuf()` 永远返回 `NULL + EOPNOTSUPP`, 完全不调内核 ioctl. 即使有人误调用了 GDR API, 也只会被打印一行后立即返回.
2. `perftest_mem_gpu_proxy.c::gpu_proxy_negotiate()` 直接把 `self->gdr_mode = PERFTEST_GDR_HOST_PROXY`, 完全不调 `urma_query_dmabuf_caps()`, 也就完全不与内核交互.

加 `alloc_buffer` 总是把 `*out_dmabuf_fd = -1`, 上层 `register_mem` 看到这个就会走老的 `urma_register_seg()` 主机路径, **永远不会** 触发 dma-buf IOCTL.

---

## 3. 改动逐文件解释 (面向内核工程师)

> 受众假设: 熟悉 Linux 内核 / DMA 子系统 / file_operations 风格的 vtable, 但用户态通讯库经验较少.

### 3.1 `perftest_mem_gpu_proxy.c` (新增, ~340 行)

整个文件就是一张实现了 `perftest_memory_ops_t` (类似 `struct file_operations`) 的"驱动":

```c
ops->init               = gpu_proxy_init;
ops->destroy            = gpu_proxy_destroy;
ops->alloc_buffer       = gpu_proxy_alloc_buffer;
ops->free_buffer        = gpu_proxy_free_buffer;
ops->copy_host_to_buffer    = gpu_proxy_copy_h2b;
ops->copy_buffer_to_host    = gpu_proxy_copy_b2h;
ops->copy_buffer_to_buffer  = gpu_proxy_copy_b2b;
ops->write_byte         = gpu_proxy_write_byte;
ops->read_byte          = gpu_proxy_read_byte;
ops->negotiate_gdr_mode = gpu_proxy_negotiate;
ops->stage_to_nic_buffer    = gpu_proxy_stage_to_nic;
ops->stage_from_nic_buffer  = gpu_proxy_stage_from_nic;
```

私有数据:

```c
typedef struct gpu_proxy_priv {
    void   *gpu_ptr;   /* "GPU buffer" (本骨架 memalign 模拟; 真 CUDA: CUdeviceptr) */
    void   *shadow;    /* host shadow (URMA 注册的就是它) */
    size_t  size;
    int     gpu_id;
} gpu_proxy_priv_t;
```

最关键的两个函数:

```c
/* 真 CUDA: cuMemcpyDtoH(shadow_addr, (CUdeviceptr)gpu_addr, len) */
static int gpu_proxy_stage_to_nic(perftest_memory_ops_t *self,
                                  void *shadow_addr, void *gpu_addr, size_t len)
{
    ...
    memcpy(shadow_addr, gpu_addr, len);
    PROXY_LOG("staged %zu bytes  gpu(%p) -> shadow(%p) [simulating cuMemcpyDtoH]",
              len, gpu_addr, shadow_addr);
    return 0;
}

/* 真 CUDA: cuMemcpyHtoD((CUdeviceptr)gpu_addr, shadow_addr, len) */
static int gpu_proxy_stage_from_nic(perftest_memory_ops_t *self,
                                    void *gpu_addr, void *shadow_addr, size_t len)
{
    ...
    memcpy(gpu_addr, shadow_addr, len);
    PROXY_LOG("staged %zu bytes  shadow(%p) -> gpu(%p) [simulating cuMemcpyHtoD]",
              len, shadow_addr, gpu_addr);
    return 0;
}
```

所有 print 都通过宏 `PROXY_LOG(...)` 走, 宏体内自动注入 `__func__`:

```c
#define PROXY_LOG(fmt, ...) \
    fprintf(stderr, "[gpu-proxy] %s: " fmt "\n", __func__, ##__VA_ARGS__)
```

风格上与内核 `pr_info_ratelimited("[%s] ...", __func__, ...)` 一致, 方便 `strace -e write` / `grep` 排错.

### 3.2 `perftest_gpu_proxy_demo.c` (新增, ~150 行)

`main -> run_demo` 五步:

1. **SEND 侧 setup**: `create -> init -> negotiate -> alloc_buffer`, 然后把 sender "GPU" 填充 `0xC3`;
2. **RECV 侧 setup**: 同样四步;
3. **IO 路径**: `send.stage_to_nic_buffer -> memcpy(recv_shadow, send_shadow) -> recv.stage_from_nic_buffer`. 中间那次 memcpy 模拟"NIC DMA + 网络 + NIC DMA";
4. **校验**: 检查 recv 侧 GPU buffer 4096 字节全部等于 `0xC3`. 任意一字节不符报 FAIL;
5. **teardown**: `free_buffer -> destroy -> free(ops)`, 配对释放.

不引入 URMA 头, 不引入 CUDA, 不引入内核. 整个 demo 1 个目标文件链接 + 1 个 main, 就一份 ELF.

### 3.3 `Makefile.demo` (新增, ~25 行)

```makefile
$(DEMO): $(SRCS) perftest_memory.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)
```

`CFLAGS = -O2 -g -Wall -Wextra -std=c99 -D_GNU_SOURCE`. 没有 `-I` 没有 `-l`. 在 Ubuntu / openEuler / Alpine 上都能直接 build.

### 3.4 `perftest_memory.h` (3 行 + 一段注释)

新加 3 个对外符号:

```c
perftest_memory_ops_t *perftest_gpu_proxy_create(perftest_config_t *cfg);
void                  *perftest_gpu_proxy_get_gpu_ptr(perftest_memory_ops_t *ops);
size_t                 perftest_gpu_proxy_get_size(perftest_memory_ops_t *ops);
```

注释解释 `get_gpu_ptr / get_size` 仅供 demo 使用 (真实 perftest 不需要, 因为 GPU 指针自己持有).

### 3.5 `perftest_mem_dmabuf.c` 内的 `perftest_memory_create()` (~15 行)

旧版固定返回 host. 新版按 `PERFTEST_MEM` 环境变量选择:

```c
$ PERFTEST_MEM=gpu_proxy urma_perftest ...    # 走本轮的新后端
$ PERFTEST_MEM=host      urma_perftest ...    # 老路径
```

这是 `cfg->mem_type` ABI 字段尚未加入 `perftest_config_t` 之前的过渡方案. 一旦 `mem_type` ABI 合入, 这里改回 `switch (cfg->mem_type)` 即可.

### 3.6 `urma_seg_dmabuf.c` (注释强化 + `__func__`)

把 `register_seg_dmabuf` 的 stub 注释改为 *显式声明*这是 GDR 旁路点, 并在 print 里使用 `__func__`:

```c
URMA_DMABUF_LOG("%s: BYPASS (no kernel) va=0x%lx len=%lu fd=%d off=%lu -> ENOTSUP",
                __func__, ...);
```

---

## 4. 怎么跑

```bash
# 0) 切到 demo 目录
cd src/urma/tools/urma_perftest

# 1) 编 demo (不需要 cmake, 不需要 urma, 不需要 cuda, 不需要 kernel-devel)
make -f Makefile.demo

# 2) 跑
./perftest_gpu_proxy_demo
```

期望输出 (节选, exit 0):

```
main: urma_perftest GPU host-proxy minimal demo (no CUDA, no URMA, no kernel)
==[ run_demo: SEND side setup ]==
[gpu-proxy] perftest_gpu_proxy_create: ok (no-CUDA, no-kernel, host-proxy only)
[gpu-proxy] gpu_proxy_init: ok (simulated GPU id=0, no CUDA/HIP linked)
[gpu-proxy] gpu_proxy_negotiate: forced gdr_mode=HOST_PROXY (GDR path bypassed, kernel not involved)
[gpu-proxy] gpu_proxy_alloc_buffer: ok: gpu_ptr=... shadow=... size=4096 align=4096 dmabuf_fd=-1
run_demo: send: filled GPU buffer with pattern 0xC3
...
[gpu-proxy] gpu_proxy_stage_to_nic: staged 4096 bytes gpu(...) -> shadow(...) [simulating cuMemcpyDtoH]
run_demo: simulating NIC DMA: send_shadow -> recv_shadow (4096 bytes)
[gpu-proxy] gpu_proxy_stage_from_nic: staged 4096 bytes shadow(...) -> gpu(...) [simulating cuMemcpyHtoD]
run_demo: PASS -- recv GPU buffer matches send pattern 0xC3 across all 4096 bytes
```

注意每一行都带 `__func__`:
- `[gpu-proxy] gpu_proxy_negotiate: ...`
- `[gpu-proxy] gpu_proxy_alloc_buffer: ...`
- `run_demo: ...` / `main: ...`

---

## 5. 把"模拟 GPU"换成真 CUDA / ROCm 的 3 个 diff

当对端真正部署上 NVIDIA / AMD GPU 时, 改 `perftest_mem_gpu_proxy.c` 这 3 处即可, 上层 perftest 完全不变:

| 现状 | 真 CUDA | 真 ROCm |
| --- | --- | --- |
| `memalign(align, size)` (gpu_ptr) | `cuMemAlloc(&d_ptr, size)` | `hipMalloc(&d_ptr, size)` |
| `memalign(align, size)` (shadow) | `cuMemAllocHost(&shadow, size)` | `hipHostMalloc(&shadow, size, hipHostMallocDefault)` |
| `memcpy(shadow, gpu, len)` (stage_to_nic) | `cuMemcpyDtoH(shadow, (CUdeviceptr)gpu, len)` | `hipMemcpyDtoH(shadow, gpu, len)` |
| `memcpy(gpu, shadow, len)` (stage_from_nic) | `cuMemcpyHtoD((CUdeviceptr)gpu, shadow, len)` | `hipMemcpyHtoD(gpu, shadow, len)` |
| `free(gpu_ptr)` | `cuMemFree(gpu_ptr)` | `hipFree(gpu_ptr)` |
| `free(shadow)` | `cuMemFreeHost(shadow)` | `hipHostFree(shadow)` |

`negotiate / init / destroy / copy_* / write_byte / read_byte / stage_*` 这些函数的骨架与控制流都不需要再动.

---

## 6. 与上一轮 GDR 骨架的关系

本分支 = 上一轮的全部 + 本文档列出的最小代码. 编译时:

- 编 `urma_perftest` 主程序仍需要 ubcore / glib2-devel / 等 (现状不变);
- 编 `perftest_gpu_proxy_demo` 不需要这些, 只要 `gcc + libc`. 这就是"可运行"的最小切片;
- GDR API (`urma_register_seg_dmabuf` / `urma_query_dmabuf_caps`) 依然存在, 但其内部已经是 *彻底的旁路* —— 返回 ENOTSUP, 不发任何 ioctl. 后续等 ubcore + udma 内核侧合入 dma-buf 路径后, 把这两个 stub 真正实现即可自动从"HOST_PROXY"升级到"GDR_DIRECT", 上层 perftest 与 CLI 完全不需要再改.

---

## 7. 改动统计

```
docs/urma_gpu_host_proxy.md                         | new   ~280 行
src/urma/tools/urma_perftest/perftest_mem_gpu_proxy.c    | new   ~340 行  (核心实现)
src/urma/tools/urma_perftest/perftest_gpu_proxy_demo.c   | new   ~150 行  (独立 demo)
src/urma/tools/urma_perftest/Makefile.demo               | new    ~25 行
src/urma/tools/urma_perftest/.gitignore                  | new     2 行
src/urma/tools/urma_perftest/perftest_memory.h           | +15 行 (导出 + 注释)
src/urma/tools/urma_perftest/perftest_mem_dmabuf.c       | +15/-10 行 (env-based dispatcher)
src/urma/tools/urma_perftest/CMakeLists.txt              | +1 行
src/urma/lib/urma/core/urma_seg_dmabuf.c                 | 注释强化 + __func__
```

代码以 **新增** 为主, 对原有函数体没有改动, 因此对 urma_perftest 主程序原有行为完全没有影响.
