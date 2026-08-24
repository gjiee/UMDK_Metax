/*
 * GeForce / RTX 4090D GDRCopy hook — libgdr_geforce_hook.so
 *
 * ---------------------------------------------------------------------------
 * LD_PRELOAD 是怎么“绑架”符号的（动态链接，不是 patch libcuda）
 * ---------------------------------------------------------------------------
 *
 *   export LD_PRELOAD=$PWD/libgdr_geforce_hook.so
 *   ./gdrcopy_copylat
 *
 * 动态链接器（ld.so）加载顺序变成：
 *
 *   1. 先加载本 .so
 *   2. 再加载可执行文件依赖的 libc / libcuda.so.1 / libgdrapi
 *
 * 解析未定义符号时，从先加载的库开始搜。本文件导出了和 libc/libcuda
 * 同名的全局函数：
 *
 *   ioctl, cuMemAlloc, cuMemAlloc_v2, cuDeviceGetAttribute, ...
 *
 * GDRCopy / libcuda 里的 `call ioctl` / `bl cuMemAlloc` 会先命中我们，
 * 而不是原来的实现。这就是“绑架”：符号拦截，不改二进制文件偏移。
 *
 * 我们自己还要调用“真的”实现时，用 dlsym(RTLD_NEXT, "ioctl")：
 * 从本 .so 之后的库继续搜，拿到 libc 的 ioctl、libcuda 的 cuMemCreate。
 * 千万不要再调自己（否则递归死循环）。
 *
 * 绑不住的情况：对方用 syscall(SYS_ioctl) 而不是 libc ioctl()；
 * 或者静态链接了 CUDA。当前 575 libcuda + GDRCopy 走的是动态符号。
 *
 * urma_perftest 一类自己 dlopen(libcuda) 再 dlsym(handle, "cuMemAlloc_v2")
 * 的 loader，不会走 PLT，所以本文件也拦截 libc 的 dlsym()，把我们导出的
 * CUDA / ioctl 符号还回去。内部查“真符号”一律走 real_dlsym，避免递归。
 *
 * 全局 LD_PRELOAD / /etc/ld.so.preload：
 *   ioctl() 符号仍会被每个进程绑到本 so（无法避免），但函数体只在
 *   NVIDIA RM 设备（major 195 / /dev/nvidiactl / /dev/nvidiaN）上的
 *   RM_ALLOC / RM_ALLOC_OBJECT 成功之后才加锁、拆包。其它 fd（socket、
 *   tty、eventfd、/dev/null）只做一次 type/nr 判断就原样转发。
 *   构造函数不再 dlopen libcuda，也不打印 loaded（除非
 *   GDR_GEFORCE_VERBOSE=1）。
 *
 * 这里没有写死你 GDB 里的 libcuda 地址（2a515c / 0x47d9d0 等）。
 * 写死的是 RM 公开协议；hClient / hMemory / VA 运行时从 ioctl 里抓。
 *
 * ---------------------------------------------------------------------------
 * 功能（GeForce libcuda 从不发 REGISTER_VIDMEM，get_pages 会 0x57）
 * ---------------------------------------------------------------------------
 *   1. cuDeviceGetAttribute(116/110) 强制返回 1
 *   2. cuMemAlloc 改成 VMM，RM 里才有 class 0x40 的 hMemory
 *   3. hook ioctl，记下 CUDA 的 hClient/hDevice/hSubdevice/hVASpace/hMemory
 *   4. 自己 Alloc 0x503c + REGISTER_VA_SPACE + REGISTER_VIDMEM
 *
 * 内核仍用 aikitoria 575.64.05-p2p（GPU↔GPU BAR1 P2P）。本 hook 不管 BAR1 大小。
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

/* -------------------------------------------------------------------------- */
/* NVIDIA RM ioctl 线规（公开头文件，不是 libcuda 反汇编地址）
 *
 * ioctl(fd, _IOWR('F', nr, struct), &packet)
 *   nr=0x2B RM_ALLOC    新建对象，得到 handle
 *   nr=0x2A RM_CONTROL  对已有对象发 cmd（例如 0x503c0104）
 *
 * class 用来认出 CUDA 正在建什么；handle 本身每次运行都不同。
 * -------------------------------------------------------------------------- */

#define NV_IOCTL_MAGIC              'F'
#define NV_ESC_RM_ALLOC_OBJECT      0x28
#define NV_ESC_RM_CONTROL           0x2A
#define NV_ESC_RM_ALLOC             0x2B
#define NVIDIA_RM_MAJOR             195u

#define NV01_DEVICE_0               0x00000080u  /* GPU device */
#define NV20_SUBDEVICE_0            0x00002080u  /* 0x503c 的 parent */
#define FERMI_VASPACE_A             0x000090f1u  /* GPU VA space */
#define NV50_THIRD_PARTY_P2P        0x0000503cu  /* 第三方 P2P 登记本 */
#define NV01_MEMORY_LOCAL_USER      0x00000040u  /* 物理 FB，REGISTER_VIDMEM 要这个 */
#define NV01_MEMORY_LOCAL_PHYSICAL  0x000000c2u
#define NV01_MEMORY_LIST_FBMEM      0x00000082u

#define NV503C_FLAGS_TYPE_BAR1      0x00000001u
#define NV503C_CTRL_CMD_REGISTER_VA_SPACE  0x503c0102u
#define NV503C_CTRL_CMD_REGISTER_VIDMEM    0x503c0104u
#define NV503C_CTRL_CMD_UNREGISTER_VIDMEM  0x503c0105u

#define NV_OK                       0x00000000u
#define NV_ERR_INSERT_DUPLICATE_NAME 0x00000056u

#define GDR_PAGE                    0x10000ull

