#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# isolate_umdk_gpu.sh — 把当前 UMDK *编译产物* 拷到隔离目录，
# 不装进 /usr/lib64，测试进程也不再解析系统原生 liburma / liburma_common。
#
# 原理: liburma.so 用 dladdr() 反推自己所在目录 + "/urma" 作为 provider
# 搜索路径（urma_main.c urma_open_drivers()）。把我们编的
# liburma.so + liburma_common.so + liburma-udma.so 放进同一棵树，
# 再用 LD_LIBRARY_PATH / RUNPATH 指过去。
#
# 用法:
#   ./isolate_umdk_gpu.sh [BUILD_DIR] [ISOLATE_DIR]
#
#   BUILD_DIR   默认: 当前目录（在 UMDK 的 build/ 下执行可不传）
#   ISOLATE_DIR 默认: $HOME/umdk_gpu_isolated
#
# 用完:
#   <ISOLATE_DIR>/run.sh <ISOLATE_DIR>/bin/urma_perftest write_lat_gpu --gpu-mode=peermem ...
# 关掉 hook:
#   UMDK_GDR_HOOK=0 <ISOLATE_DIR>/run.sh ...

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HOOK_SRC="$SCRIPT_DIR/userspace/gdr-geforce-hook"
BUILD_DIR="$(cd "${1:-$PWD}" && pwd)"
ISOLATE_DIR="${2:-$HOME/umdk_gpu_isolated}"
HOOK_SO="$ISOLATE_DIR/lib/libgdr_geforce_hook.so"
LIB_DIR="$ISOLATE_DIR/lib"
PROV_DIR="$ISOLATE_DIR/lib/urma"
BIN_DIR="$ISOLATE_DIR/bin"

echo "=== SCRIPT_DIR  = $SCRIPT_DIR"
echo "=== BUILD_DIR   = $BUILD_DIR"
echo "=== ISOLATE_DIR = $ISOLATE_DIR"
echo

mkdir -p "$PROV_DIR" "$BIN_DIR"

copy_matches() {
	local dest="$1"
	shift
	local f
	for f in "$@"; do
		[ -e "$f" ] || continue
		echo "  copy: $f -> $dest/"
		cp -a "$f" "$dest/"
	done
}

