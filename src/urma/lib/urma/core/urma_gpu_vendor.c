/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2026. All rights reserved.
 *
 * Detect NVIDIA vs Muxi (MetaX / 沐曦) so urma_register_seg can tell the
 * kernel which GPU P2P pin path to use.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "urma_api.h"

#define PCI_VENDOR_NVIDIA 0x10deu
#define PCI_VENDOR_METAX  0x9999u /* MetaX / 沐曦, from metax.ko PCI aliases */

#define GV_LOG(fmt, ...) \
    fprintf(stderr, "[gpu-vendor] %s: " fmt "\n", __func__, ##__VA_ARGS__)

static urma_gpu_vendor_t g_preferred = URMA_GPU_VENDOR_NONE;
static int g_sys_cached;
static urma_gpu_vendor_t g_sys_vendor = URMA_GPU_VENDOR_NONE;

void urma_gpu_vendor_set_preferred(urma_gpu_vendor_t vendor)
{
    g_preferred = vendor;
    GV_LOG("preferred=%s", urma_gpu_vendor_name(vendor));
}

const char *urma_gpu_vendor_name(urma_gpu_vendor_t vendor)
{
    switch (vendor) {
        case URMA_GPU_VENDOR_NONE:
            return "none";
        case URMA_GPU_VENDOR_NVIDIA:
            return "nvidia";
        case URMA_GPU_VENDOR_MUXI:
            return "muxi";
        default:
            return "?";
    }
}

int urma_gpu_vendor_from_string(const char *s, urma_gpu_vendor_t *out)
{
    if (s == NULL || out == NULL) {
        return -1;
    }
    if (strcasecmp(s, "none") == 0 || strcasecmp(s, "auto") == 0) {
        *out = URMA_GPU_VENDOR_NONE;
        return 0;
    }
    if (strcasecmp(s, "nvidia") == 0 || strcasecmp(s, "nv") == 0) {
        *out = URMA_GPU_VENDOR_NVIDIA;
        return 0;
    }
    if (strcasecmp(s, "muxi") == 0 || strcasecmp(s, "metax") == 0 ||
        strcasecmp(s, "maca") == 0 || strcasecmp(s, "mx") == 0) {
        *out = URMA_GPU_VENDOR_MUXI;
        return 0;
    }
    return -1;
}

static urma_gpu_vendor_t vendor_from_env_value(const char *e)
{
    urma_gpu_vendor_t v;

    if (e == NULL || e[0] == '\0') {
        return URMA_GPU_VENDOR_NONE;
    }
    if (urma_gpu_vendor_from_string(e, &v) != 0) {
        GV_LOG("unknown env vendor \"%s\", ignored", e);
        return URMA_GPU_VENDOR_NONE;
    }
    return v;
}

static urma_gpu_vendor_t vendor_from_env(void)
{
    urma_gpu_vendor_t v;

    v = vendor_from_env_value(getenv("URMA_GPU_VENDOR"));
    if (v != URMA_GPU_VENDOR_NONE) {
        return v;
    }
    return vendor_from_env_value(getenv("PERFTEST_GPU_VENDOR"));
}

static int read_sysfs_hex(const char *path, unsigned int *out)
{
    FILE *fp;
    int n;

    if (path == NULL || out == NULL) {
        return -1;
    }
    fp = fopen(path, "r");
    if (fp == NULL) {
        return -1;
    }
    n = fscanf(fp, "0x%x", out);
    if (n != 1) {
        rewind(fp);
        n = fscanf(fp, "%x", out);
    }
    fclose(fp);
    return n == 1 ? 0 : -1;
}

static int pci_has_vendor(unsigned int want)
{
    DIR *dir;
    struct dirent *ent;
    char path[296];
    unsigned int id = 0;
    int found = 0;

    dir = opendir("/sys/bus/pci/devices");
    if (dir == NULL) {
        return 0;
    }
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/vendor",
                 ent->d_name);
        if (read_sysfs_hex(path, &id) == 0 && id == want) {
            found = 1;
            break;
        }
    }
    closedir(dir);
    return found;
}

static int path_exists(const char *p)
{
    return p != NULL && access(p, F_OK) == 0;
}

static int maca_stack_present(void)
{
    const char *home = getenv("MACA_PATH");
    char buf[512];

    if (path_exists("/opt/maca/lib/libsymbol_cu.so") ||
        path_exists("/opt/maca/lib/libmcruntime.so") ||
        path_exists("/opt/maca/lib64/libsymbol_cu.so") ||
        path_exists("/opt/maca/tools/cu-bridge/lib/libcuda.so") ||
        path_exists("/usr/bin/mx-smi") ||
        path_exists("/sys/module/metax")) {
        return 1;
    }
    if (home != NULL && home[0] != '\0') {
        snprintf(buf, sizeof(buf), "%s/lib/libsymbol_cu.so", home);
        if (path_exists(buf)) {
            return 1;
        }
        snprintf(buf, sizeof(buf), "%s/lib/libmcruntime.so", home);
        if (path_exists(buf)) {
            return 1;
        }
    }
    return 0;
}

static urma_gpu_vendor_t vendor_from_sys(void)
{
    int has_nv;
    int has_mx;

    has_nv = pci_has_vendor(PCI_VENDOR_NVIDIA) ||
             path_exists("/sys/module/nvidia") ||
             path_exists("/dev/nvidiactl");
    has_mx = pci_has_vendor(PCI_VENDOR_METAX) ||
             path_exists("/sys/module/metax") ||
             maca_stack_present();

    if (has_mx && !has_nv) {
        return URMA_GPU_VENDOR_MUXI;
    }
    if (has_nv && !has_mx) {
        return URMA_GPU_VENDOR_NVIDIA;
    }
    if (has_mx && has_nv) {
        /* Mixed node: prefer Muxi only if PCI 0x9999 is present. */
        if (pci_has_vendor(PCI_VENDOR_METAX)) {
            return URMA_GPU_VENDOR_MUXI;
        }
        return URMA_GPU_VENDOR_NVIDIA;
    }
    return URMA_GPU_VENDOR_NONE;
}

urma_gpu_vendor_t urma_detect_gpu_vendor(void)
{
    urma_gpu_vendor_t v;

    if (g_preferred == URMA_GPU_VENDOR_NVIDIA ||
        g_preferred == URMA_GPU_VENDOR_MUXI) {
        return g_preferred;
    }
    v = vendor_from_env();
    if (v != URMA_GPU_VENDOR_NONE) {
        return v;
    }
    if (!g_sys_cached) {
        g_sys_vendor = vendor_from_sys();
        g_sys_cached = 1;
        GV_LOG("sys detect -> %s", urma_gpu_vendor_name(g_sys_vendor));
    }
    return g_sys_vendor;
}