/* RM_ALLOC 旧包。GDB 里 x/8wx $x2 看到的就是这类字段。 */
typedef struct {
    uint32_t hRoot;
    uint32_t hObjectParent;
    uint32_t hObjectNew;
    uint32_t hClass;
    uint64_t pAllocParms;
    uint32_t paramsSize;
    uint32_t status;
} __attribute__((aligned(8))) nvos21_t;

typedef struct {
    uint32_t hRoot;
    uint32_t hObjectParent;
    uint32_t hObjectNew;
    uint32_t hClass;
    uint64_t pAllocParms;
    uint64_t pRightsRequested;
    uint32_t paramsSize;
    uint32_t flags;
    uint32_t status;
} __attribute__((aligned(8))) nvos64_t;

typedef struct {
    uint32_t hRoot;
    uint32_t hObjectParent;
    uint32_t hObjectNew;
    uint32_t hClass;
    uint32_t status;
} nvos05_t;

/* RM_CONTROL。cmd 在偏移 +8，就是你条件断点 *(uint32_t *)($x2+8) 盯的那个。 */
typedef struct {
    uint32_t hClient;
    uint32_t hObject;
    uint32_t cmd;
    uint32_t flags;
    uint64_t params;
    uint32_t paramsSize;
    uint32_t status;
} __attribute__((aligned(8))) nvos54_t;

typedef struct {
    uint32_t flags;
} nv503c_alloc_t;

typedef struct {
    uint32_t hVASpace;
    uint32_t pad;
    uint64_t vaSpaceToken;
} nv503c_register_vaspace_t;

typedef struct {
    uint32_t hMemory;
    uint32_t pad;
    uint64_t address;
    uint64_t size;
    uint64_t offset;
} nv503c_register_vidmem_t;

typedef struct {
    uint32_t hMemory;
} nv503c_unregister_vidmem_t;

_Static_assert(sizeof(nvos21_t) == 32, "NVOS21 size");
_Static_assert(sizeof(nvos64_t) == 48, "NVOS64 size");
_Static_assert(sizeof(nvos54_t) == 32, "NVOS54 size");
_Static_assert(sizeof(nv503c_register_vaspace_t) == 16, "REGISTER_VA_SPACE size");
_Static_assert(sizeof(nv503c_register_vidmem_t) == 32, "REGISTER_VIDMEM size");

/* -------------------------------------------------------------------------- */
/* Minimal CUDA driver types (CUDA 12.x layout)                               */
/* -------------------------------------------------------------------------- */

typedef int CUresult;
typedef int CUdevice;
typedef unsigned long long CUdeviceptr;
typedef unsigned long long CUmemGenericAllocationHandle;
typedef int CUdevice_attribute;
typedef int CUpointer_attribute;
typedef int CUmemAllocationGranularity_flags;

#define CUDA_SUCCESS 0
#define CUDA_ERROR_NOT_SUPPORTED 801
#define CU_DEVICE_ATTRIBUTE_GPU_DIRECT_RDMA_WITH_CUDA_VMM_SUPPORTED 110
#define CU_DEVICE_ATTRIBUTE_GPU_DIRECT_RDMA_SUPPORTED 116
#define CU_POINTER_ATTRIBUTE_SYNC_MEMOPS 6
#define CU_MEM_ALLOCATION_TYPE_PINNED 0x1
#define CU_MEM_LOCATION_TYPE_DEVICE 0x1
#define CU_MEM_ACCESS_FLAGS_PROT_READWRITE 0x3
#define CU_MEM_ALLOC_GRANULARITY_MINIMUM 0x0

typedef struct {
    int type;
    int id;
} CUmemLocation;

typedef struct {
    unsigned int type;
    unsigned int requestedHandleTypes;
    CUmemLocation location;
    void *win32HandleMetaData;
    struct {
        unsigned char compressionType;
        unsigned char gpuDirectRDMACapable;
        unsigned short usage;
        unsigned char reserved[4];
    } allocFlags;
} CUmemAllocationProp;

typedef struct {
    CUmemLocation location;
    unsigned int flags;
} CUmemAccessDesc;

/* -------------------------------------------------------------------------- */
/* State                                                                      */
/* -------------------------------------------------------------------------- */

typedef CUresult (*fn_cuDeviceGetAttribute)(int *, CUdevice_attribute, CUdevice);
typedef CUresult (*fn_cuCtxGetDevice)(CUdevice *);
typedef CUresult (*fn_cuMemAlloc)(CUdeviceptr *, size_t);
typedef CUresult (*fn_cuMemFree)(CUdeviceptr);
typedef CUresult (*fn_cuMemGetAllocationGranularity)(size_t *, const CUmemAllocationProp *, CUmemAllocationGranularity_flags);
typedef CUresult (*fn_cuMemAddressReserve)(CUdeviceptr *, size_t, size_t, CUdeviceptr, unsigned long long);
typedef CUresult (*fn_cuMemAddressFree)(CUdeviceptr, size_t);
typedef CUresult (*fn_cuMemCreate)(CUmemGenericAllocationHandle *, size_t, const CUmemAllocationProp *, unsigned long long);
typedef CUresult (*fn_cuMemRelease)(CUmemGenericAllocationHandle);
typedef CUresult (*fn_cuMemMap)(CUdeviceptr, size_t, size_t, CUmemGenericAllocationHandle, unsigned long long);
typedef CUresult (*fn_cuMemUnmap)(CUdeviceptr, size_t);
typedef CUresult (*fn_cuMemSetAccess)(CUdeviceptr, size_t, const CUmemAccessDesc *, size_t);
typedef CUresult (*fn_cuPointerSetAttribute)(const void *, CUpointer_attribute, CUdeviceptr);
typedef int (*fn_ioctl)(int, unsigned long, ...);
typedef void *(*fn_dlsym)(void *, const char *);

