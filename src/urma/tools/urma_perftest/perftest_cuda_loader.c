/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2026. All rights reserved.
 *
 * dlopen/dlsym wrappers for libcuda.so.1 (no cuda.h at build time).
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dlfcn.h>
#include <errno.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "perftest_cuda_loader.h"
#include "urma_api.h"

#define CL_LOG(fmt, ...) \
    fprintf(stderr, "[cuda-loader] %s: " fmt "\n", __func__, ##__VA_ARGS__)

/* ---- CUDA Driver API ABI 速写, 与 cuda.h 内容一致 (CUDA 12.x) ---- */
typedef int      CUresult;          /* cuda.h: enum CUresult */
typedef int      CUdevice;
typedef void    *CUcontext;
typedef uint64_t CUdeviceptr;
/*
 * FIX: 单独给 CUpointer_attribute 一个 typedef (而不是内联写 int), 纯粹是
 * 规范化/可读性收益 —— ABI 上跟 int 完全一样 (cuda.h 里就是个普通 enum,
 * 4 字节, 不会因为换个 typedef 名字改变调用约定), 之前那次 EINVAL 不是
 * 这个类型名字的锅。
 */
typedef int      CUpointer_attribute;
#define CUDA_SUCCESS                                 0
#define CU_CTX_MAP_HOST                              0x08
#define CU_MEM_RANGE_HANDLE_TYPE_DMA_BUF_FD          0x1
/*
 * CUpointer_attribute 里跟 GPUDirect RDMA "pin 之前的握手" 相关的那一个。
 * 数值来自 cuda.h 的稳定 ABI 枚举 (CU_POINTER_ATTRIBUTE_SYNC_MEMOPS = 6),
 * 跟本文件其它属性常量 (PTR_ATTR_MEMORY_TYPE=2/IS_MANAGED=8/
 * DEVICE_ORDINAL=9/RANGE_START_ADDR=11/RANGE_SIZE=12, 见 dump_cuda_ptr())
 * 是同一套编号体系, 已经跟官方文档核对过。
 */
#define CU_POINTER_ATTRIBUTE_SYNC_MEMOPS             6
/* 在 cuda.h 里 cuMemGetHandleForAddressRange 的签名是:
 *   CUresult cuMemGetHandleForAddressRange(
 *       void *handle, CUdeviceptr dptr, size_t size,
 *       CUmemRangeHandleType handleType, unsigned long long flags);
 */

_Static_assert(sizeof(CUdeviceptr) == 8, "CUdeviceptr must be 64-bit");
_Static_assert(sizeof(uintptr_t) == 8, "uintptr_t must be 64-bit");
_Static_assert((int)PERFTEST_GPU_VENDOR_NVIDIA == (int)URMA_GPU_VENDOR_NVIDIA,
               "perftest/urma NVIDIA vendor id must match");
_Static_assert((int)PERFTEST_GPU_VENDOR_MUXI == (int)URMA_GPU_VENDOR_MUXI,
               "perftest/urma Muxi vendor id must match");

/* ---- 函数指针类型 ---- */
typedef CUresult (*pfn_cuInit)(unsigned int Flags);
typedef CUresult (*pfn_cuDriverGetVersion)(int *driverVersion);
typedef CUresult (*pfn_cuDeviceGet)(CUdevice *device, int ordinal);
typedef CUresult (*pfn_cuDeviceGetCount)(int *count);
typedef CUresult (*pfn_cuDeviceGetName)(char *name, int len, CUdevice dev);
typedef CUresult (*pfn_cuCtxCreate)(CUcontext *pctx, unsigned int flags, CUdevice dev);
typedef CUresult (*pfn_cuCtxDestroy)(CUcontext ctx);
typedef CUresult (*pfn_cuCtxSetCurrent)(CUcontext ctx);
typedef CUresult (*pfn_cuCtxGetCurrent)(CUcontext *pctx);
typedef CUresult (*pfn_cuMemAlloc)(CUdeviceptr *dptr, size_t bytesize);
typedef CUresult (*pfn_cuMemFree)(CUdeviceptr dptr);
typedef CUresult (*pfn_cuMemAllocHost)(void **pp, size_t bytesize);
typedef CUresult (*pfn_cuMemFreeHost)(void *p);
typedef CUresult (*pfn_cuMemcpyDtoH)(void *dstHost, CUdeviceptr srcDevice, size_t ByteCount);
typedef CUresult (*pfn_cuMemcpyHtoD)(CUdeviceptr dstDevice, const void *srcHost, size_t ByteCount);
typedef CUresult (*pfn_cuMemGetHandleForAddressRange)(
    void *handle, CUdeviceptr dptr, size_t size,
    int handleType, unsigned long long flags);
typedef CUresult (*pfn_cuPointerSetAttribute)(
    const void *value, CUpointer_attribute attribute, CUdeviceptr ptr);
/*
 * FIX: cuGetErrorName/cuGetErrorString —— 之前排查 cuPointerSetAttribute
 * 返回 1 (CUDA_ERROR_INVALID_VALUE) 只能靠猜数字, 加上这两个之后所有
 * CL_LOG 都能顺手打出符号名 + 官方文字描述, 以后再出现任何 CUresult != 0
 * 都不用再去翻 cuda.h 对照表。
 */
typedef CUresult (*pfn_cuGetErrorName)(CUresult error, const char **pStr);
typedef CUresult (*pfn_cuGetErrorString)(CUresult error, const char **pStr);

/* ---- 全局状态 ---- */
static int        g_loaded;
static int        g_ctx_owned;
static void      *g_libcuda;
static void      *g_libmxkw;
static CUcontext  g_ctx;
static perftest_gpu_vendor_t g_vendor_hint = PERFTEST_GPU_VENDOR_NONE;
static perftest_gpu_vendor_t g_vendor = PERFTEST_GPU_VENDOR_NONE;
static int        g_use_maca_runtime; /* 1 = mc* from libmcruntime.so */

#define DECL_SYM(name) static pfn_##name g_##name
DECL_SYM(cuInit);
DECL_SYM(cuDriverGetVersion);
DECL_SYM(cuDeviceGet);
DECL_SYM(cuDeviceGetCount);
DECL_SYM(cuDeviceGetName);
DECL_SYM(cuCtxCreate);
DECL_SYM(cuCtxDestroy);
DECL_SYM(cuCtxSetCurrent);
DECL_SYM(cuCtxGetCurrent);
DECL_SYM(cuMemAlloc);
DECL_SYM(cuMemFree);
DECL_SYM(cuMemAllocHost);
DECL_SYM(cuMemFreeHost);
DECL_SYM(cuMemcpyDtoH);
DECL_SYM(cuMemcpyHtoD);
DECL_SYM(cuMemGetHandleForAddressRange);
DECL_SYM(cuPointerSetAttribute);
DECL_SYM(cuGetErrorName);
DECL_SYM(cuGetErrorString);

/* MACA native runtime (libmcruntime.so) — used when CUDA-compat symbols are absent. */
#define MACA_SUCCESS 0
#define MACA_MEMCPY_HOST_TO_DEVICE 1
#define MACA_MEMCPY_DEVICE_TO_HOST 2

typedef int (*pfn_mcInit)(unsigned int flags);
typedef int (*pfn_mcGetDeviceCount)(int *count);
typedef int (*pfn_mcSetDevice)(int device);
typedef int (*pfn_mcMalloc)(void **ptr, size_t size);
typedef int (*pfn_mcFree)(void *ptr);
typedef int (*pfn_mcMallocHost)(void **ptr, size_t size, unsigned int flags);
typedef int (*pfn_mcFreeHost)(void *ptr);
typedef int (*pfn_mcMemcpy)(void *dst, const void *src, size_t count, int kind);
typedef int (*pfn_mcMemcpyDtoH)(void *dst, uint64_t src, size_t count);
typedef int (*pfn_mcMemcpyHtoD)(uint64_t dst, const void *src, size_t count);
typedef const char *(*pfn_mcGetErrorString)(int err);
typedef int (*pfn_mxkwExportDMABufHandle)(void *ptr, uint64_t size,
                                          int *fd, uint64_t *offset);

static pfn_mcInit g_mcInit;
static pfn_mcGetDeviceCount g_mcGetDeviceCount;
static pfn_mcSetDevice g_mcSetDevice;
static pfn_mcMalloc g_mcMalloc;
static pfn_mcFree g_mcFree;
static pfn_mcMallocHost g_mcMallocHost;
static pfn_mcFreeHost g_mcFreeHost;
static pfn_mcMemcpy g_mcMemcpy;
static pfn_mcMemcpyDtoH g_mcMemcpyDtoH;
static pfn_mcMemcpyHtoD g_mcMemcpyHtoD;
static pfn_mcGetErrorString g_mcGetErrorString;
static pfn_mxkwExportDMABufHandle g_mxkwExportDMABufHandle;

const char *perftest_cuda_vendor_name(perftest_gpu_vendor_t vendor)
{
    switch (vendor) {
        case PERFTEST_GPU_VENDOR_NONE:
            return "none";
        case PERFTEST_GPU_VENDOR_NVIDIA:
            return "nvidia";
        case PERFTEST_GPU_VENDOR_MUXI:
            return "muxi";
        default:
            return "?";
    }
}

void perftest_cuda_set_vendor_hint(perftest_gpu_vendor_t vendor)
{
    g_vendor_hint = vendor;
    if (vendor == PERFTEST_GPU_VENDOR_NVIDIA ||
        vendor == PERFTEST_GPU_VENDOR_MUXI) {
        urma_gpu_vendor_set_preferred((urma_gpu_vendor_t)vendor);
    }
    CL_LOG("vendor hint=%s", perftest_cuda_vendor_name(vendor));
}

perftest_gpu_vendor_t perftest_cuda_get_vendor(void)
{
    if (g_vendor != PERFTEST_GPU_VENDOR_NONE) {
        return g_vendor;
    }
    if (g_vendor_hint != PERFTEST_GPU_VENDOR_NONE) {
        return g_vendor_hint;
    }
    return (perftest_gpu_vendor_t)urma_detect_gpu_vendor();
}

int perftest_cuda_needs_sync_memops(void)
{
    return perftest_cuda_get_vendor() == PERFTEST_GPU_VENDOR_NVIDIA;
}

static int path_is_maca(const char *path)
{
    if (path == NULL) {
        return 0;
    }
    return strstr(path, "maca") != NULL ||
           strstr(path, "libsymbol_cu") != NULL ||
           strstr(path, "libmcruntime") != NULL;
}

/*
 * FIX: 统一的错误打印 helper, 优先用 cuGetErrorName/cuGetErrorString
 * (需要 loader 已经把这两个符号解出来); 解不出来 (极老驱动) 就退化成
 * 纯数字, 不会崩。
 */
static void cl_log_cuda_error(const char *what, CUresult rc)
{
    const char *name = NULL;
    const char *desc = NULL;

    if (g_cuGetErrorName != NULL)
        (void)g_cuGetErrorName(rc, &name);
    if (g_cuGetErrorString != NULL)
        (void)g_cuGetErrorString(rc, &desc);

    fprintf(stderr, "[cuda-loader] %s -> %d (%s): %s\n",
            what, (int)rc,
            name ? name : "unknown",
            desc ? desc : "unknown");
}

//Change the context to current thread
static int perftest_cuda_make_ctx_current(void)
{
    CUresult rc;

    if (!g_loaded) {
        CL_LOG("loader not initialised");
        return -1;
    }

    if (g_use_maca_runtime) {
        if (g_mcSetDevice == NULL) {
            return 0;
        }
        rc = g_mcSetDevice(0);
        if (rc != MACA_SUCCESS) {
            CL_LOG("mcSetDevice -> %d", (int)rc);
            return -1;
        }
        return 0;
    }

    if (!g_ctx) {
        CL_LOG("CUDA ctx not created");
        return -1;
    }

    rc = g_cuCtxSetCurrent(g_ctx);
    if (rc != CUDA_SUCCESS) {
        CL_LOG("cuCtxSetCurrent -> %d", (int)rc);
        return -1;
    }

    return 0;
}

int perftest_cuda_set_current_ctx(void)
{
    return perftest_cuda_make_ctx_current();
}

/*
 * 部分 CUDA Driver API 名字带 _v2 后缀 (ABI stable handle). 这里两个候选名
 * 都尝试, 与 linux-rdma 的做法一致.
 */


static void *dlsym_prefer_v2(void *lib,
                             const char *legacy,
                             const char *v2)
{
    void *p = NULL;

    if (v2 != NULL)
        p = dlsym(lib, v2);

    if (p == NULL)
        p = dlsym(lib, legacy);

    return p;
}


static void *dlsym2(void *lib, const char *primary, const char *fallback)
{
    void *p = dlsym(lib, primary);
    if (p == NULL && fallback != NULL) {
        p = dlsym(lib, fallback);
    }
    return p;
}

static void clear_cuda_syms(void)
{
    g_cuInit = NULL;
    g_cuDriverGetVersion = NULL;
    g_cuDeviceGet = NULL;
    g_cuDeviceGetCount = NULL;
    g_cuDeviceGetName = NULL;
    g_cuCtxCreate = NULL;
    g_cuCtxDestroy = NULL;
    g_cuCtxSetCurrent = NULL;
    g_cuCtxGetCurrent = NULL;
    g_cuMemAlloc = NULL;
    g_cuMemFree = NULL;
    g_cuMemAllocHost = NULL;
    g_cuMemFreeHost = NULL;
    g_cuMemcpyDtoH = NULL;
    g_cuMemcpyHtoD = NULL;
    g_cuMemGetHandleForAddressRange = NULL;
    g_cuPointerSetAttribute = NULL;
    g_cuGetErrorName = NULL;
    g_cuGetErrorString = NULL;
}

static int load_cuda_symbols(void *lib)
{
#define LOAD(name, preferred)                                                \
    do {                                                                     \
        const char *selected = preferred ? preferred : #name;                \
        g_##name = (pfn_##name)dlsym_prefer_v2(                              \
            lib, #name, preferred);                                          \
        if (g_##name == NULL) {                                              \
            CL_LOG("dlsym(%s/%s) failed: %s",                                \
                   #name, preferred ? preferred : "-", dlerror());            \
            clear_cuda_syms();                                               \
            return -1;                                                       \
        }                                                                    \
        CL_LOG("loaded %s for %s at %p",                                     \
               selected, #name, (void *)g_##name);                           \
    } while (0)

    LOAD(cuInit,                            NULL);
    LOAD(cuDriverGetVersion,                NULL);
    LOAD(cuDeviceGet,                       NULL);
    LOAD(cuDeviceGetCount,                  NULL);
    LOAD(cuDeviceGetName,                   NULL);
    LOAD(cuCtxCreate,                       "cuCtxCreate_v2");
    LOAD(cuCtxDestroy,                      "cuCtxDestroy_v2");
    LOAD(cuCtxSetCurrent,                   NULL);
    LOAD(cuCtxGetCurrent,                   NULL);
    LOAD(cuMemAlloc,                        "cuMemAlloc_v2");
    LOAD(cuMemFree,                         "cuMemFree_v2");
    LOAD(cuMemAllocHost,                    "cuMemAllocHost_v2");
    LOAD(cuMemFreeHost,                     NULL);
    LOAD(cuMemcpyDtoH,                      "cuMemcpyDtoH_v2");
    LOAD(cuMemcpyHtoD,                      "cuMemcpyHtoD_v2");
    LOAD(cuPointerSetAttribute,             NULL);
    LOAD(cuGetErrorName,                    NULL);
    LOAD(cuGetErrorString,                  NULL);
#undef LOAD

    g_cuMemGetHandleForAddressRange =
        (pfn_cuMemGetHandleForAddressRange)dlsym2(lib,
            "cuMemGetHandleForAddressRange", "mcMemGetHandleForAddressRange");
    if (g_cuMemGetHandleForAddressRange == NULL) {
        CL_LOG("note: cuMemGetHandleForAddressRange not present "
               "-- --gpu-mode=direct may use mxkwExportDMABufHandle");
    }
    return 0;
}

static int load_maca_runtime_symbols(void *lib)
{
    g_mcMalloc = (pfn_mcMalloc)dlsym(lib, "mcMalloc");
    g_mcFree = (pfn_mcFree)dlsym(lib, "mcFree");
    g_mcMemcpy = (pfn_mcMemcpy)dlsym(lib, "mcMemcpy");
    if (g_mcMalloc == NULL || g_mcFree == NULL) {
        CL_LOG("libmcruntime missing mcMalloc/mcFree: %s", dlerror());
        return -1;
    }
    g_mcInit = (pfn_mcInit)dlsym(lib, "mcInit");
    g_mcGetDeviceCount = (pfn_mcGetDeviceCount)dlsym(lib, "mcGetDeviceCount");
    g_mcSetDevice = (pfn_mcSetDevice)dlsym(lib, "mcSetDevice");
    g_mcMallocHost = (pfn_mcMallocHost)dlsym(lib, "mcMallocHost");
    g_mcFreeHost = (pfn_mcFreeHost)dlsym(lib, "mcFreeHost");
    g_mcMemcpyDtoH = (pfn_mcMemcpyDtoH)dlsym(lib, "mcMemcpyDtoH");
    g_mcMemcpyHtoD = (pfn_mcMemcpyHtoD)dlsym(lib, "mcMemcpyHtoD");
    g_mcGetErrorString = (pfn_mcGetErrorString)dlsym(lib, "mcGetErrorString");
    if (g_mcMemcpy == NULL && (g_mcMemcpyDtoH == NULL || g_mcMemcpyHtoD == NULL)) {
        CL_LOG("libmcruntime missing memcpy symbols");
        return -1;
    }
    CL_LOG("loaded MACA runtime mcMalloc=%p mcMemcpy=%p",
           (void *)g_mcMalloc, (void *)g_mcMemcpy);
    return 0;
}

static void try_load_mxkw(void)
{
    const char *home = getenv("MACA_PATH");
    char buf[512];
    static const char *const cand[] = {
        "libmxkw.so",
        "/opt/maca/lib/libmxkw.so",
        "/opt/maca/lib64/libmxkw.so",
        NULL
    };
    int i;

    for (i = 0; cand[i] != NULL; i++) {
        g_libmxkw = dlopen(cand[i], RTLD_LAZY);
        if (g_libmxkw != NULL) {
            break;
        }
    }
    if (g_libmxkw == NULL && home != NULL && home[0] != '\0') {
        snprintf(buf, sizeof(buf), "%s/lib/libmxkw.so", home);
        g_libmxkw = dlopen(buf, RTLD_LAZY);
    }
    if (g_libmxkw == NULL) {
        return;
    }
    g_mxkwExportDMABufHandle =
        (pfn_mxkwExportDMABufHandle)dlsym(g_libmxkw, "mxkwExportDMABufHandle");
    if (g_mxkwExportDMABufHandle == NULL) {
        CL_LOG("libmxkw loaded but mxkwExportDMABufHandle missing");
        dlclose(g_libmxkw);
        g_libmxkw = NULL;
        return;
    }
    CL_LOG("loaded mxkwExportDMABufHandle from MACA libmxkw");
}

static void fill_lib_candidates(perftest_gpu_vendor_t want,
                                const char **out, size_t outn, size_t *n)
{
    const char *home = getenv("MACA_PATH");
    static char maca_sym[512];
    static char maca_rt[512];
    static char maca_cu[512];
    size_t i = 0;

    *n = 0;
    /* Muxi/auto: probe MACA CUDA-compat and native runtime before system libcuda. */
    if (want != PERFTEST_GPU_VENDOR_NVIDIA) {
        out[i++] = "libsymbol_cu.so";
        out[i++] = "/opt/maca/lib/libsymbol_cu.so";
        out[i++] = "/opt/maca/lib64/libsymbol_cu.so";
        out[i++] = "/opt/maca/tools/cu-bridge/lib/libcuda.so.1";
        out[i++] = "/opt/maca/tools/cu-bridge/lib/libcuda.so";
        if (home != NULL && home[0] != '\0') {
            snprintf(maca_sym, sizeof(maca_sym), "%s/lib/libsymbol_cu.so", home);
            snprintf(maca_cu, sizeof(maca_cu),
                     "%s/tools/cu-bridge/lib/libcuda.so", home);
            out[i++] = maca_sym;
            out[i++] = maca_cu;
        }
        out[i++] = "libmcruntime.so";
        out[i++] = "/opt/maca/lib/libmcruntime.so";
        out[i++] = "/opt/maca/lib64/libmcruntime.so";
        if (home != NULL && home[0] != '\0') {
            snprintf(maca_rt, sizeof(maca_rt), "%s/lib/libmcruntime.so", home);
            out[i++] = maca_rt;
        }
    }
    out[i++] = "libcuda.so.1";
    out[i++] = "libcuda.so";
    if (i > outn) {
        i = outn;
    }
    *n = i;
}

static int try_open_gpu_lib(const char *path)
{
    void *lib;
    int is_maca_rt;

    if (path == NULL || path[0] == '\0') {
        return -1;
    }
    dlerror();
    lib = dlopen(path, RTLD_LAZY | RTLD_GLOBAL);
    if (lib == NULL) {
        return -1;
    }

    is_maca_rt = (strstr(path, "libmcruntime") != NULL) ||
                 (dlsym(lib, "mcMalloc") != NULL && dlsym(lib, "cuInit") == NULL);

    if (is_maca_rt) {
        if (load_maca_runtime_symbols(lib) != 0) {
            dlclose(lib);
            return -1;
        }
        g_libcuda = lib;
        g_use_maca_runtime = 1;
        g_vendor = PERFTEST_GPU_VENDOR_MUXI;
        CL_LOG("using MACA native runtime from %s", path);
        return 0;
    }

    if (load_cuda_symbols(lib) != 0) {
        dlclose(lib);
        return -1;
    }
    g_libcuda = lib;
    g_use_maca_runtime = 0;
    if (path_is_maca(path) ||
        perftest_cuda_get_vendor() == PERFTEST_GPU_VENDOR_MUXI) {
        g_vendor = PERFTEST_GPU_VENDOR_MUXI;
    } else {
        g_vendor = PERFTEST_GPU_VENDOR_NVIDIA;
    }
    CL_LOG("using CUDA-compat driver from %s vendor=%s",
           path, perftest_cuda_vendor_name(g_vendor));
    return 0;
}

int perftest_cuda_loader_init(void)
{
    const char *cands[24];
    size_t n = 0;
    size_t i;
    perftest_gpu_vendor_t want;

    if (g_loaded) {
        return 0;
    }

    want = g_vendor_hint;
    if (want == PERFTEST_GPU_VENDOR_NONE) {
        want = (perftest_gpu_vendor_t)urma_detect_gpu_vendor();
    }
    CL_LOG("probe vendor=%s hint=%s",
           perftest_cuda_vendor_name(want),
           perftest_cuda_vendor_name(g_vendor_hint));

    fill_lib_candidates(want, cands, sizeof(cands) / sizeof(cands[0]), &n);
    for (i = 0; i < n; i++) {
        if (try_open_gpu_lib(cands[i]) == 0) {
            g_loaded = 1;
            try_load_mxkw();
            if (!g_use_maca_runtime && g_cuDriverGetVersion != NULL) {
                int ver = 0;
                if (g_cuDriverGetVersion(&ver) == CUDA_SUCCESS) {
                    CL_LOG("driver version=%d.%d vendor=%s",
                           ver / 1000, (ver % 1000) / 10,
                           perftest_cuda_vendor_name(g_vendor));
                }
            }
            return 0;
        }
    }

    CL_LOG("no NVIDIA libcuda / Muxi MACA runtime found");
    return -1;
}

void perftest_cuda_loader_fini(void)
{
    perftest_cuda_ctx_destroy();
    if (g_libmxkw != NULL) {
        dlclose(g_libmxkw);
        g_libmxkw = NULL;
        g_mxkwExportDMABufHandle = NULL;
    }
    if (g_libcuda != NULL) {
        dlclose(g_libcuda);
        g_libcuda = NULL;
    }
    g_loaded = 0;
    g_use_maca_runtime = 0;
    g_vendor = PERFTEST_GPU_VENDOR_NONE;
    clear_cuda_syms();
    CL_LOG("ok");
}

int perftest_cuda_is_loaded(void)
{
    return g_loaded ? 1 : 0;
}

//DEBUG

static void dump_cuda_ptr(const char *name, void *p)
{
    typedef CUresult (*pfn_cuPointerGetAttribute_local)(
        void *data, int attribute, CUdeviceptr ptr);

    enum {
        PTR_ATTR_MEMORY_TYPE      = 2,
        PTR_ATTR_IS_MANAGED       = 8,
        PTR_ATTR_DEVICE_ORDINAL   = 9,
        PTR_ATTR_RANGE_START_ADDR = 11,
        PTR_ATTR_RANGE_SIZE       = 12
    };

    static pfn_cuPointerGetAttribute_local p_cuPointerGetAttribute = NULL;

    CUdeviceptr d;
    CUresult rc;
    int mem_type = 0;
    int dev = -1;
    unsigned int managed = 0;
    void *range_start = NULL;
    size_t range_size = 0;
    const char *type_str = "";

    if (!g_loaded || g_libcuda == NULL) {
        CL_LOG("%s=%p: loader not initialised", name, p);
        return;
    }
    if (g_use_maca_runtime) {
        CL_LOG("%s=%p: skip CUDA pointer dump on MACA native runtime", name, p);
        return;
    }

    if (p == NULL) {
        CL_LOG("%s=NULL", name);
        return;
    }

    if (p_cuPointerGetAttribute == NULL) {
        p_cuPointerGetAttribute =
            (pfn_cuPointerGetAttribute_local)dlsym2(
                g_libcuda, "cuPointerGetAttribute", NULL);

        if (p_cuPointerGetAttribute == NULL) {
            CL_LOG("%s=%p: cuPointerGetAttribute not available", name, p);
            return;
        }
    }

    d = (CUdeviceptr)(uintptr_t)p;

    rc = p_cuPointerGetAttribute(&mem_type, PTR_ATTR_MEMORY_TYPE, d);
    if (rc != CUDA_SUCCESS) {
        CL_LOG("%s=%p MEMORY_TYPE failed rc=%d", name, p, (int)rc);
        return;
    }

    rc = p_cuPointerGetAttribute(&dev, PTR_ATTR_DEVICE_ORDINAL, d);
    if (rc != CUDA_SUCCESS) {
        CL_LOG("%s=%p DEVICE_ORDINAL failed rc=%d", name, p, (int)rc);
    }

    rc = p_cuPointerGetAttribute(&managed, PTR_ATTR_IS_MANAGED, d);
    if (rc != CUDA_SUCCESS) {
        CL_LOG("%s=%p IS_MANAGED failed rc=%d", name, p, (int)rc);
    }

    rc = p_cuPointerGetAttribute(&range_start, PTR_ATTR_RANGE_START_ADDR, d);
    if (rc != CUDA_SUCCESS) {
        CL_LOG("%s=%p RANGE_START failed rc=%d", name, p, (int)rc);
    }

    rc = p_cuPointerGetAttribute(&range_size, PTR_ATTR_RANGE_SIZE, d);
    if (rc != CUDA_SUCCESS) {
        CL_LOG("%s=%p RANGE_SIZE failed rc=%d", name, p, (int)rc);
    }

    if (mem_type == 1) {
        type_str = "HOST";
    } else if (mem_type == 2) {
        type_str = "DEVICE";
    } else if (mem_type == 4) {
        type_str = "UNIFIED";
    } else {
        type_str = "UNKNOWN";
    }

    CL_LOG("%s=%p mem_type=%d(%s) dev=%d managed=%u range_start=%p range_size=%zu",
           name, p, mem_type, type_str, dev, managed, range_start, range_size);
}



/* ---- context ---- */
int perftest_cuda_ctx_create(int dev_id)
{
    CUresult rc;
    CUdevice cu_dev;
    int devcnt = 0;
    char name[128];

    if (!g_loaded) {
        CL_LOG("FAIL: loader not initialised");
        return -1;
    }
    if (g_ctx_owned) {
        CL_LOG("ctx already created (id=%d), reusing", dev_id);
        return 0;
    }

    if (g_use_maca_runtime) {
        if (g_mcInit != NULL) {
            rc = g_mcInit(0);
            if (rc != MACA_SUCCESS) {
                CL_LOG("mcInit -> %d", (int)rc);
                return -1;
            }
        }
        if (g_mcGetDeviceCount != NULL) {
            rc = g_mcGetDeviceCount(&devcnt);
            if (rc != MACA_SUCCESS || devcnt <= 0) {
                CL_LOG("mcGetDeviceCount -> %d cnt=%d", (int)rc, devcnt);
                return -1;
            }
            if (dev_id < 0 || dev_id >= devcnt) {
                CL_LOG("dev_id=%d out of range [0,%d)", dev_id, devcnt);
                return -1;
            }
        }
        if (g_mcSetDevice != NULL) {
            rc = g_mcSetDevice(dev_id);
            if (rc != MACA_SUCCESS) {
                CL_LOG("mcSetDevice -> %d", (int)rc);
                return -1;
            }
        }
        g_ctx = (CUcontext)(uintptr_t)(dev_id + 1);
        g_ctx_owned = 1;
        CL_LOG("ok: MACA runtime dev_id=%d", dev_id);
        return 0;
    }

    rc = g_cuInit(0);
    if (rc != CUDA_SUCCESS) { CL_LOG("cuInit -> %d", (int)rc); return -1; }

    rc = g_cuDeviceGetCount(&devcnt);
    if (rc != CUDA_SUCCESS || devcnt <= 0) {
        CL_LOG("cuDeviceGetCount -> %d cnt=%d", (int)rc, devcnt); return -1;
    }
    if (dev_id < 0 || dev_id >= devcnt) {
        CL_LOG("dev_id=%d out of range [0,%d)", dev_id, devcnt); return -1;
    }
    rc = g_cuDeviceGet(&cu_dev, dev_id);
    if (rc != CUDA_SUCCESS) { CL_LOG("cuDeviceGet -> %d", (int)rc); return -1; }

    name[0] = '\0';
    g_cuDeviceGetName(name, sizeof(name), cu_dev);

    rc = g_cuCtxCreate(&g_ctx, CU_CTX_MAP_HOST, cu_dev);
    if (rc != CUDA_SUCCESS) { CL_LOG("cuCtxCreate -> %d", (int)rc); return -1; }
    rc = g_cuCtxSetCurrent(g_ctx);
    if (rc != CUDA_SUCCESS) { CL_LOG("cuCtxSetCurrent -> %d", (int)rc); return -1; }

    g_ctx_owned = 1;
    CL_LOG("ok: dev_id=%d name=\"%s\" vendor=%s",
           dev_id, name, perftest_cuda_vendor_name(g_vendor));
    return 0;
}

void perftest_cuda_ctx_destroy(void)
{
    if (g_ctx_owned && g_ctx != NULL && g_cuCtxDestroy != NULL &&
        !g_use_maca_runtime) {
        g_cuCtxDestroy(g_ctx);
        CL_LOG("ok");
    }
    g_ctx       = NULL;
    g_ctx_owned = 0;
}

/* ---- memory ---- */
int perftest_cuda_mem_alloc(size_t size, void **out_dptr)
{
    CUdeviceptr d = 0;
    CUresult    rc;

    if (!g_loaded || out_dptr == NULL || !g_ctx) {
        CL_LOG("invalid state: loaded=%d out=%p ctx=%p",
               g_loaded, (void *)out_dptr, g_ctx);
        return -1;
    }

    if (g_use_maca_runtime) {
        void *p = NULL;

        if (perftest_cuda_make_ctx_current() != 0) {
            return -1;
        }
        rc = g_mcMalloc(&p, size);
        if (rc != MACA_SUCCESS || p == NULL) {
            CL_LOG("mcMalloc(%zu) -> %d ptr=%p", size, (int)rc, p);
            return -1;
        }
        *out_dptr = p;
        CL_LOG("ok: MACA dptr=%p size=%zu", *out_dptr, size);
        return 0;
    }

    /*
     * FIX: CUDA context 是 per-thread 状态 (current context), 不是
     * per-process 的。g_ctx 这个 CUcontext 对象本身在哪个线程创建都
     * 一样, 但"哪个 context 对当前线程生效"必须每次用之前显式
     * cuCtxSetCurrent() 一遍——如果调用这个函数的线程跟当初
     * perftest_cuda_ctx_create() 的线程不是同一个 (或者中间有别的
     * CUDA 调用换过 current context), 这里不重新 set 的话,
     * cuMemAlloc 本身可能就在错误的 (甚至没有) context 下执行。
     */
    rc = g_cuCtxSetCurrent(g_ctx);
    if (rc != CUDA_SUCCESS) {
        cl_log_cuda_error("cuCtxSetCurrent", rc);
        return -1;
    }

    rc = g_cuMemAlloc(&d, size);
    if (rc != CUDA_SUCCESS) {
        cl_log_cuda_error("cuMemAlloc", rc);
        return -1;
    }
    *out_dptr = (void *)(uintptr_t)d;
    CL_LOG("ok: ctx=%p dptr=%p cuda_va=0x%llx size=%zu",
           g_ctx, *out_dptr, (unsigned long long)d, size);
    return 0;
}

int perftest_cuda_mem_free(void *dptr)
{
    CUresult rc;
    if (!g_loaded) { return 0; }
    if (dptr == NULL) { return 0; }
    if (g_use_maca_runtime) {
        rc = g_mcFree(dptr);
        if (rc != MACA_SUCCESS) {
            CL_LOG("mcFree(%p) -> %d", dptr, (int)rc);
            return -1;
        }
        return 0;
    }
    rc = g_cuMemFree((CUdeviceptr)(uintptr_t)dptr);
    if (rc != CUDA_SUCCESS) {
        CL_LOG("cuMemFree(%p) -> %d", dptr, (int)rc); return -1;
    }
    return 0;
}

int perftest_cuda_set_sync_memops(void *dptr)
{
    unsigned int flag = 1;
    CUdeviceptr  cuda_ptr;
    CUcontext    current_ctx = NULL;
    CUresult     rc;

    if (!perftest_cuda_needs_sync_memops()) {
        CL_LOG("skip SYNC_MEMOPS on vendor=%s (NVIDIA peermem only)",
               perftest_cuda_vendor_name(perftest_cuda_get_vendor()));
        return 0;
    }

    if (!g_loaded || !g_ctx || !g_cuPointerSetAttribute || !dptr) {
        CL_LOG("invalid state: loaded=%d ctx=%p fn=%p dptr=%p",
               g_loaded, g_ctx, (void *)g_cuPointerSetAttribute, dptr);
        return -1;
    }

    cuda_ptr = (CUdeviceptr)(uintptr_t)dptr;

    /*
     * FIX: 同 perftest_cuda_mem_alloc() —— current context 是 per-thread
     * 状态, 调用这个函数的线程未必就是当初创建/持有 g_ctx 的那个线程
     * (或者线程没变, 但期间别的 CUDA 调用把 current context 换掉了)。
     * 官方文档写得很明确: 如果当前 context 访问不到这个 ptr,
     * cuPointerSetAttribute 就会返回 CUDA_ERROR_INVALID_VALUE —— 这正是
     * 之前反复看到 "-> 1" 最可能的原因, 不是地址/长度/attribute 编号错。
     */
    rc = g_cuCtxSetCurrent(g_ctx);
    if (rc != CUDA_SUCCESS) {
        cl_log_cuda_error("cuCtxSetCurrent", rc);
        return -1;
    }

    /*
     * 显式回读一次, 而不是"set 成功就默认它生效了"——两者理论上应该一致,
     * 但既然上一轮已经在这个调用上栽了一次, 干脆多这一步交叉验证,
     * 免得又靠猜。
     */
    if (g_cuCtxGetCurrent != NULL) {
        rc = g_cuCtxGetCurrent(&current_ctx);
        if (rc != CUDA_SUCCESS) {
            cl_log_cuda_error("cuCtxGetCurrent", rc);
            return -1;
        }
        if (current_ctx != g_ctx) {
            CL_LOG("context mismatch after cuCtxSetCurrent: expected=%p current=%p",
                   g_ctx, current_ctx);
            return -1;
        }
    }

    CL_LOG("cuPointerSetAttribute enter: ctx=%p dptr=%p cuda_ptr=0x%llx "
           "flag=%u attr=%d fn=%p",
           current_ctx, dptr, (unsigned long long)cuda_ptr, flag,
           (int)CU_POINTER_ATTRIBUTE_SYNC_MEMOPS, (void *)g_cuPointerSetAttribute);

    rc = g_cuPointerSetAttribute(&flag, CU_POINTER_ATTRIBUTE_SYNC_MEMOPS, cuda_ptr);
    if (rc != CUDA_SUCCESS) {
        cl_log_cuda_error("cuPointerSetAttribute(SYNC_MEMOPS)", rc);
        return -1;
    }

    CL_LOG("ok: dptr=%p SYNC_MEMOPS=1 (required before handing this ptr to "
           "nvidia_p2p_get_pages(), otherwise kernel-side pin fails -EINVAL)",
           dptr);
    return 0;
}

int perftest_cuda_mem_alloc_host(size_t size, void **out_hptr)
{
    CUresult rc;
    if (!g_loaded || out_hptr == NULL) { CL_LOG("invalid arg"); return -1; }
    if (g_use_maca_runtime) {
        if (g_mcMallocHost == NULL) {
            CL_LOG("mcMallocHost missing, falling back to memalign");
            if (posix_memalign(out_hptr, 4096, size) != 0) {
                return -1;
            }
            memset(*out_hptr, 0, size);
            return 0;
        }
        rc = g_mcMallocHost(out_hptr, size, 0);
        if (rc != MACA_SUCCESS) {
            CL_LOG("mcMallocHost(%zu) -> %d", size, (int)rc);
            return -1;
        }
        CL_LOG("ok: MACA hptr=%p size=%zu", *out_hptr, size);
        return 0;
    }
    rc = g_cuMemAllocHost(out_hptr, size);
    if (rc != CUDA_SUCCESS) {
        CL_LOG("cuMemAllocHost(%zu) -> %d", size, (int)rc); return -1;
    }
    CL_LOG("ok: hptr=%p size=%zu (pinned)", *out_hptr, size);
    return 0;
}

int perftest_cuda_mem_free_host(void *hptr)
{
    CUresult rc;
    if (!g_loaded) { return 0; }
    if (hptr == NULL) { return 0; }
    if (g_use_maca_runtime) {
        if (g_mcFreeHost == NULL) {
            free(hptr);
            return 0;
        }
        rc = g_mcFreeHost(hptr);
        if (rc != MACA_SUCCESS) {
            CL_LOG("mcFreeHost(%p) -> %d", hptr, (int)rc);
            return -1;
        }
        return 0;
    }
    rc = g_cuMemFreeHost(hptr);
    if (rc != CUDA_SUCCESS) {
        CL_LOG("cuMemFreeHost(%p) -> %d", hptr, (int)rc); return -1;
    }
    return 0;
}

/* ---- D2H / H2D ---- */
int perftest_cuda_memcpy_d2h(void *host, void *dev, size_t n)
{
    CUresult rc;
    if (!g_loaded) { CL_LOG("loader not initialised"); return -1; }
    if (perftest_cuda_make_ctx_current() != 0)    return -1; //check if current thread has the context

    if (g_use_maca_runtime) {
        if (g_mcMemcpyDtoH != NULL) {
            rc = g_mcMemcpyDtoH(host, (uint64_t)(uintptr_t)dev, n);
        } else {
            rc = g_mcMemcpy(host, dev, n, MACA_MEMCPY_DEVICE_TO_HOST);
        }
        if (rc != MACA_SUCCESS) {
            CL_LOG("mcMemcpy D2H -> %d (host=%p dev=%p n=%zu)",
                   (int)rc, host, dev, n);
            return -1;
        }
        return 0;
    }

    rc = g_cuMemcpyDtoH(host, (CUdeviceptr)(uintptr_t)dev, n);
    if (rc != CUDA_SUCCESS) {
        CL_LOG("cuMemcpyDtoH -> %d (host=%p dev=%p n=%zu)",
               (int)rc, host, dev, n);
        return -1;
    }
    return 0;
}

int perftest_cuda_memcpy_h2d(void *dev, const void *host, size_t n)
{
    CUresult rc;
    if (!g_loaded) { CL_LOG("loader not initialised"); return -1; }
    if (perftest_cuda_make_ctx_current() != 0)    return -1; //check if current thread has the context

    if (g_use_maca_runtime) {
        if (g_mcMemcpyHtoD != NULL) {
            rc = g_mcMemcpyHtoD((uint64_t)(uintptr_t)dev, host, n);
        } else {
            rc = g_mcMemcpy(dev, host, n, MACA_MEMCPY_HOST_TO_DEVICE);
        }
        if (rc != MACA_SUCCESS) {
            CL_LOG("mcMemcpy H2D -> %d (dev=%p host=%p n=%zu)",
                   (int)rc, dev, host, n);
            return -1;
        }
        return 0;
    }

    rc = g_cuMemcpyHtoD((CUdeviceptr)(uintptr_t)dev, host, n);
    if (rc != CUDA_SUCCESS) {
        CL_LOG("cuMemcpyHtoD -> %d (dev=%p host=%p n=%zu)",
               (int)rc, dev, host, n);
        return -1;
    }
    return 0;
}



/* ---- dma-buf ---- */
int perftest_cuda_get_dmabuf_fd(void *dptr, size_t size,
                                int *out_fd, uint64_t *out_offset)
{
    int      fd = -1;
    CUresult rc;
    if (perftest_cuda_make_ctx_current() != 0)
    return -1;



    dump_cuda_ptr("dmabuf dptr", dptr);

    if (!g_loaded || out_fd == NULL || out_offset == NULL) {
        CL_LOG("invalid arg"); return -1;
    }
    if (g_mxkwExportDMABufHandle != NULL) {
        uint64_t off = 0;
        int mx_fd = -1;
        rc = g_mxkwExportDMABufHandle(dptr, (uint64_t)size, &mx_fd, &off);
        if (rc == MACA_SUCCESS && mx_fd >= 0) {
            *out_fd = mx_fd;
            *out_offset = off;
            CL_LOG("ok: mxkwExportDMABufHandle dptr=%p size=%zu -> fd=%d off=%lu",
                   dptr, size, mx_fd, (unsigned long)off);
            return 0;
        }
        CL_LOG("mxkwExportDMABufHandle -> %d, try cuMemGetHandleForAddressRange",
               (int)rc);
    }
    if (g_cuMemGetHandleForAddressRange == NULL) {
        CL_LOG("FAIL: no dma-buf export "
               "(cuMemGetHandleForAddressRange / mxkwExportDMABufHandle)");
        return -1;
    }
    /*
     * 真实现里要先对齐到 host page size 才行, 这里照搬 linux-rdma cuda_memory.c
     * 的做法: dptr & ~(pagesz-1), 然后偏移 = dptr - aligned.
     */
    {
        size_t      pagesz  = (size_t)sysconf(_SC_PAGESIZE);
        CUdeviceptr d       = (CUdeviceptr)(uintptr_t)dptr;
        CUdeviceptr aligned = d & ~((CUdeviceptr)pagesz - 1);
        uint64_t    offset  = (uint64_t)(d - aligned);
        size_t      asize   = (size + (size_t)offset + pagesz - 1) & ~(pagesz - 1);

        rc = g_cuMemGetHandleForAddressRange(
                &fd, aligned, asize,
                CU_MEM_RANGE_HANDLE_TYPE_DMA_BUF_FD, 0);
        if (rc != CUDA_SUCCESS) {
            CL_LOG("cuMemGetHandleForAddressRange -> %d", (int)rc);
            return -1;
        }
        *out_fd     = fd;
        *out_offset = offset;
        CL_LOG("ok: dptr=%p size=%zu -> fd=%d offset=%lu",
               dptr, size, fd, (unsigned long)offset);
    }
    return 0;
}