# ---------- 1. liburma.so* 核心库 ----------
echo "--- [1/7] liburma.so* (core) ---"
mapfile -t LIBURMA_CORE < <(find "$BUILD_DIR" -name "liburma.so*" -path "*/urma/lib/urma/core/*" 2>/dev/null)
if [ ${#LIBURMA_CORE[@]} -eq 0 ]; then
	mapfile -t LIBURMA_CORE < <(find "$BUILD_DIR" -name "liburma.so*" 2>/dev/null | grep -v "/hw/udma/" | grep -v "liburma-" || true)
fi
if [ ${#LIBURMA_CORE[@]} -eq 0 ]; then
	echo "ERROR: 没找到 liburma.so*，确认 BUILD_DIR 对不对（当前: $BUILD_DIR）" >&2
	exit 1
fi
copy_matches "$LIB_DIR" "${LIBURMA_CORE[@]}"

# ---------- 2. liburma_common.so*（缺这个就会落到 /usr/lib64 原生库）----------
echo
echo "--- [2/7] liburma_common.so* ---"
mapfile -t LIBURMA_COMMON < <(find "$BUILD_DIR" -name "liburma_common.so*" 2>/dev/null)
if [ ${#LIBURMA_COMMON[@]} -eq 0 ]; then
	echo "ERROR: 没找到 liburma_common.so*。不拷的话 ldd 会解析到 /usr/lib64 系统库。" >&2
	exit 1
fi
copy_matches "$LIB_DIR" "${LIBURMA_COMMON[@]}"

# ---------- 3. liburma-*.so* provider（含版本化 soname）----------
echo
echo "--- [3/7] liburma-*.so* (provider) ---"
mapfile -t PROVIDERS < <(find "$BUILD_DIR" \( -name "liburma-*.so" -o -name "liburma-*.so.*" \) 2>/dev/null)
if [ ${#PROVIDERS[@]} -eq 0 ]; then
	echo "ERROR: 没找到任何 liburma-*.so provider" >&2
	exit 1
fi
copy_matches "$PROV_DIR" "${PROVIDERS[@]}"

# ---------- 4. 测试程序 ----------
echo
echo "--- [4/7] 测试程序 ---"
FOUND_BIN=0
for name in urma_perftest urma_ping urma_admin urma_example; do
	BIN=$(find "$BUILD_DIR" -type f -executable -name "$name" 2>/dev/null | head -1 || true)
	if [ -n "$BIN" ]; then
		echo "  copy: $BIN"
		cp -a "$BIN" "$BIN_DIR/"
		FOUND_BIN=1
	fi
done
if [ "$FOUND_BIN" -eq 0 ]; then
	echo "  警告: 一个测试程序都没找到，你需要自己把要测的二进制拷进 $BIN_DIR/"
fi

# ---------- 5. 编译期依赖里、BUILD 树里还有的其它 .so（以及 libummu）----------
echo
echo "--- [5/7] 补齐 NEEDED .so（来自本 build；libummu 若只在系统上则冻结一份）---"
needed_names() {
	local obj="$1"
	if command -v readelf >/dev/null 2>&1; then
		readelf -d "$obj" 2>/dev/null | awk '/NEEDED/ { gsub(/[\[\]]/,"",$NF); print $NF }'
	else
		ldd "$obj" 2>/dev/null | awk '/=>/ { print $1 }'
	fi
}

is_system_skip() {
	case "$1" in
	libc.so*|libm.so*|libdl.so*|librt.so*|libpthread.so*|libgcc_s.so*| \
	libstdc++.so*|ld-linux*.so*|libcuda.so*|libnvidia*|linux-vdso.so*)
		return 0 ;;
	esac
	return 1
}

copy_needed_from_build_or_freeze() {
	local obj="$1"
	local name src
	while read -r name; do
		[ -n "$name" ] || continue
		is_system_skip "$name" && continue
		[ -e "$LIB_DIR/$name" ] && continue
		[ -e "$PROV_DIR/$name" ] && continue
		src=$(find "$BUILD_DIR" -name "$name" 2>/dev/null | head -1 || true)
		if [ -n "$src" ]; then
			echo "  copy from build: $src"
			cp -a "$src" "$LIB_DIR/"
			# 连同同目录版本化文件一起带走
			copy_matches "$LIB_DIR" "$(dirname "$src")/${name%.*}"* 2>/dev/null || true
			continue
		fi
		# libummu 不在 UMDK 仓库里编，但 liburma-udma.so 链它。
		# 拷进隔离目录，避免和以后系统升级搅在一起；不是 UMDK 产物。
		if [ "$name" = "libummu.so" ] || [[ "$name" == libummu.so* ]]; then
			src=$(ldconfig -p 2>/dev/null | awk -v n="$name" '$1==n { print $NF; exit }')
			if [ -z "$src" ] || [ ! -e "$src" ]; then
				src=$(find /usr/lib64 /usr/lib /usr/local/lib /usr/local/lib64 -name "$name" 2>/dev/null | head -1 || true)
			fi
			if [ -n "$src" ] && [ -e "$src" ]; then
				echo "  freeze libummu (kernel userspace, not UMDK): $src"
				cp -a "$src" "$LIB_DIR/"
				copy_matches "$LIB_DIR" "$(dirname "$src")"/libummu.so* 2>/dev/null || true
			else
				echo "  警告: $obj 需要 $name，BUILD 和系统都没找到" >&2
			fi
		fi
	done < <(needed_names "$obj")
}

for obj in "$LIB_DIR"/liburma.so* "$LIB_DIR"/liburma_common.so* "$PROV_DIR"/liburma-*.so* "$BIN_DIR"/*; do
	[ -e "$obj" ] || continue
	[ -L "$obj" ] && continue
	copy_needed_from_build_or_freeze "$obj"
done

# ---------- 6. libgdrapi（优先我们编的 /usr/local，不优先 /usr/lib64）----------
echo
echo "--- [6/7] libgdrapi.so* ---"
GDR_COPIED=0
copy_gdrapi_from() {
	local src="$1"
	[ -e "$src" ] || return 1
	echo "  copy: $src"
	cp -a "$src" "$LIB_DIR/"
	# 同目录 soname 链
	copy_matches "$LIB_DIR" "$(dirname "$src")"/libgdrapi.so* 2>/dev/null || true
	GDR_COPIED=1
}

if [ -n "${PERFTEST_GDRCOPY_LIB:-}" ]; then
	copy_gdrapi_from "$PERFTEST_GDRCOPY_LIB" || true
fi
if [ "$GDR_COPIED" -eq 0 ] && [ -n "${GDRCOPY_PREFIX:-}" ]; then
	copy_gdrapi_from "$GDRCOPY_PREFIX/lib/libgdrapi.so.2" || \
		copy_gdrapi_from "$GDRCOPY_PREFIX/lib64/libgdrapi.so.2" || true
fi
if [ "$GDR_COPIED" -eq 0 ]; then
	src=$(find "$BUILD_DIR" -name "libgdrapi.so*" 2>/dev/null | head -1 || true)
	[ -n "$src" ] && copy_gdrapi_from "$src" || true
fi
if [ "$GDR_COPIED" -eq 0 ]; then
	for src in \
		/usr/local/lib/libgdrapi.so.2 \
		/usr/local/lib64/libgdrapi.so.2 \
		/usr/local/lib/libgdrapi.so \
		/usr/local/lib64/libgdrapi.so
	do
		copy_gdrapi_from "$src" && break
	done
fi
if [ "$GDR_COPIED" -eq 0 ]; then
	echo "  警告: 没找到编译安装的 libgdrapi（/usr/local/lib）。" >&2
	echo "  peermem 会退化成 1B D2H 门铃，小包会卡住。可设 PERFTEST_GDRCOPY_LIB=... 再跑本脚本。" >&2
fi

# ---------- 7. GeForce GDR hook ----------
echo
echo "--- [7/7] make GeForce GDR hook ---"
if [ ! -f "$HOOK_SRC/Makefile" ] || [ ! -f "$HOOK_SRC/gdr_geforce_hook.c" ]; then
	echo "ERROR: hook 源码不在 $HOOK_SRC （需要 userspace/gdr-geforce-hook）" >&2
	exit 1
fi
make -C "$HOOK_SRC"
cp -a "$HOOK_SRC/libgdr_geforce_hook.so" "$HOOK_SO"
echo "  copy: $HOOK_SRC/libgdr_geforce_hook.so -> $HOOK_SO"

# ---------- RUNPATH：$ORIGIN，避免 DT_RPATH 写死 /usr/lib64 盖掉 LD_LIBRARY_PATH ----------
if command -v patchelf >/dev/null 2>&1; then
	echo
	echo "--- patchelf RUNPATH ---"
	for obj in "$LIB_DIR"/liburma*.so* "$LIB_DIR"/libgdrapi.so* "$LIB_DIR"/libummu.so* "$PROV_DIR"/liburma-*.so*; do
		[ -e "$obj" ] || continue
		[ -L "$obj" ] && continue
		patchelf --remove-rpath "$obj" 2>/dev/null || true
		patchelf --set-rpath '$ORIGIN:$ORIGIN/urma' "$obj" 2>/dev/null || \
			echo "  警告: patchelf $obj 失败" >&2
		echo "  rpath: $obj -> \$ORIGIN:\$ORIGIN/urma"
	done
	for bin in "$BIN_DIR"/*; do
		[ -f "$bin" ] || continue
		patchelf --remove-rpath "$bin" 2>/dev/null || true
		patchelf --set-rpath '$ORIGIN/../lib' "$bin" 2>/dev/null || \
			echo "  警告: patchelf $bin 失败" >&2
		echo "  rpath: $bin -> \$ORIGIN/../lib"
	done
else
	echo
	echo "--- 未安装 patchelf，依赖 run.sh 的 LD_LIBRARY_PATH ---"
fi

# ---------- 生成 run.sh：只用隔离目录，不把系统 LD_LIBRARY_PATH 拼在前面 ----------
echo
echo "--- 生成 run.sh ---"
GDR_IN_ISOLATE=""
if [ -e "$LIB_DIR/libgdrapi.so.2" ]; then
	GDR_IN_ISOLATE="$LIB_DIR/libgdrapi.so.2"
elif [ -e "$LIB_DIR/libgdrapi.so" ]; then
	GDR_IN_ISOLATE="$LIB_DIR/libgdrapi.so"
fi

cat > "$ISOLATE_DIR/run.sh" <<EOF
#!/usr/bin/env bash
# 只用隔离目录里编出来的 liburma / liburma_common / provider / (可选) libgdrapi。
# 不要把系统 /usr/lib64 的 UMDK 拼进 LD_LIBRARY_PATH 前面。
# GeForce GDR hook 只加在这次 exec 上。关掉: UMDK_GDR_HOOK=0 $ISOLATE_DIR/run.sh ...
set -euo pipefail
export LD_LIBRARY_PATH="$LIB_DIR:$PROV_DIR"
if [ -n "$GDR_IN_ISOLATE" ]; then
	export PERFTEST_GDRCOPY_LIB="$GDR_IN_ISOLATE"
fi
if [ "\${UMDK_GDR_HOOK:-1}" != "0" ] && [ -f "$HOOK_SO" ]; then
	export LD_PRELOAD="$HOOK_SO\${LD_PRELOAD:+:\$LD_PRELOAD}"
fi

if [ "\${UMDK_RUN_QUIET:-0}" != "1" ]; then
	echo "[run.sh] LD_LIBRARY_PATH=\$LD_LIBRARY_PATH" >&2
	echo "[run.sh] LD_PRELOAD=\${LD_PRELOAD:-}" >&2
	echo "[run.sh] PERFTEST_GDRCOPY_LIB=\${PERFTEST_GDRCOPY_LIB:-}" >&2
	TARGET_BIN="\$1"
	if command -v ldd >/dev/null 2>&1 && [ -f "\$TARGET_BIN" ]; then
		echo "[run.sh] ldd \$TARGET_BIN (urma/gdr/ummu):" >&2
		LD_PRELOAD= ldd "\$TARGET_BIN" 2>/dev/null | grep -Ei 'liburma|libgdrapi|libummu' >&2 || true
	fi
fi
exec "\$@"
EOF
chmod +x "$ISOLATE_DIR/run.sh"

echo
echo "=== 隔离目录内容 ==="
find "$ISOLATE_DIR" -type f -o -type l | sort

echo
echo "--- RPATH/RUNPATH ---"
for bin in "$BIN_DIR"/*; do
	[ -f "$bin" ] || continue
	echo "-- $bin --"
	if command -v readelf >/dev/null 2>&1; then
		readelf -d "$bin" 2>/dev/null | grep -E "RPATH|RUNPATH" || echo "  (无 RPATH/RUNPATH)"
	fi
done

echo
echo "=== 验证：liburma* 必须全部来自隔离目录，禁止 /usr/lib64 ==="
VERIFY_FAIL=0
for bin in "$BIN_DIR"/*; do
	[ -f "$bin" ] || continue
	echo "-- $bin --"
	RESOLVED=$(LD_LIBRARY_PATH="$LIB_DIR:$PROV_DIR" LD_PRELOAD= ldd "$bin" 2>/dev/null | grep -Ei 'liburma|libgdrapi|libummu' || true)
	echo "$RESOLVED"
	if echo "$RESOLVED" | grep -E 'liburma' | grep -E '/usr/lib64|/usr/lib/' >/dev/null 2>&1; then
		echo "ERROR: 仍解析到系统原生 liburma*，隔离失败。" >&2
		VERIFY_FAIL=1
	fi
	if ! echo "$RESOLVED" | grep -q 'liburma_common'; then
		echo "ERROR: ldd 看不到 liburma_common（隔离拷贝失败？）" >&2
		VERIFY_FAIL=1
	fi
	if ! echo "$RESOLVED" | grep -F "$ISOLATE_DIR/lib" | grep -q 'liburma.so'; then
		echo "ERROR: liburma.so 没有解析到 $ISOLATE_DIR/lib" >&2
		VERIFY_FAIL=1
	fi
done
if [ "$VERIFY_FAIL" -ne 0 ]; then
	exit 1
fi

echo
echo "=== 完成 ==="
echo "跑测试（系统 /usr/lib64 的 liburma* 不会进这个进程）："
echo "  $ISOLATE_DIR/run.sh $BIN_DIR/urma_perftest write_lat_gpu --gpu-mode=peermem -d udmac0d1e2 ..."
echo
echo "成功时应看到:"
echo "  liburma.so.0 => $LIB_DIR/liburma.so.0"
echo "  liburma_common.so.0 => $LIB_DIR/liburma_common.so.0"
echo "  [gdrcopy] ... libgdrapi （若已拷进隔离目录）"
echo "  [gdr-geforce] cuMemAlloc -> VMM 和 REGISTER_VIDMEM status=0x0"
echo
echo "关掉 hook:  UMDK_GDR_HOOK=0 $ISOLATE_DIR/run.sh ..."
echo "更新隔离目录:  $0 $BUILD_DIR $ISOLATE_DIR"
