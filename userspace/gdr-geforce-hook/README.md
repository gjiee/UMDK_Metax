# GDRCopy on GeForce / RTX 4090D — userspace hook

## 拷到 UMDK（不要全局 `export LD_PRELOAD`）

把本目录和旁边的 isolate 脚本拷进 UMDK 仓库根：

```bash
# 在 UMDK 仓库根
cp -a /path/to/nv_module_netlab/userspace/gdr-geforce-hook userspace/
cp /path/to/nv_module_netlab/userspace/isolate_umdk_gpu.sh ./
./isolate_umdk_gpu.sh build $HOME/umdk_gpu_isolated
```

`isolate` 会现场 `make` hook，拷进隔离目录，并生成只给测试进程设
`LD_PRELOAD` 的 `run.sh`。`cat` / 打开文件不会加载 so。

```bash
$HOME/umdk_gpu_isolated/run.sh \
  $HOME/umdk_gpu_isolated/bin/urma_perftest write_lat_gpu \
  --gpu-mode=peermem -d udmac0d1e2 ...
# 关掉 hook: UMDK_GDR_HOOK=0 .../run.sh ...
```

`run.sh` 会拦 `dlsym(libcuda, "cuMemAlloc_v2")`（urma_perftest 的 loader）。
成功日志要有 `cuMemAlloc -> VMM` 和 `REGISTER_VIDMEM status=0x0`。

## LD_PRELOAD 怎么拦截

```bash
export LD_PRELOAD=$PWD/libgdr_geforce_hook.so
./gdrcopy_copylat
```

动态链接器先加载这个 `.so`。里面导出了和 `libc` / `libcuda` 同名的 `ioctl`、`cuMemAlloc` 等，所以进程里的调用先进 hook，再由 `dlsym(RTLD_NEXT, ...)` 转到真实现。不改 `libcuda` 文件偏移。详情见 `gdr_geforce_hook.c` 文件头。


Do **not** patch the NVIDIA kernel modules for GDR. Keep
[aikitoria `575.64.05-p2p`](https://github.com/aikitoria/open-gpu-kernel-modules/tree/575.64.05-p2p)
as-is (that fork only enables GPU↔GPU BAR1 P2P).

This `LD_PRELOAD` library is the GDR half:

1. `CU_DEVICE_ATTRIBUTE_GPU_DIRECT_RDMA_SUPPORTED` (116) and the VMM RDMA
   attribute (110) return 1, so GDRCopy `check_gdr_support()` passes.
2. `cuMemAlloc` is turned into VMM (`cuMemCreate` / `cuMemMap`) so RM has an
   `hMemory` that can be registered.
3. `ioctl` captures CUDA’s `hClient` / `hDevice` / `hSubdevice` / `hVASpace` /
   `hMemory`.
4. The hook allocates `NV50_THIRD_PARTY_P2P` (`0x503c`) once and sends
   `REGISTER_VA_SPACE` (`0x503c0102`) + `REGISTER_VIDMEM` (`0x503c0104`).

After that, stock `gdr_pin_buffer()` → `nvidia_p2p_get_pages()` can find the
CUDA VA instead of returning `0x57` / `-EINVAL`.

## Build (on the 4090D box)

```bash
cd userspace/gdr-geforce-hook
make
```

Needs `gcc` only. No CUDA toolkit headers.

## Run

```bash
export LD_PRELOAD=$PWD/libgdr_geforce_hook.so
# optional: GPUDIRECT_GPU=0
# optional: GDR_GEFORCE_QUIET=1
# optional: GDR_GEFORCE_VERBOSE=1   # print "loaded" (off by default; needed for global preload)

./copybw
# or ./validate / ./sanity
```

### Global preload

`ioctl()` is still the exported symbol (LD_PRELOAD cannot attach by fd), but
the hook **only parses packets on NVIDIA RM nodes**: `/dev/nvidiactl`,
`/dev/nvidiaN`, char major 195, and only for `RM_ALLOC` / `RM_ALLOC_OBJECT`.
Socket / tty / eventfd / framebuffer `ioctl`s are forwarded with no lock.

The constructor does **not** `dlopen` libcuda and does **not** print `loaded`
unless `GDR_GEFORCE_VERBOSE=1`. Safe to leave in the login environment:

```bash
export LD_PRELOAD=/absolute/path/libgdr_geforce_hook.so
```

`sudo` still drops `LD_PRELOAD`. Already root: do not wrap with `sudo`.
Otherwise use `sudo env LD_PRELOAD=/absolute/path/libgdr_geforce_hook.so ...`
or `/etc/ld.so.preload` (survives sudo because it is not an env var).

`urma_perftest` `dlopen`s libcuda and `dlsym`s `cuMemAlloc_v2` by handle.
This `.so` also intercepts `dlsym` so that loader gets the VMM+register path.

Success looks like:

```text
[gdr-geforce] ready GPU 0 client=0x... subdev=0x... va=0x...
[gdr-geforce] captured hMemory=0x...
[gdr-geforce] allocated NV50_THIRD_PARTY_P2P hTPP=0x...
[gdr-geforce] REGISTER_VA_SPACE ... status=0x0
[gdr-geforce] REGISTER_VIDMEM ... status=0x0
[gdr-geforce] cuMemAlloc -> VMM va=0x... size=0x...
```

Then `gdr_pin_buffer` must not return `-22`.

If `REGISTER_VIDMEM status=0x0` already printed but the test dies with
`CUDA_ERROR_NOT_SUPPORTED` at `copylat.cpp` / `gpu_mem_alloc`, that is
`cuPointerSetAttribute(SYNC_MEMOPS)` on the VMM pointer (GeForce rejects it).
Rebuild this hook: it now intercepts that call and returns success so pin can run.

## If it still fails

| Log | Meaning |
|---|---|
| never `ready GPU` | `ioctl` hook missed CUDA RM_ALLOC (or `cuInit` did not run) |
| `cannot alloc 0x503c yet` | handles not captured before first alloc |
| `REGISTER_VIDMEM ... no hMemory` | still on PMA `cuMemAlloc`; VMM replace did not run |
| `REGISTER_VIDMEM ... status=0x...` nonzero | wrong handle, or VA/size not 64K aligned |
| register ok, pin still `-22` | pin VA/length is not inside the registered range |

Keep BAR1 at 32 GB and the aikitoria modules loaded. This hook does not replace
that P2P work.
