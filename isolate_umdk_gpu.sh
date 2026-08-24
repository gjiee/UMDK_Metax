#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# isolate_umdk_gpu.sh — 一键把当前 UMDK build 产物拷到隔离目录，
# 不装进 /usr/lib64，不影响系统原生 liburma.so / liburma-udma.so。
#
# 原理: liburma.so 用 dladdr() 反推自己所在目录 + "/urma" 作为 provider
# 搜索路径（见 urma_main.c urma_open_drivers()），不是写死 /usr/lib64。
# 只要把 liburma.so + liburma-udma.so 一起放进自定义目录，用
# LD_LIBRARY_PATH 指过去，系统默认路径原封不动，其他进程完全不受影响。
#
# 同时在本脚本里 make userspace/gdr-geforce-hook，并把
# libgdr_geforce_hook.so 拷进隔离目录。生成的 run.sh 只给这一次
# exec 的测试进程设 LD_PRELOAD，不写登录环境、也不碰 /etc/ld.so.preload。
# 所以 cat / vim / 打开 isolate 脚本本身不会加载 hook。
#
# 用法:
#   ./isolate_umdk_gpu.sh [BUILD_DIR] [ISOLATE_DIR]
#
#   BUILD_DIR   默认: 当前目录（在 UMDK 的 build/ 目录下执行本脚本即可不传）
#   ISOLATE_DIR 默认: $HOME/umdk_gpu_isolated
#
# 用完直接:
#   <ISOLATE_DIR>/run.sh <ISOLATE_DIR>/bin/urma_perftest write_lat_gpu --gpu-mode=peermem -d udmac0d1e2 ...
# 关掉 hook:
#   UMDK_GDR_HOOK=0 <ISOLATE_DIR>/run.sh ...

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HOOK_SRC="$SCRIPT_DIR/userspace/gdr-geforce-hook"
BUILD_DIR="$(cd "${1:-$PWD}" && pwd)"
ISOLATE_DIR="${2:-$HOME/umdk_gpu_isolated}"
HOOK_SO="$ISOLATE_DIR/lib/libgdr_geforce_hook.so"

echo "=== SCRIPT_DIR  = $SCRIPT_DIR"
echo "=== BUILD_DIR   = $BUILD_DIR"
echo "=== ISOLATE_DIR = $ISOLATE_DIR"
echo

mkdir -p "$ISOLATE_DIR/lib/urma" "$ISOLATE_DIR/bin"