static fn_ioctl real_ioctl;
static fn_dlsym real_dlsym;
static fn_cuDeviceGetAttribute real_cuDeviceGetAttribute;
static fn_cuCtxGetDevice real_cuCtxGetDevice;
static fn_cuMemAlloc real_cuMemAlloc;
static fn_cuMemAlloc real_cuMemAlloc_v2;
static fn_cuMemFree real_cuMemFree;
static fn_cuMemFree real_cuMemFree_v2;
static fn_cuMemGetAllocationGranularity real_cuMemGetAllocationGranularity;
static fn_cuMemAddressReserve real_cuMemAddressReserve;
static fn_cuMemAddressFree real_cuMemAddressFree;
static fn_cuMemCreate real_cuMemCreate;
static fn_cuMemRelease real_cuMemRelease;
static fn_cuMemMap real_cuMemMap;
static fn_cuMemUnmap real_cuMemUnmap;
static fn_cuMemSetAccess real_cuMemSetAccess;
static fn_cuPointerSetAttribute real_cuPointerSetAttribute;

/*
 * 运行时状态：全是这次进程 ioctl 抓到的，不是写死的。
 * target_gpu 来自环境变量 GPUDIRECT_GPU，默认 0。
 */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static int target_gpu;
static int nv_fd = -1;
static uint32_t h_client;
static uint32_t h_device;
static uint32_t h_subdevice;
static uint32_t h_vaspace;
static int n_device;
static int handles_ready;
static uint32_t h_tpp;
static int tpp_ready;
static int vas_registered;

static int capturing_memory;
static uint32_t captured_hmemory;

static int quiet;
static int verbose;
static int hook_ready;

typedef struct vmm_alloc {
    CUdeviceptr va;
    size_t size;
    CUmemGenericAllocationHandle phys;
    uint32_t hMemory;
    int replaced_cumemalloc;
    struct vmm_alloc *next;
} vmm_alloc_t;

static vmm_alloc_t *vmm_list;

