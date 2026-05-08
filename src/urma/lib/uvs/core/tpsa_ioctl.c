/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2025. All rights reserved.
 * Description: tpsa ioctl implementation file
 * Author: Ji Lei
 * Create: 2023-7-3
 * Note:
 * History: 2023-7-3 tpsa ioctl implementation
 */
#include <stdint.h>
#include <errno.h>
#include <dirent.h>

#include "urma_cmd.h"
#include "tpsa_log.h"
#include "uvs_cmd_tlv.h"
#include "tpsa_ioctl.h"

#define UVS_TPF_CDEV_DIR "/dev/ubcore"

int uvs_ioctl_in_global(tpsa_ioctl_ctx_t *ioctl_ctx, uvs_global_cmd_t cmd, void *arg, uint32_t arg_len)
{
    tpsa_cmd_hdr_t hdr = {0};
    int ret = 0;

    hdr.command = (uint32_t)cmd;
    hdr.args_len = arg_len;
    hdr.args_addr = (uint64_t)arg;

    ret = ioctl(ioctl_ctx->ubcore_fd, TPSA_CMD, &hdr);
    if (ret != 0) {
        TPSA_LOG_ERR("ioctl failed, ret:%d, errno:%d, cmd:%u.\n", ret, errno, hdr.command);
        return errno > 0 ? -errno : ret;
    }

    return 0;
}