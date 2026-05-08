/* SPDX-License-Identifier: MIT
 * UMS(UB Memory based Socket)
 *
 * Description: ums preload implementation
 *
 * Copyright IBM Corp. 2016, 2018
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * UMS implementation:
 *     Author(s):  YAO Yufeng ZHANG Chuwen
 */

#include <dlfcn.h>
#include <errno.h>
#include <netdb.h>
#include <search.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <netinet/in.h>

#include <gnu/lib-names.h>
#include <sys/socket.h>
#include <sys/types.h>

#define DLOPEN_FLAG RTLD_LAZY

#ifndef SMCPROTO_SMC
#define SMCPROTO_SMC 0  /* SMC protocol, IPv4 */
#define SMCPROTO_SMC6 1 /* SMC protocol, IPv6 */
#endif

#ifndef AF_SMC
#define AF_SMC 43
#endif

static void *g_dl_handle = NULL;
int (*g_orig_socket)(int domain, int type, int protocol);

static void initialize(void);

static int g_ums_debug_mode = 0;

#define GET_FUNC(x)                                                \
    if (g_dl_handle) {                                             \
        char *err; dlerror(); g_orig_##x = dlsym(g_dl_handle, #x); \
        if ((!g_orig_##x) && (err = dlerror())) {                  \
            fprintf(stderr, "dlsym failed: " #x ": %s\n", err);    \
            g_orig_##x = emergency_##x;                            \
        }                                                          \
    } else {                                                       \
        g_orig_##x = emergency_##x;                                \
    }

static void dbg_msg(FILE *f, const char *format, ...)
{
    va_list vl;

    if (g_ums_debug_mode != 0) {
        va_start(vl, format);
        (void)vfprintf(f, format, vl);
        va_end(vl);
    }
}

static int emergency_socket(int domain, int type, int protocol)
{
    errno = EINVAL;
    (void)fprintf(stderr, "emergency_socket: domain:%d, type:%d, protocol:%d\n", domain, type, protocol);
    return -1;
}

int socket(int domain, int type, int protocol)
{
    int rc = 0;

    if (!g_dl_handle) {
        initialize();
    }

    if ((domain == AF_INET || domain == AF_INET6) && ((((uint32_t)type) & 0xf) == SOCK_STREAM) &&
        (protocol == IPPROTO_IP || protocol == IPPROTO_TCP)) {
        dbg_msg(stderr, "libums-preload: map sock to AF_SMC\n");
        if (domain == AF_INET6) {
            protocol = SMCPROTO_SMC6;
        } else { /* AF_INET */
            protocol = SMCPROTO_SMC;
        }
        domain = AF_SMC;
    }

    if (g_orig_socket) {
        rc = (*g_orig_socket)(domain, type, protocol);
    }
    return rc;
}

static void set_debug_mode(const char *name)
{
    char *value;

    value = getenv(name);
    g_ums_debug_mode = 0;
    if (value != NULL) {
        g_ums_debug_mode = (value[0] != '0');
    }
}

static void initialize(void)
{
    set_debug_mode("UMS_DEBUG");

    g_dl_handle = dlopen(LIBC_SO, DLOPEN_FLAG);
    if (!g_dl_handle) {
        dbg_msg(stderr, "dlopen failed: %s\n", dlerror());
    }
    GET_FUNC(socket);
}