static void logmsg(const char *fmt, ...)
{
    va_list ap;

    if (quiet)
        return;
    fputs("[gdr-geforce] ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static uint64_t round_up(uint64_t x, uint64_t a)
{
    return (x + a - 1) & ~(a - 1);
}

static int is_memory_class(uint32_t hClass)
{
    switch (hClass) {
    case NV01_MEMORY_LOCAL_USER:
    case NV01_MEMORY_LOCAL_PHYSICAL:
    case NV01_MEMORY_LIST_FBMEM:
    case 0x0000003eu:
    case 0x00000071u:
    case 0x0000007au:
    case 0x0000003fu:
        return 1;
    default:
        return (hClass != NV01_DEVICE_0 &&
                hClass != NV20_SUBDEVICE_0 &&
                hClass != FERMI_VASPACE_A &&
                hClass != NV50_THIRD_PARTY_P2P &&
                hClass != 0x0u &&
                hClass != 0x41u);
    }
}

/* 根据 RM_ALLOC 的 class 记账。handle 是 RM 返回的，每次不同。 */
static void track_alloc(int fd, uint32_t hRoot, uint32_t hParent,
                        uint32_t hNew, uint32_t hClass, uint32_t status)
{
    if (status != NV_OK || hNew == 0)
        return;

    if (capturing_memory) {
        int known = (hClass == NV01_MEMORY_LOCAL_USER ||
                     hClass == NV01_MEMORY_LOCAL_PHYSICAL ||
                     hClass == NV01_MEMORY_LIST_FBMEM ||
                     hClass == 0x0000003eu ||
                     hClass == 0x00000071u ||
                     hClass == 0x0000007au);
        if (known || (captured_hmemory == 0 && is_memory_class(hClass))) {
            captured_hmemory = hNew;
            logmsg("captured hMemory=0x%x class=0x%x\n", hNew, hClass);
        }
    }

    if (hClass == NV01_DEVICE_0 && hRoot != 0) {
        if (h_client != hRoot) {
            h_client = hRoot;
            n_device = 0;
            h_device = 0;
            h_subdevice = 0;
            h_vaspace = 0;
            handles_ready = 0;
            tpp_ready = 0;
            vas_registered = 0;
            h_tpp = 0;
        }
        nv_fd = fd;
        if (n_device == target_gpu)
            h_device = hNew;
        n_device++;
        logmsg("DEVICE #%d hClient=0x%x hDevice=0x%x fd=%d\n",
               n_device - 1, h_client, hNew, fd);
    }

    if (hClass == NV20_SUBDEVICE_0 && hParent == h_device && h_device != 0) {
        h_subdevice = hNew;
        logmsg("SUBDEVICE hSubdevice=0x%x\n", h_subdevice);
    }

    if (hClass == FERMI_VASPACE_A && hParent == h_device && h_device != 0 &&
        h_vaspace == 0) {
        h_vaspace = hNew;
        if (h_client && h_device && h_subdevice && h_vaspace) {
            handles_ready = 1;
            logmsg("ready GPU %d client=0x%x device=0x%x subdev=0x%x va=0x%x\n",
                   target_gpu, h_client, h_device, h_subdevice, h_vaspace);
        }
    }
}

static void resolve_real_dlsym(void)
{
    static const char *const vers[] = {
        "GLIBC_2.17",
        "GLIBC_2.2.5",
        "GLIBC_2.34",
        "GLIBC_2.27",
        NULL
    };
    void *libc;
    int i;

    if (real_dlsym)
        return;
    /*
     * 不能调我们自己导出的 dlsym()。dlvsym 不拦截。
     * aarch64 openEuler 是 GLIBC_2.17；x86_64 常见 GLIBC_2.2.5。
     */
    for (i = 0; vers[i]; i++) {
        real_dlsym = (fn_dlsym)dlvsym(RTLD_NEXT, "dlsym", vers[i]);
        if (real_dlsym)
            return;
    }
    libc = dlopen("libc.so.6", RTLD_NOW | RTLD_NOLOAD);
    if (!libc)
        libc = dlopen("libc.so.6", RTLD_NOW);
    if (!libc)
        return;
    for (i = 0; vers[i] && !real_dlsym; i++)
        real_dlsym = (fn_dlsym)dlvsym(libc, "dlsym", vers[i]);
}

static void *lookup_next(const char *name)
{
    resolve_real_dlsym();
    if (!real_dlsym || !name)
        return NULL;
    return real_dlsym(RTLD_NEXT, name);
}

/*
 * 只认会走 RM_ALLOC 的包。其它 ioctl（含 framebuffer 也用 magic 'F' 的
 * FBIO*）直接转发，不 fstat、不加锁。
 */
static int is_rm_alloc_ioctl(unsigned long request)
{
    unsigned int type = (unsigned int)_IOC_TYPE(request);
    unsigned int nr = (unsigned int)_IOC_NR(request);
    unsigned int size = (unsigned int)_IOC_SIZE(request);

    if (type != NV_IOCTL_MAGIC)
        return 0;
    if (nr == NV_ESC_RM_ALLOC)
        return size == sizeof(nvos64_t) || size == sizeof(nvos21_t);
    if (nr == NV_ESC_RM_ALLOC_OBJECT)
        return size == sizeof(nvos05_t);
    return 0;
}

static int nvidia_rm_dev_path(const char *p)
{
    unsigned int maj, mino;
    const char *rest;

    if (!p || !p[0])
        return 0;
    if (sscanf(p, "/dev/char/%u:%u", &maj, &mino) == 2)
        return maj == NVIDIA_RM_MAJOR;
    if (strncmp(p, "/dev/nvidia", 11) != 0)
        return 0;
    rest = p + 11;
    if (strcmp(rest, "ctl") == 0)
        return 1;
    if (rest[0] < '0' || rest[0] > '9')
        return 0;
    do {
        rest++;
    } while (rest[0] >= '0' && rest[0] <= '9');
    return rest[0] == '\0';
}

/* NVIDIA RM 节点：char major 195，或 /dev/nvidiactl、/dev/nvidia0..N。 */
static int is_nvidia_rm_fd(int fd)
{
    struct stat st;
    char link[64];
    char path[256];
    ssize_t n;

    if (fd < 0)
        return 0;
    if (fstat(fd, &st) != 0)
        return 0;
    if (!S_ISCHR(st.st_mode))
        return 0;
    if (major(st.st_rdev) == NVIDIA_RM_MAJOR)
        return 1;

    snprintf(link, sizeof(link), "/proc/self/fd/%d", fd);
    n = readlink(link, path, sizeof(path) - 1);
    if (n <= 0)
        return 0;
    path[n] = '\0';
    return nvidia_rm_dev_path(path);
}

static void after_rm_alloc(int fd, unsigned long request, void *arg)
{
    unsigned int nr = (unsigned int)_IOC_NR(request);
    unsigned int type = (unsigned int)_IOC_TYPE(request);
    unsigned int size = (unsigned int)_IOC_SIZE(request);

    if (type != NV_IOCTL_MAGIC || arg == NULL)
        return;

    if (nr == NV_ESC_RM_ALLOC) {
        if (size == sizeof(nvos64_t)) {
            nvos64_t *p = arg;
            track_alloc(fd, p->hRoot, p->hObjectParent, p->hObjectNew,
                        p->hClass, p->status);
        } else if (size == sizeof(nvos21_t)) {
            nvos21_t *p = arg;
            track_alloc(fd, p->hRoot, p->hObjectParent, p->hObjectNew,
                        p->hClass, p->status);
        }
        return;
    }

    if (nr == NV_ESC_RM_ALLOC_OBJECT && size == sizeof(nvos05_t)) {
        nvos05_t *p = arg;
        track_alloc(fd, p->hRoot, p->hObjectParent, p->hObjectNew,
                    p->hClass, p->status);
    }
}

static int call_ioctl(int fd, unsigned long request, void *arg)
{
    if (!real_ioctl)
        real_ioctl = (fn_ioctl)lookup_next("ioctl");
    if (!real_ioctl) {
        errno = ENOSYS;
        return -1;
    }
    return real_ioctl(fd, request, arg);
}

/*
 * 绑架 libc 的 ioctl：符号拦截无法按 fd 卸载，但解析只针对 NV RM 设备。
 * 非 RM_ALLOC 形状的请求（socket/tty/eventfd/FBIO*）立刻转发，不加锁。
 * 形状像 RM_ALLOC 时先做真 ioctl，成功后再 fstat，确认是 nvidiactl/nvidiaN
 * 才拆包。我们自己发的 0x503c 走 call_ioctl，不进这里。
 */
int ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    void *arg;
    int ret;

    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);

    if (!is_rm_alloc_ioctl(request))
        return call_ioctl(fd, request, arg);

    ret = call_ioctl(fd, request, arg);
    if (ret != 0 || !is_nvidia_rm_fd(fd))
        return ret;

    pthread_mutex_lock(&lock);
    after_rm_alloc(fd, request, arg);
    pthread_mutex_unlock(&lock);
    return ret;
}