# ---------- 1. liburma.so 核心库（含 .so / .so.0 / .so.0.0.3 版本化 soname 链）----------
echo "--- [1/5] 查找 liburma.so* (核心库) ---"
mapfile -t LIBURMA_CORE < <(find "$BUILD_DIR" -name "liburma.so*" -path "*/urma/lib/urma/core/*" 2>/dev/null)
if [ ${#LIBURMA_CORE[@]} -eq 0 ]; then
	# 兜底：万一目录结构不一样，退化成全局找但排除 hw/udma 下的 provider
	mapfile -t LIBURMA_CORE < <(find "$BUILD_DIR" -name "liburma.so*" 2>/dev/null | grep -v "/hw/udma/" || true)
fi
if [ ${#LIBURMA_CORE[@]} -eq 0 ]; then
	echo "ERROR: 没找到 liburma.so*，确认 BUILD_DIR 对不对（当前: $BUILD_DIR）" >&2
	exit 1
fi
for f in "${LIBURMA_CORE[@]}"; do
	echo "  copy: $f"
	cp -a "$f" "$ISOLATE_DIR/lib/"
done

# ---------- 2. liburma-*.so provider ----------
echo
echo "--- [2/5] 查找 liburma-*.so (provider) ---"
mapfile -t PROVIDERS < <(find "$BUILD_DIR" -name "liburma-*.so" 2>/dev/null)
if [ ${#PROVIDERS[@]} -eq 0 ]; then
	echo "ERROR: 没找到任何 liburma-*.so provider" >&2
	exit 1
fi
for f in "${PROVIDERS[@]}"; do
	echo "  copy: $f"
	cp -a "$f" "$ISOLATE_DIR/lib/urma/"
done

# ---------- 3. 测试程序 ----------
echo
echo "--- [3/5] 查找测试程序 ---"
FOUND_BIN=0
for name in urma_perftest urma_ping urma_admin urma_example; do
	BIN=$(find "$BUILD_DIR" -type f -executable -name "$name" 2>/dev/null | head -1 || true)
	if [ -n "$BIN" ]; then
		echo "  copy: $BIN"
		cp -a "$BIN" "$ISOLATE_DIR/bin/"
		FOUND_BIN=1
	fi
done
if [ "$FOUND_BIN" -eq 0 ]; then
	echo "  警告: 一个测试程序都没找到，你需要自己把要测的二进制拷进 $ISOLATE_DIR/bin/"
fi

# ---------- 4. 现场 make GeForce GDR hook，拷进隔离 lib ----------
echo
echo "--- [4/5] make GeForce GDR hook ---"
if [ ! -f "$HOOK_SRC/Makefile" ] || [ ! -f "$HOOK_SRC/gdr_geforce_hook.c" ]; then
	echo "ERROR: hook 源码不在 $HOOK_SRC （需要 userspace/gdr-geforce-hook）" >&2
	exit 1
fi
make -C "$HOOK_SRC"
cp -a "$HOOK_SRC/libgdr_geforce_hook.so" "$HOOK_SO"
echo "  copy: $HOOK_SRC/libgdr_geforce_hook.so -> $HOOK_SO"

# ---------- 5. 生成一键运行脚本（LD_LIBRARY_PATH + 仅本进程 LD_PRELOAD）----------
echo
echo "--- [5/5] 生成 run.sh ---"
cat > "$ISOLATE_DIR/run.sh" <<EOF
#!/usr/bin/env bash
# 一键用隔离目录里的 liburma.so / liburma-udma.so 跑命令，
# 不影响系统 /usr/lib64 下的原生库。
# GeForce GDR hook 只加在这次 exec 上，不要在 shell 里 export LD_PRELOAD。
# 关掉 hook: UMDK_GDR_HOOK=0 $ISOLATE_DIR/run.sh ...
export LD_LIBRARY_PATH="$ISOLATE_DIR/lib:\${LD_LIBRARY_PATH:-}"
if [ "\${UMDK_GDR_HOOK:-1}" != "0" ] && [ -f "$HOOK_SO" ]; then
	export LD_PRELOAD="$HOOK_SO\${LD_PRELOAD:+:\$LD_PRELOAD}"
fi

# 每次运行都打印一次实际解析到的 liburma*.so 路径，防止 sudo/环境变量丢失、
# 或者目标机器上其它 /etc/ld.so.conf.d 配置抢先命中系统库而不自知——
# "Failed to create urma instance!" 这种在 create_context 阶段就失败的问题，
# 十有八九是这里解析到了不匹配的库/内核 ABI 不一致，而不是 GPU 代码本身的锅。
if [ "\${UMDK_RUN_QUIET:-0}" != "1" ]; then
	echo "[run.sh] LD_LIBRARY_PATH=\$LD_LIBRARY_PATH" >&2
	echo "[run.sh] LD_PRELOAD=\${LD_PRELOAD:-}" >&2
	TARGET_BIN="\$1"
	if command -v ldd >/dev/null 2>&1 && [ -f "\$TARGET_BIN" ]; then
		echo "[run.sh] ldd \$TARGET_BIN | grep liburma:" >&2
		# ldd 自己会 exec 目标，清掉 LD_PRELOAD 以免 hook 进 ldd / 刷屏
		LD_PRELOAD= ldd "\$TARGET_BIN" 2>/dev/null | grep -i liburma >&2 || echo "[run.sh]   (没匹配到 liburma，检查一下！)" >&2
	fi
fi
exec "\$@"
EOF
chmod +x "$ISOLATE_DIR/run.sh"

echo
echo "=== 隔离目录内容 ==="
find "$ISOLATE_DIR" -type f -o -type l | sort

# ---------- 6. RPATH/RUNPATH 检查（避免 LD_LIBRARY_PATH 被无视）----------
echo
echo "--- RPATH/RUNPATH 检查（如果测试程序自己写死了 RPATH，LD_LIBRARY_PATH 可能不生效）---"
for bin in "$ISOLATE_DIR"/bin/*; do
	[ -f "$bin" ] || continue
	echo "-- $bin --"
	if command -v readelf >/dev/null 2>&1; then
		readelf -d "$bin" 2>/dev/null | grep -E "RPATH|RUNPATH" || echo "  (无 RPATH/RUNPATH，LD_LIBRARY_PATH 会正常生效)"
	fi
done

# ---------- 7. 立即验证一次真的从隔离目录加载 ----------
echo
echo "=== 验证：liburma.so 是否解析到隔离目录 ==="
for bin in "$ISOLATE_DIR"/bin/*; do
	[ -f "$bin" ] || continue
	echo "-- $bin --"
	LD_LIBRARY_PATH="$ISOLATE_DIR/lib:${LD_LIBRARY_PATH:-}" LD_PRELOAD= ldd "$bin" 2>/dev/null | grep -i liburma || echo "  (ldd 没输出 liburma，检查一下)"
done

echo
echo "=== 完成 ==="
echo "以后跑测试统一这样（系统原生 liburma 完全不受影响，hook 只进这个进程）："
echo "  $ISOLATE_DIR/run.sh $ISOLATE_DIR/bin/urma_perftest write_lat_gpu --gpu-mode=peermem -d udmac0d1e2 ..."
echo
echo "成功时应看到 [gdr-geforce] cuMemAlloc -> VMM 和 REGISTER_VIDMEM status=0x0"
echo "关掉 hook:  UMDK_GDR_HOOK=0 $ISOLATE_DIR/run.sh ..."
echo
echo "重新编译之后想更新隔离目录，直接重跑本脚本即可（会覆盖旧的拷贝，并重新 make hook）："
echo "  $0 $BUILD_DIR $ISOLATE_DIR"
