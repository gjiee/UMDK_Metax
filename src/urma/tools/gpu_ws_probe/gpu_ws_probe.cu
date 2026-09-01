/*
 * A100 / GPUDirect working-set probe (no URMA, no UMMU).
 *
 * Distinguishes:
 *   (1) per-chunk length  — 512K copy always slow, 128K always fast
 *   (2) working-set window — 128K copy is fast only when the GPU buffer
 *       being touched is small (~256K); same 128K in a 1MB+ buffer is slow
 *   (3) HBM vs BAR        — D2D (HBM) has no cliff; D2H/H2D (GPU CE over
 *       the GPU's own PCIe BAR) reproduces the URMA 26G→17G step
 *
 * Host DRAM via the UB NIC is a different path (already full-rate). This
 * tool never touches udmac; it only uses the NVIDIA copy engine.
 *
 * Build on the GPU node (isolate not required):
 *   nvcc -O2 -std=c++14 gpu_ws_probe.cu -o gpu_ws_probe
 *   ./gpu_ws_probe                  # --preset prove
 *   ./gpu_ws_probe --sweep
 *   ./gpu_ws_probe --kind d2h --pattern random --ws 4M --chunk 128K
 *
 * UMDK_GDR_HOOK=0 recommended on A100 (do not LD_PRELOAD the GeForce hook).
 */

#include <cuda_runtime.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define GPU_PAGE (64u * 1024u)

enum kind_t { KIND_D2D, KIND_D2H, KIND_H2D };
enum pat_t { PAT_HAMMER, PAT_WALK, PAT_RANDOM };

static void die_cuda(const char *what, cudaError_t e)
{
    std::fprintf(stderr, "FAIL %s: %s\n", what, cudaGetErrorString(e));
    std::exit(1);
}