/* 我们主动发的 RM_ALLOC。走 call_ioctl，避免再进符号绑架后的逻辑死循环。 */
static uint32_t rm_alloc(uint32_t hRoot, uint32_t hParent, uint32_t hClass,
                         void *parms, uint32_t parmsSize, uint32_t *hOut)
{
    nvos64_t p;
    unsigned long req;
    int rc;

    memset(&p, 0, sizeof(p));
    p.hRoot = hRoot;
    p.hObjectParent = hParent;
    p.hObjectNew = 0;
    p.hClass = hClass;
    p.pAllocParms = (uint64_t)(uintptr_t)parms;
    p.paramsSize = parmsSize;

    req = _IOWR(NV_IOCTL_MAGIC, NV_ESC_RM_ALLOC, nvos64_t);
    rc = call_ioctl(nv_fd, req, &p);
    if (rc == 0 && p.status == NV_OK && p.hObjectNew != 0) {
        *hOut = p.hObjectNew;
        return NV_OK;
    }

    if (rc != 0 || p.status != NV_OK) {
        nvos21_t q;
        static uint32_t next_handle = 0x00e00001u;

        memset(&q, 0, sizeof(q));
        q.hRoot = hRoot;
        q.hObjectParent = hParent;
        q.hObjectNew = next_handle++;
        q.hClass = hClass;
        q.pAllocParms = (uint64_t)(uintptr_t)parms;
        q.paramsSize = parmsSize;
        req = _IOWR(NV_IOCTL_MAGIC, NV_ESC_RM_ALLOC, nvos21_t);
        rc = call_ioctl(nv_fd, req, &q);
        if (rc == 0 && q.status == NV_OK) {
            *hOut = q.hObjectNew;
            return NV_OK;
        }
        return q.status ? q.status : (uint32_t)(rc < 0 ? (uint32_t)errno : 0xffffffffu);
    }

    return p.status ? p.status : 0xffffffffu;
}

/* 我们主动发的 RM_CONTROL：REGISTER_VA_SPACE / REGISTER_VIDMEM。 */
static uint32_t rm_ctrl(uint32_t hCli, uint32_t hObj, uint32_t cmd,
                        void *params, uint32_t paramsSize)
{
    nvos54_t c;
    unsigned long req;
    int rc;

    memset(&c, 0, sizeof(c));
    c.hClient = hCli;
    c.hObject = hObj;
    c.cmd = cmd;
    c.params = (uint64_t)(uintptr_t)params;
    c.paramsSize = paramsSize;
    req = _IOWR(NV_IOCTL_MAGIC, NV_ESC_RM_CONTROL, nvos54_t);
    rc = call_ioctl(nv_fd, req, &c);
    if (rc < 0)
        return (uint32_t)errno;
    return c.status;
}

/* 每个 CUDA client 只建一本 0x503c，再登记一次 VA space。 */
static int ensure_tpp_locked(void)
{
    nv503c_alloc_t ap;
    nv503c_register_vaspace_t rv;
    uint32_t st;

    if (tpp_ready)
        return 0;
    if (nv_fd < 0 || h_client == 0 || h_subdevice == 0) {
        logmsg("cannot alloc 0x503c yet (fd=%d client=0x%x subdev=0x%x)\n",
               nv_fd, h_client, h_subdevice);
        return -1;
    }

    memset(&ap, 0, sizeof(ap));
    ap.flags = NV503C_FLAGS_TYPE_BAR1;
    st = rm_alloc(h_client, h_subdevice, NV50_THIRD_PARTY_P2P,
                  &ap, sizeof(ap), &h_tpp);
    if (st != NV_OK) {
        logmsg("RM_ALLOC 0x503c failed status=0x%x\n", st);
        return -1;
    }
    tpp_ready = 1;
    logmsg("allocated NV50_THIRD_PARTY_P2P hTPP=0x%x\n", h_tpp);

    memset(&rv, 0, sizeof(rv));
    rv.hVASpace = h_vaspace;
    st = rm_ctrl(h_client, h_tpp, NV503C_CTRL_CMD_REGISTER_VA_SPACE,
                 &rv, sizeof(rv));
    if (st != NV_OK && st != NV_ERR_INSERT_DUPLICATE_NAME) {
        logmsg("REGISTER_VA_SPACE hVASpace=0x%x failed status=0x%x\n",
               h_vaspace, st);
        return -1;
    }
    vas_registered = 1;
    logmsg("REGISTER_VA_SPACE hVASpace=0x%x token=0x%llx status=0x%x\n",
           h_vaspace, (unsigned long long)rv.vaSpaceToken, st);
    return 0;
}

/* 把 (hMemory, CUDA VA, size) 写进登记本。地址/长度必须 64K 对齐。 */
static int register_vidmem_locked(uint64_t va, uint64_t size, uint32_t hMemory)
{
    nv503c_register_vidmem_t vm;
    uint32_t st;
    uint64_t reg_va;
    uint64_t reg_sz;

    if (hMemory == 0) {
        logmsg("REGISTER_VIDMEM skipped: no hMemory for va=0x%llx\n",
               (unsigned long long)va);
        return -1;
    }
    if (ensure_tpp_locked() != 0)
        return -1;

    reg_va = va & ~(GDR_PAGE - 1);
    reg_sz = round_up(size + (va - reg_va), GDR_PAGE);
    if (reg_sz == 0)
        reg_sz = GDR_PAGE;

    memset(&vm, 0, sizeof(vm));
    vm.hMemory = hMemory;
    vm.address = reg_va;
    vm.size = reg_sz;
    vm.offset = 0;
    st = rm_ctrl(h_client, h_tpp, NV503C_CTRL_CMD_REGISTER_VIDMEM,
                 &vm, sizeof(vm));
    if (st != NV_OK && st != NV_ERR_INSERT_DUPLICATE_NAME) {
        logmsg("REGISTER_VIDMEM hMemory=0x%x va=0x%llx size=0x%llx failed status=0x%x\n",
               hMemory, (unsigned long long)reg_va, (unsigned long long)reg_sz, st);
        return -1;
    }
    logmsg("REGISTER_VIDMEM hMemory=0x%x va=0x%llx size=0x%llx status=0x%x\n",
           hMemory, (unsigned long long)reg_va, (unsigned long long)reg_sz, st);
    return 0;
}

