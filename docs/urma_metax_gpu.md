# URMA — 沐曦 (MetaX) GPU 段与内核 PR #21 对齐

内核侧（`123toorc/urma_driver` PR #21）把 GPU pin 收成厂商无关的 `ops->pin/unpin`。选卡是 **`udma.ko` 模块参数**，不是用户态 ucmd：

```bash
insmod metax.ko
insmod udma.ko gpu_vendor=metax
insmod udma_metax_p2p_bridge.ko
```

用户态 ABI **不变**：`urma_seg_cfg_t.is_gpu_seg` → `struct udma_register_seg_ucmd { is_gpu_seg, reserved }`，走 `udrv_data`。`reserved` 必须保持 0，不要塞 vendor id。

## perftest

```bash
urma_perftest write_bw_gpu --gpu-mode=metax -d <ub_dev>
urma_perftest write_bw_gpu --use_metax=0    -d <ub_dev>
```

- `mcSetDevice` + `mcMalloc`（`dlopen libmcruntime.so`，编译不链 MACA）
- 长度按 4KB 向上对齐，对应内核 MetaX `PAGE_SIZE` 对齐（NVIDIA peermem 仍是 64KB）
- `is_gpu_seg=1`，内核 `metax_p2p_acquire_mem` pin
- 没有 MACA 时 fallback `memalign`（与 peermem 无 CUDA 时相同）

库搜索：`PERFTEST_METAX_LIB`、`$MACA_PATH/{lib,lib64}`、`/opt/maca`、`/usr/local/maca`、默认 linker path。

## gdr-verify（读 GPU 内容）

`write_lat_gpu` 的 content verifier 由 **编译开关** 打开，不是 CLI：

```bash
# 在 src/ 的 build 目录；已有 CMakeCache 时必须显式 -D，否则会沿用旧的 OFF
cmake -DPERFTEST_ENABLE_GPU_DEBUG=ON ..
make urma_perftest -j
```

确认二进制里有 verifier：

```bash
strings bin/urma_perftest | grep -E "gdr-verify|test GDR write|GPU_RECV_UNWRITTEN"
```

跑 `write_lat_gpu --gpu-mode=metax` 时 stderr 应出现：

- `[gdr-verify] PERFTEST_ENABLE_GPU_DEBUG=1`
- `GPU planted "test GDR write"` / `GPU recv prefilled with "GPU_RECV_UNWRITTEN"`
- 结束后 D2H dump：`PASS: saw magic`（WRITE 落到 GPU）或 `FAIL: still INIT mark`（没落到 GPU）

这条路径会 D2H 读 recv，**不要拿这时的 t_avg 当性能数**。测 latency 用 `-DPERFTEST_ENABLE_GPU_DEBUG=OFF`。

## 不要和 PR #38 混用

另一份用户态草稿把 `reserved` 改成 `gpu_vendor` 并让内核按 ucmd 分发。那和 PR #21 不一致：本树按内核模块参数选卡。