#define CUDACHECK(expr)                                                        \
    do {                                                                       \
        cudaError_t _e = (expr);                                               \
        if (_e != cudaSuccess)                                                 \
            die_cuda(#expr, _e);                                               \
    } while (0)

static uint64_t parse_size(const char *s)
{
    char *end = NULL;
    double v = std::strtod(s, &end);
    uint64_t mul = 1;

    if (end != NULL && *end != '\0') {
        if (*end == 'K' || *end == 'k') {
            mul = 1024ull;
            end++;
        } else if (*end == 'M' || *end == 'm') {
            mul = 1024ull * 1024ull;
            end++;
        } else if (*end == 'G' || *end == 'g') {
            mul = 1024ull * 1024ull * 1024ull;
            end++;
        }
        if (*end == 'B' || *end == 'b') {
            end++;
        }
        if (*end != '\0') {
            std::fprintf(stderr, "bad size: %s\n", s);
            std::exit(1);
        }
    }
    if (v <= 0) {
        std::fprintf(stderr, "bad size: %s\n", s);
        std::exit(1);
    }
    return (uint64_t)(v * (double)mul);
}

static const char *kind_name(kind_t k)
{
    return (k == KIND_D2D) ? "d2d" : (k == KIND_D2H) ? "d2h" : "h2d";
}

static const char *pat_name(pat_t p)
{
    return (p == PAT_HAMMER) ? "hammer" : (p == PAT_WALK) ? "walk" : "random";
}

static kind_t parse_kind(const char *s)
{
    if (std::strcmp(s, "d2d") == 0) {
        return KIND_D2D;
    }
    if (std::strcmp(s, "d2h") == 0) {
        return KIND_D2H;
    }
    if (std::strcmp(s, "h2d") == 0) {
        return KIND_H2D;
    }
    std::fprintf(stderr, "kind must be d2d|d2h|h2d\n");
    std::exit(1);
}

static pat_t parse_pat(const char *s)
{
    if (std::strcmp(s, "hammer") == 0) {
        return PAT_HAMMER;
    }
    if (std::strcmp(s, "walk") == 0) {
        return PAT_WALK;
    }
    if (std::strcmp(s, "random") == 0) {
        return PAT_RANDOM;
    }
    std::fprintf(stderr, "pattern must be hammer|walk|random\n");
    std::exit(1);
}

static void fill_offsets(std::vector<size_t> *off, pat_t pat, size_t ws,
                         size_t chunk, uint32_t n)
{
    size_t slots = (chunk == 0 || ws < chunk) ? 1 : (ws / chunk);

    off->resize(n);
    if (pat == PAT_HAMMER || slots <= 1) {
        for (uint32_t i = 0; i < n; i++) {
            (*off)[i] = 0;
        }
        return;
    }
    if (pat == PAT_WALK) {
        for (uint32_t i = 0; i < n; i++) {
            (*off)[i] = (size_t)(i % slots) * chunk;
        }
        return;
    }
    /* random: 64K-aligned slot inside ws, not the same slot twice in a row */
    uint64_t x = 0x9e3779b97f4a7c15ull;
    size_t prev = (size_t)-1;
    for (uint32_t i = 0; i < n; i++) {
        x = x * 6364136223846793005ull + 1;
        size_t slot = (size_t)((x >> 17) % slots);
        if (slot == prev && slots > 1) {
            slot = (slot + 1 + (size_t)((x >> 7) % (slots - 1))) % slots;
        }
        prev = slot;
        (*off)[i] = slot * chunk;
    }
}

struct run_result {
    double mib_s;
    double ms;
    uint64_t bytes;
    uint32_t iters;
};

static run_result run_one(kind_t kind, pat_t pat, size_t ws, size_t chunk,
                          int depth, double min_ms, uint32_t min_iters)
{
    void *d_src = NULL;
    void *d_dst = NULL;
    void *h_pin = NULL;
    cudaStream_t st;
    cudaEvent_t ev0, ev1;

    CUDACHECK(cudaMalloc(&d_src, ws));
    CUDACHECK(cudaMemset(d_src, 0x5a, ws));
    if (kind == KIND_D2D) {
        CUDACHECK(cudaMalloc(&d_dst, ws));
        CUDACHECK(cudaMemset(d_dst, 0, ws));
    } else {
        CUDACHECK(cudaMallocHost(&h_pin, ws));
        std::memset(h_pin, 0x3c, ws);
    }
    CUDACHECK(cudaStreamCreate(&st));
    CUDACHECK(cudaEventCreate(&ev0));
    CUDACHECK(cudaEventCreate(&ev1));

    uint32_t n = min_iters;
    if (n < 256) {
        n = 256;
    }
    if (n % (uint32_t)depth) {
        n += (uint32_t)depth - (n % (uint32_t)depth);
    }

    std::vector<size_t> off;
    fill_offsets(&off, pat, ws, chunk, n);

    /* warmup */
    for (uint32_t i = 0; i < (uint32_t)depth * 4u && i < n; i++) {
        size_t o = off[i];
        if (kind == KIND_D2D) {
            CUDACHECK(cudaMemcpyAsync((char *)d_dst + o, (char *)d_src + o, chunk,
                                      cudaMemcpyDeviceToDevice, st));
        } else if (kind == KIND_D2H) {
            CUDACHECK(cudaMemcpyAsync((char *)h_pin + o, (char *)d_src + o, chunk,
                                      cudaMemcpyDeviceToHost, st));
        } else {
            CUDACHECK(cudaMemcpyAsync((char *)d_src + o, (char *)h_pin + o, chunk,
                                      cudaMemcpyHostToDevice, st));
        }
    }
    CUDACHECK(cudaStreamSynchronize(st));

    /* grow n until we cover min_ms (first guess, then one retune) */
    run_result r = {};
    for (int pass = 0; pass < 2; pass++) {
        fill_offsets(&off, pat, ws, chunk, n);
        CUDACHECK(cudaEventRecord(ev0, st));
        for (uint32_t i = 0; i < n; i++) {
            size_t o = off[i];
            if (kind == KIND_D2D) {
                CUDACHECK(cudaMemcpyAsync((char *)d_dst + o, (char *)d_src + o,
                                          chunk, cudaMemcpyDeviceToDevice, st));
            } else if (kind == KIND_D2H) {
                CUDACHECK(cudaMemcpyAsync((char *)h_pin + o, (char *)d_src + o,
                                          chunk, cudaMemcpyDeviceToHost, st));
            } else {
                CUDACHECK(cudaMemcpyAsync((char *)d_src + o, (char *)h_pin + o,
                                          chunk, cudaMemcpyHostToDevice, st));
            }
            if (depth <= 1 || ((i + 1) % (uint32_t)depth) == 0) {
                CUDACHECK(cudaStreamSynchronize(st));
            }
        }
        CUDACHECK(cudaEventRecord(ev1, st));
        CUDACHECK(cudaEventSynchronize(ev1));
        float ms = 0;
        CUDACHECK(cudaEventElapsedTime(&ms, ev0, ev1));
        r.ms = (double)ms;
        r.iters = n;
        r.bytes = (uint64_t)n * (uint64_t)chunk;
        r.mib_s = (r.ms > 0.0) ? ((double)r.bytes / (1024.0 * 1024.0)) / (r.ms / 1000.0)
                               : 0.0;
        if (r.ms >= min_ms || pass == 1) {
            break;
        }
        uint32_t grow = (uint32_t)((min_ms / r.ms) * (double)n * 1.15);
        if (grow <= n) {
            break;
        }
        n = grow;
        if (n % (uint32_t)depth) {
            n += (uint32_t)depth - (n % (uint32_t)depth);
        }
    }

    cudaEventDestroy(ev0);
    cudaEventDestroy(ev1);
    cudaStreamDestroy(st);
    if (d_dst) {
        cudaFree(d_dst);
    }
    cudaFree(d_src);
    if (h_pin) {
        cudaFreeHost(h_pin);
    }
    return r;
}

static void print_row(kind_t k, pat_t p, size_t ws, size_t chunk,
                      const run_result &r)
{
    std::printf("  %-4s  %-6s  %8zu  %8zu  %8u  %8.2f  %10.1f\n", kind_name(k),
                pat_name(p), ws, chunk, r.iters, r.ms, r.mib_s);
    std::fflush(stdout);
}

static void usage(const char *argv0)
{
    std::printf(
        "Usage: %s [options]\n"
        "  --preset prove     key cells that separate WS-window vs chunk-length\n"
        "  --sweep            full kind x pattern x ws x chunk matrix\n"
        "  --kind d2d|d2h|h2d\n"
        "  --pattern hammer|walk|random\n"
        "  --ws SIZE          GPU buffer (working set)\n"
        "  --chunk SIZE       bytes per memcpy\n"
        "  --depth N          async in-flight copies before sync (default 32)\n"
        "  --min-ms MS        measure at least this long (default 400)\n"
        "  --dev N\n"
        "\n"
        "SIZE accepts K/M (1024). Offsets are chunk-aligned (and so 64K if chunk is).\n"
        "MiB/s uses 1024^2, same as urma_perftest.\n",
        argv0);
}

int main(int argc, char **argv)
{
    int preset_prove = 1;
    int sweep = 0;
    int have_kind = 0;
    int have_pat = 0;
    int have_ws = 0;
    int have_chunk = 0;
    kind_t kind = KIND_D2H;
    pat_t pat = PAT_HAMMER;
    size_t ws = 1024 * 1024;
    size_t chunk = 128 * 1024;
    int depth = 32;
    double min_ms = 400.0;
    int dev = 0;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
        if (std::strcmp(argv[i], "--preset") == 0 && i + 1 < argc) {
            preset_prove = (std::strcmp(argv[++i], "prove") == 0);
            sweep = 0;
            continue;
        }
        if (std::strcmp(argv[i], "--sweep") == 0) {
            sweep = 1;
            preset_prove = 0;
            continue;
        }
        if (std::strcmp(argv[i], "--kind") == 0 && i + 1 < argc) {
            kind = parse_kind(argv[++i]);
            have_kind = 1;
            preset_prove = 0;
            continue;
        }
        if (std::strcmp(argv[i], "--pattern") == 0 && i + 1 < argc) {
            pat = parse_pat(argv[++i]);
            have_pat = 1;
            preset_prove = 0;
            continue;
        }
        if (std::strcmp(argv[i], "--ws") == 0 && i + 1 < argc) {
            ws = (size_t)parse_size(argv[++i]);
            have_ws = 1;
            preset_prove = 0;
            continue;
        }
        if (std::strcmp(argv[i], "--chunk") == 0 && i + 1 < argc) {
            chunk = (size_t)parse_size(argv[++i]);
            have_chunk = 1;
            preset_prove = 0;
            continue;
        }
        if (std::strcmp(argv[i], "--depth") == 0 && i + 1 < argc) {
            depth = std::atoi(argv[++i]);
            if (depth < 1) {
                depth = 1;
            }
            continue;
        }
        if (std::strcmp(argv[i], "--min-ms") == 0 && i + 1 < argc) {
            min_ms = std::atof(argv[++i]);
            continue;
        }
        if (std::strcmp(argv[i], "--dev") == 0 && i + 1 < argc) {
            dev = std::atoi(argv[++i]);
            continue;
        }
        std::fprintf(stderr, "unknown arg: %s\n", argv[i]);
        usage(argv[0]);
        return 1;
    }

    CUDACHECK(cudaSetDevice(dev));
    cudaDeviceProp prop;
    CUDACHECK(cudaGetDeviceProperties(&prop, dev));
    std::printf("# GPU %d %s  PCI %04x:%02x:%02x  BAR1 hint via CE memcpy\n",
                dev, prop.name, prop.pciDomainID, prop.pciBusID, prop.pciDeviceID);
    std::printf("# depth=%d min_ms=%.0f  (do NOT LD_PRELOAD gdr-geforce-hook)\n",
                depth, min_ms);
    std::printf("# kind  pat     ws_bytes  chunk     iters      ms       MiB/s\n");
    std::printf("# ----  ------  --------  --------  --------  --------  ----------\n");

    if (have_kind && have_pat && have_ws && have_chunk) {
        if (chunk > ws || (ws % chunk) != 0) {
            std::fprintf(stderr, "need ws %% chunk == 0 and chunk <= ws\n");
            return 1;
        }
        print_row(kind, pat, ws, chunk,
                  run_one(kind, pat, ws, chunk, depth, min_ms, 512));
        return 0;
    }

    struct cell {
        kind_t k;
        pat_t p;
        size_t ws;
        size_t ch;
    };

    std::vector<cell> cells;
    if (preset_prove) {
        /* The four questions:
         *  Q1 128K@256K hammer d2h — small WS, should be near Gen4 if CE can do it
         *  Q2 128K@1M   hammer d2h — same chunk, big WS (URMA wr_split analog)
         *  Q3 128K@1M   random d2h — random inside the same 1M
         *  Q4 512K@256K is impossible; 512K@1M hammer — per-chunk
         *  Q5 d2d copies of Q1/Q2 — HBM must stay huge
         */
        const size_t k256 = 256ull * 1024ull;
        const size_t k128 = 128ull * 1024ull;
        const size_t k512 = 512ull * 1024ull;
        const size_t m1 = 1024ull * 1024ull;
        const size_t m4 = 4ull * 1024ull * 1024ull;
        kind_t kinds[3] = { KIND_D2H, KIND_H2D, KIND_D2D };
        for (int ki = 0; ki < 3; ki++) {
            cells.push_back({ kinds[ki], PAT_HAMMER, k256, k128 });
            cells.push_back({ kinds[ki], PAT_HAMMER, m1, k128 });
            cells.push_back({ kinds[ki], PAT_WALK, m1, k128 });
            cells.push_back({ kinds[ki], PAT_RANDOM, m1, k128 });
            cells.push_back({ kinds[ki], PAT_HAMMER, m1, k512 });
            cells.push_back({ kinds[ki], PAT_RANDOM, m4, k128 });
            cells.push_back({ kinds[ki], PAT_WALK, m4, k512 });
        }
    } else if (sweep) {
        kind_t kinds[3] = { KIND_D2H, KIND_H2D, KIND_D2D };
        pat_t pats[3] = { PAT_HAMMER, PAT_WALK, PAT_RANDOM };
        size_t wss[] = { 256ull * 1024, 512ull * 1024, 1024ull * 1024,
                         4ull * 1024 * 1024, 16ull * 1024 * 1024 };
        size_t chs[] = { 64ull * 1024, 128ull * 1024, 256ull * 1024,
                         512ull * 1024 };
        for (kind_t k : kinds) {
            for (pat_t p : pats) {
                for (size_t w : wss) {
                    for (size_t c : chs) {
                        if (c <= w && (w % c) == 0) {
                            cells.push_back({ k, p, w, c });
                        }
                    }
                }
            }
        }
    } else {
        if (chunk > ws || (ws % chunk) != 0) {
            std::fprintf(stderr, "need ws %% chunk == 0 and chunk <= ws\n");
            return 1;
        }
        kind_t kinds[3] = { KIND_D2H, KIND_H2D, KIND_D2D };
        pat_t pats[3] = { PAT_HAMMER, PAT_WALK, PAT_RANDOM };
        for (int ki = 0; ki < 3; ki++) {
            if (have_kind && kinds[ki] != kind) {
                continue;
            }
            for (int pi = 0; pi < 3; pi++) {
                if (have_pat && pats[pi] != pat) {
                    continue;
                }
                cells.push_back({ kinds[ki], pats[pi],
                                  have_ws ? ws : (size_t)(1024 * 1024),
                                  have_chunk ? chunk : (size_t)(128 * 1024) });
            }
        }
    }

    for (const cell &c : cells) {
        print_row(c.k, c.p, c.ws, c.ch,
                  run_one(c.k, c.p, c.ws, c.ch, depth, min_ms, 512));
    }

    std::printf("#\n"
                "# Read: MiB/s uses 1024^2 (urma_perftest units).\n"
                "#   d2d ~100s GB/s, no 26→17 step  => not HBM.\n"
                "#   d2h/h2d 128K@256K high, 128K@1M ~17G  => WS window, URMA-independent.\n"
                "#   d2h/h2d all ~25G                    => URMA/UMMU, not GPU CE/BAR.\n"
                "#   random << walk on large ws          => TLB/window thrash.\n"
                "#   random ~= hammer on large ws        => window is the mapped range,\n"
                "#                                          not the access pattern.\n");
    return 0;
}