static void remember_vmm(CUdeviceptr va, size_t size,
                         CUmemGenericAllocationHandle phys, uint32_t hMemory,
                         int replaced)
{
    vmm_alloc_t *n = calloc(1, sizeof(*n));

    if (!n)
        return;
    n->va = va;
    n->size = size;
    n->phys = phys;
    n->hMemory = hMemory;
    n->replaced_cumemalloc = replaced;
    n->next = vmm_list;
    vmm_list = n;
}

static vmm_alloc_t *take_vmm(CUdeviceptr va)
{
    vmm_alloc_t **pp;
    vmm_alloc_t *n;

    for (pp = &vmm_list; *pp; pp = &(*pp)->next) {
        if ((*pp)->va == va) {
            n = *pp;
            *pp = n->next;
            return n;
        }
    }
    return NULL;
}

static int owns_va_locked(CUdeviceptr ptr)
{
    vmm_alloc_t *n;

    for (n = vmm_list; n; n = n->next) {
        if (ptr >= n->va && ptr < n->va + n->size)
            return 1;
    }
    return 0;
}

/*
 * 拿“真的” CUDA 符号：RTLD_NEXT 跳过本 so，避免拿到我们自己的 cuMemAlloc。
 * 若 libcuda 还没加载，再 dlopen("libcuda.so.1")。
 */
static void *load_cuda_sym(const char *name)
{
    void *h;
    void *s;

    /* 必须用 real_dlsym：拦截后的 dlsym(libcuda, "cuMemAlloc_v2") 会返回我们自己。 */
    s = lookup_next(name);
    if (s)
        return s;
    h = dlopen("libcuda.so.1", RTLD_LAZY | RTLD_NOLOAD);
    if (!h)
        h = dlopen("libcuda.so.1", RTLD_LAZY);
    if (!h)
        return NULL;
    resolve_real_dlsym();
    return real_dlsym ? real_dlsym(h, name) : NULL;
}

static void resolve_cuda(void)
{
    if (real_cuMemAlloc_v2)
        return;
    real_cuDeviceGetAttribute = load_cuda_sym("cuDeviceGetAttribute");
    real_cuCtxGetDevice = load_cuda_sym("cuCtxGetDevice");
    real_cuMemAlloc_v2 = load_cuda_sym("cuMemAlloc_v2");
    real_cuMemAlloc = load_cuda_sym("cuMemAlloc");
    real_cuMemFree_v2 = load_cuda_sym("cuMemFree_v2");
    real_cuMemFree = load_cuda_sym("cuMemFree");
    real_cuMemGetAllocationGranularity = load_cuda_sym("cuMemGetAllocationGranularity");
    real_cuMemAddressReserve = load_cuda_sym("cuMemAddressReserve");
    real_cuMemAddressFree = load_cuda_sym("cuMemAddressFree");
    real_cuMemCreate = load_cuda_sym("cuMemCreate");
    real_cuMemRelease = load_cuda_sym("cuMemRelease");
    real_cuMemMap = load_cuda_sym("cuMemMap");
    real_cuMemUnmap = load_cuda_sym("cuMemUnmap");
    real_cuMemSetAccess = load_cuda_sym("cuMemSetAccess");
    real_cuPointerSetAttribute = load_cuda_sym("cuPointerSetAttribute");
    if (!real_ioctl)
        real_ioctl = (fn_ioctl)lookup_next("ioctl");
}

/*
 * 替代 cuMemAlloc：VMM 会 RM_ALLOC class 0x40，ioctl hook 才能抓到 hMemory。
 * 普通 cuMemAlloc 常走 UVM，RM 里没有可 REGISTER_VIDMEM 的对象。
 */
