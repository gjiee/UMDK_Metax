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
# 用法:
#   ./isolate_umdk_gpu.sh [BUILD_DIR] [ISOLATE_DIR]
#
#   BUILD_DIR   默认: 当前目录（在 UMDK 的 build/ 目录下执行本脚本即可不传）
#   ISOLATE_DIR 默认: $HOME/umdk_gpu_isolated
#
# 用完直接:
#   <ISOLATE_DIR>/run.sh <ISOLATE_DIR>/bin/urma_perftest write_lat_gpu -d ubp0 ...

set -euo pipefail

BUILD_DIR="$(cd "${1:-$PWD}" && pwd)"
ISOLATE_DIR="${2:-$HOME/umdk_gpu_isolated}"

echo "=== BUILD_DIR   = $BUILD_DIR"
echo "=== ISOLATE_DIR = $ISOLATE_DIR"
echo

mkdir -p "$ISOLATE_DIR/lib/urma" "$ISOLATE_DIR/bin"

# ---------- 1. liburma.so 核心库（含 .so / .so.0 / .so.0.0.3 版本化 soname 链）----------
echo "--- [1/4] 查找 liburma.so* (核心库) ---"
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

# ---------- 2. liburma-udma.so provider（以及其它 liburma-*.so provider，如果有）----------
echo
echo "--- [2/4] 查找 liburma-*.so (provider) ---"
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
echo "--- [3/4] 查找测试程序 ---"
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

# ---------- 4. 生成一键运行脚本 ----------
echo
echo "--- [4/4] 生成 run.sh ---"
cat > "$ISOLATE_DIR/run.sh" <<EOF
#!/usr/bin/env bash
# 一键用隔离目录里的 liburma.so / liburma-udma.so 跑命令，
# 不影响系统 /usr/lib64 下的原生库。
export LD_LIBRARY_PATH="$ISOLATE_DIR/lib:\${LD_LIBRARY_PATH:-}"
exec "\$@"
EOF
chmod +x "$ISOLATE_DIR/run.sh"

echo
echo "=== 隔离目录内容 ==="
find "$ISOLATE_DIR" -type f -o -type l | sort

# ---------- 5. RPATH/RUNPATH 检查（避免 LD_LIBRARY_PATH 被无视）----------
echo
echo "--- RPATH/RUNPATH 检查（如果测试程序自己写死了 RPATH，LD_LIBRARY_PATH 可能不生效）---"
for bin in "$ISOLATE_DIR"/bin/*; do
	[ -f "$bin" ] || continue
	echo "-- $bin --"
	if command -v readelf >/dev/null 2>&1; then
		readelf -d "$bin" 2>/dev/null | grep -E "RPATH|RUNPATH" || echo "  (无 RPATH/RUNPATH，LD_LIBRARY_PATH 会正常生效)"
	fi
done

# ---------- 6. 立即验证一次真的从隔离目录加载 ----------
echo
echo "=== 验证：liburma.so 是否解析到隔离目录 ==="
for bin in "$ISOLATE_DIR"/bin/*; do
	[ -f "$bin" ] || continue
	echo "-- $bin --"
	LD_LIBRARY_PATH="$ISOLATE_DIR/lib:${LD_LIBRARY_PATH:-}" ldd "$bin" 2>/dev/null | grep -i liburma || echo "  (ldd 没输出 liburma，检查一下)"
done

echo
echo "=== 完成 ==="
echo "以后跑测试统一这样（系统原生 liburma 完全不受影响）："
echo "  $ISOLATE_DIR/run.sh $ISOLATE_DIR/bin/urma_perftest write_lat_gpu -d ubp0 ..."
echo
echo "重新编译之后想更新隔离目录，直接重跑本脚本即可（会覆盖旧的拷贝）："
echo "  $0 $BUILD_DIR $ISOLATE_DIR"
