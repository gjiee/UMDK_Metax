/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 * File Name     : dlock_log.cpp
 * Description   : dlock log module
 * History       : create file & add functions
 * 1.Date        : 2021-06-11
 * Author        : zhangjun
 * Modification  : Created file
 */
#include <syslog.h>
#include <cstdio>
#include <cstdarg>

#include "urma_api.h"
#include "urma_log.h"
#include "dlock_log.h"

namespace dlock {
static int g_dlock_log_level = LOG_WARNING;

void dlock_set_log_level(int log_level)
{
    if ((log_level > LOG_DEBUG) || (log_level < 0)) {
        return;
    }
    g_dlock_log_level = log_level;
    urma_log_set_level(static_cast<urma_vlog_level_t>(log_level));
}

void dlock_log(const char *func, int line, int priority, const char *format, ...) noexcept
{
    int ret;
    va_list args;
    char file_format[DLOCK_LOG_FORMAT_LEN + 1] = {0};

    if (priority > g_dlock_log_level) {
        return;
    }

    ret = snprintf(file_format, sizeof(file_format) - 1, "%s[%d] | %s\n", func, line, format);
    if (ret < 0) {
        return;
    }

    va_start(args, format);
    static_cast<void>(vprintf(file_format, args));
    va_end(args);
}
};