static CUresult vmm_alloc_and_register(CUdeviceptr *dptr, size_t bytesize)
{
    CUmemAllocationProp prop;
    CUmemAccessDesc access;
    CUdevice dev = 0;
    CUdeviceptr va = 0;
    CUmemGenericAllocationHandle phys = 0;
    size_t gran = 0;
    size_t alloc_sz;
    CUresult r;
    uint32_t hMemory = 0;
    int sync = 1;

    resolve_cuda();
    if (!real_cuMemCreate || !real_cuMemMap || !real_cuMemAddressReserve ||
        !real_cuCtxGetDevice) {
        logmsg("VMM symbols missing, cannot replace cuMemAlloc\n");
        return CUDA_ERROR_NOT_SUPPORTED;
    }

    r = real_cuCtxGetDevice(&dev);
    if (r != CUDA_SUCCESS)
        return r;

    memset(&prop, 0, sizeof(prop));
    prop.type = CU_MEM_ALLOCATION_TYPE_PINNED;
    prop.location.type = CU_MEM_LOCATION_TYPE_DEVICE;
    prop.location.id = (int)dev;
    prop.allocFlags.gpuDirectRDMACapable = 1;

    r = real_cuMemGetAllocationGranularity(&gran, &prop,
                                           CU_MEM_ALLOC_GRANULARITY_MINIMUM);
    if (r != CUDA_SUCCESS) {
        prop.allocFlags.gpuDirectRDMACapable = 0;
        r = real_cuMemGetAllocationGranularity(&gran, &prop,
                                               CU_MEM_ALLOC_GRANULARITY_MINIMUM);
        if (r != CUDA_SUCCESS)
            return r;
    }
    if (gran < GDR_PAGE)
        gran = GDR_PAGE;

    alloc_sz = round_up(bytesize, gran);
    r = real_cuMemAddressReserve(&va, alloc_sz, gran, 0, 0);
    if (r != CUDA_SUCCESS)
        return r;

    pthread_mutex_lock(&lock);
    capturing_memory = 1;
    captured_hmemory = 0;
    pthread_mutex_unlock(&lock);

    r = real_cuMemCreate(&phys, alloc_sz, &prop, 0);
    if (r != CUDA_SUCCESS && prop.allocFlags.gpuDirectRDMACapable) {
        logmsg("cuMemCreate RDMA-capable failed (%d), retry without flag\n", r);
        prop.allocFlags.gpuDirectRDMACapable = 0;
        r = real_cuMemCreate(&phys, alloc_sz, &prop, 0);
    }
    if (r != CUDA_SUCCESS) {
        real_cuMemAddressFree(va, alloc_sz);
        pthread_mutex_lock(&lock);
        capturing_memory = 0;
        pthread_mutex_unlock(&lock);
        return r;
    }

    pthread_mutex_lock(&lock);
    capturing_memory = 0;
    hMemory = captured_hmemory;
    pthread_mutex_unlock(&lock);

    r = real_cuMemMap(va, alloc_sz, 0, phys, 0);
    if (r != CUDA_SUCCESS) {
        real_cuMemRelease(phys);
        real_cuMemAddressFree(va, alloc_sz);
        return r;
    }

    memset(&access, 0, sizeof(access));
    access.location = prop.location;
    access.flags = CU_MEM_ACCESS_FLAGS_PROT_READWRITE;
    r = real_cuMemSetAccess(va, alloc_sz, &access, 1);
    if (r != CUDA_SUCCESS) {
        real_cuMemUnmap(va, alloc_sz);
        real_cuMemRelease(phys);
        real_cuMemAddressFree(va, alloc_sz);
        return r;
    }

    if (real_cuPointerSetAttribute) {
        CUresult sync_r = real_cuPointerSetAttribute(&sync,
                                                     CU_POINTER_ATTRIBUTE_SYNC_MEMOPS, va);
        if (sync_r != CUDA_SUCCESS)
            logmsg("SYNC_MEMOPS on VMM va=0x%llx returned %d (ignored; GeForce)\n",
                   (unsigned long long)va, sync_r);
    }

    pthread_mutex_lock(&lock);
    register_vidmem_locked((uint64_t)va, (uint64_t)alloc_sz, hMemory);
    remember_vmm(va, alloc_sz, phys, hMemory, 1);
    pthread_mutex_unlock(&lock);

    *dptr = va;
    logmsg("cuMemAlloc -> VMM va=0x%llx size=0x%llx hMemory=0x%x\n",
           (unsigned long long)va, (unsigned long long)alloc_sz, hMemory);
    return CUDA_SUCCESS;
}

/* 绑架 libcuda 的 cuDeviceGetAttribute。116/110 强制为 1，代替 nop 那条 tbz。 */
CUresult cuDeviceGetAttribute(int *pi, CUdevice_attribute attrib, CUdevice dev)
{
    CUresult r;

    resolve_cuda();
    if (!real_cuDeviceGetAttribute)
        return CUDA_ERROR_NOT_SUPPORTED;
    r = real_cuDeviceGetAttribute(pi, attrib, dev);
    if (pi && (attrib == CU_DEVICE_ATTRIBUTE_GPU_DIRECT_RDMA_SUPPORTED ||
               attrib == CU_DEVICE_ATTRIBUTE_GPU_DIRECT_RDMA_WITH_CUDA_VMM_SUPPORTED)) {
        logmsg("cuDeviceGetAttribute(%d) was %d -> 1\n", attrib, *pi);
        *pi = 1;
        return CUDA_SUCCESS;
    }
    return r;
}

/* 绑架 cuMemAlloc(_v2)：GDRCopy 调这个名字，实际走 VMM+登记。 */
CUresult cuMemAlloc_v2(CUdeviceptr *dptr, size_t bytesize)
{
    CUresult r = vmm_alloc_and_register(dptr, bytesize);

    if (r == CUDA_SUCCESS)
        return r;
    resolve_cuda();
    logmsg("VMM replace failed (%d), falling back to real cuMemAlloc\n", r);
    if (real_cuMemAlloc_v2)
        return real_cuMemAlloc_v2(dptr, bytesize);
    if (real_cuMemAlloc)
        return real_cuMemAlloc(dptr, bytesize);
    return r;
}

CUresult cuMemAlloc(CUdeviceptr *dptr, size_t bytesize)
{
    return cuMemAlloc_v2(dptr, bytesize);
}

/* 绑架 SYNC_MEMOPS：4090D 上 VMM 指针会返回 801，GDRCopy 会在 pin 前 assert。 */
CUresult cuPointerSetAttribute(const void *value, CUpointer_attribute attrib,
                               CUdeviceptr ptr)
{
    CUresult r;
    int owned;

    resolve_cuda();
    if (!real_cuPointerSetAttribute)
        return CUDA_ERROR_NOT_SUPPORTED;

    r = real_cuPointerSetAttribute(value, attrib, ptr);
    if (attrib != CU_POINTER_ATTRIBUTE_SYNC_MEMOPS)
        return r;
    if (r == CUDA_SUCCESS)
        return r;

    pthread_mutex_lock(&lock);
    owned = owns_va_locked(ptr);
    pthread_mutex_unlock(&lock);

    logmsg("cuPointerSetAttribute(SYNC_MEMOPS) ptr=0x%llx -> %d%s\n",
           (unsigned long long)ptr, r, owned ? " (owned VMM, return SUCCESS)" : "");
    if (owned || r == CUDA_ERROR_NOT_SUPPORTED)
        return CUDA_SUCCESS;
    return r;
}

/* 绑架 cuMemCreate：打开抓 hMemory 窗口；RDMA 旗标失败则去掉再试。 */
CUresult cuMemCreate(CUmemGenericAllocationHandle *handle, size_t size,
                     const CUmemAllocationProp *prop, unsigned long long flags)
{
    CUresult r;
    CUmemAllocationProp copy;

    resolve_cuda();
    if (!real_cuMemCreate)
        return CUDA_ERROR_NOT_SUPPORTED;

    pthread_mutex_lock(&lock);
    capturing_memory = 1;
    captured_hmemory = 0;
    pthread_mutex_unlock(&lock);

    r = real_cuMemCreate(handle, size, prop, flags);
    if (r != CUDA_SUCCESS && prop && prop->allocFlags.gpuDirectRDMACapable) {
        copy = *prop;
        copy.allocFlags.gpuDirectRDMACapable = 0;
        logmsg("cuMemCreate RDMA-capable failed (%d), retry without flag\n", r);
        r = real_cuMemCreate(handle, size, &copy, flags);
    }

    pthread_mutex_lock(&lock);
    capturing_memory = 0;
    pthread_mutex_unlock(&lock);
    return r;
}

/* 绑架 cuMemMap：GDRCopy 自己走 VMM 时，map 成功后立刻 REGISTER_VIDMEM。 */
CUresult cuMemMap(CUdeviceptr ptr, size_t size, size_t offset,
                  CUmemGenericAllocationHandle handle, unsigned long long flags)
{
    CUresult r;
    uint32_t hMemory;

    resolve_cuda();
    if (!real_cuMemMap)
        return CUDA_ERROR_NOT_SUPPORTED;
    r = real_cuMemMap(ptr, size, offset, handle, flags);
    if (r != CUDA_SUCCESS)
        return r;

    pthread_mutex_lock(&lock);
    hMemory = captured_hmemory;
    register_vidmem_locked((uint64_t)ptr, (uint64_t)size, hMemory);
    remember_vmm(ptr, size, handle, hMemory, 0);
    pthread_mutex_unlock(&lock);
    return r;
}

static CUresult free_replaced(CUdeviceptr dptr)
{
    vmm_alloc_t *n;
    CUresult r = CUDA_SUCCESS;

    pthread_mutex_lock(&lock);
    n = take_vmm(dptr);
    pthread_mutex_unlock(&lock);
    if (!n)
        return CUDA_ERROR_NOT_SUPPORTED;

    if (n->replaced_cumemalloc) {
        if (real_cuMemUnmap)
            r = real_cuMemUnmap(n->va, n->size);
        if (real_cuMemRelease && n->phys)
            real_cuMemRelease(n->phys);
        if (real_cuMemAddressFree)
            real_cuMemAddressFree(n->va, n->size);
    }
    free(n);
    return r == CUDA_SUCCESS ? CUDA_SUCCESS : r;
}

CUresult cuMemFree_v2(CUdeviceptr dptr)
{
    CUresult r;

    resolve_cuda();
    r = free_replaced(dptr);
    if (r == CUDA_SUCCESS)
        return r;
    if (real_cuMemFree_v2)
        return real_cuMemFree_v2(dptr);
    if (real_cuMemFree)
        return real_cuMemFree(dptr);
    return r;
}

CUresult cuMemFree(CUdeviceptr dptr)
{
    return cuMemFree_v2(dptr);
}

/*
 * .so 一被加载就跑（在 main 之前）。全局 preload 时每个进程都会进这里：
 * 只读环境变量、解析 libc ioctl/dlsym。禁止 dlopen libcuda。
 */
__attribute__((constructor))
static void gdr_geforce_init(void)
{
    const char *e;

    e = getenv("GDR_GEFORCE_QUIET");
    quiet = (e && e[0] != '0');
    e = getenv("GDR_GEFORCE_VERBOSE");
    verbose = (e && e[0] != '0');
    e = getenv("GPUDIRECT_GPU");
    if (!e)
        e = getenv("GDR_GEFORCE_GPU");
    if (e)
        target_gpu = atoi(e);

    resolve_real_dlsym();
    if (!real_ioctl)
        real_ioctl = (fn_ioctl)lookup_next("ioctl");
    hook_ready = 1;
    if (verbose)
        logmsg("loaded (GPU %d). ioctl inspects only NVIDIA RM fds; cuMemAlloc->VMM\n",
               target_gpu);
}

/*
 * urma_perftest 的 cuda-loader：dlopen("libcuda.so.1") 后
 * dlsym(handle, "cuMemAlloc_v2")，不走 PLT。这里把我们劫持的名字还回去。
 * RTLD_NEXT 不改，内部 lookup_next / load_cuda_sym 才能拿到真实现。
 */
void *dlsym(void *handle, const char *name)
{
    resolve_real_dlsym();
    if (!real_dlsym)
        return NULL;
    if (handle != RTLD_NEXT) {
        if (strcmp(name, "ioctl") == 0)
            return (void *)(uintptr_t)ioctl;
        if (strcmp(name, "cuMemAlloc") == 0)
            return (void *)(uintptr_t)cuMemAlloc;
        if (strcmp(name, "cuMemAlloc_v2") == 0)
            return (void *)(uintptr_t)cuMemAlloc_v2;
        if (strcmp(name, "cuMemFree") == 0)
            return (void *)(uintptr_t)cuMemFree;
        if (strcmp(name, "cuMemFree_v2") == 0)
            return (void *)(uintptr_t)cuMemFree_v2;
        if (strcmp(name, "cuDeviceGetAttribute") == 0)
            return (void *)(uintptr_t)cuDeviceGetAttribute;
        if (strcmp(name, "cuPointerSetAttribute") == 0)
            return (void *)(uintptr_t)cuPointerSetAttribute;
        if (strcmp(name, "cuMemCreate") == 0)
            return (void *)(uintptr_t)cuMemCreate;
        if (strcmp(name, "cuMemMap") == 0)
            return (void *)(uintptr_t)cuMemMap;
    }
    return real_dlsym(handle, name);
}
