/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: urpc handshaker information cmd
 * Create: 2025-07-11
 */

#include "unix_server.h"
#include "urpc_lib_log.h"
#include "task_manager.h"

#include "handshaker_info.h"

static void process_handshaker_all(
    urpc_ipc_ctl_head_t *req_ctl, char *request, urpc_ipc_ctl_head_t *rsp_ctl, char **reply)
{
    /* obtain an overview of all tasks */
    if (req_ctl->data_size != sizeof(urpc_handshaker_cmd_input_t)) {
        URPC_LIB_LOG_ERR("invalid request control header\n");
        rsp_ctl->error_code = -EINVAL;
        return;
    }

    uint32_t aaqi_size = (uint32_t)sizeof(system_task_statistics_t);
    system_task_statistics_t *task_summary = urpc_dbuf_malloc(URPC_DBUF_TYPE_DFX, aaqi_size);
    if (task_summary == NULL) {
        URPC_LIB_LOG_ERR("failed to malloc, errno: %d\n", errno);
        rsp_ctl->error_code = -URPC_ERR_ENOMEM;
        return;
    }
    task_manager_summary_info_get(task_summary);
    rsp_ctl->data_size = aaqi_size;
    *reply = (char *)task_summary;
}

static void process_handshaker_by_key(urpc_ipc_ctl_head_t *req_ctl, char *request,
    urpc_ipc_ctl_head_t *rsp_ctl, char **reply)
{
    if (req_ctl->data_size != sizeof(urpc_handshaker_cmd_input_t)) {
        URPC_LIB_LOG_ERR("invalid request control header\n");
        rsp_ctl->error_code = -EINVAL;
        return;
    }

    char *output = NULL;
    uint32_t output_size = 0;
    urpc_handshaker_cmd_input_t *input = (urpc_handshaker_cmd_input_t *)(uintptr_t)request;
    int ret = task_manager_task_info_get(input->channel_id, input->task_id, &output, &output_size);
    if (ret != URPC_SUCCESS) {
        rsp_ctl->error_code = ret;
        return;
    }

    rsp_ctl->data_size = output_size;
    *reply = output;
    return;
}

static urpc_ipc_cmd_t g_urpc_handshaker_task_cmd[URPC_HANDSHAKER_CMD_ID_MAX] = {
    {
        .module_id = (uint16_t)URPC_IPC_MODULE_HANDSHAKER,
        .cmd_id = (uint16_t)URPC_HANDSHAKER_CMD_ID_ALL_HANDSHAKER,
        .func = process_handshaker_all,
        .reply_malloced = true,
    }, {
        .module_id = (uint16_t)URPC_IPC_MODULE_HANDSHAKER,
        .cmd_id = (uint16_t)URPC_HANDSHAKER_CMD_ID_BY_TASK_ID,
        .func = process_handshaker_by_key,
        .reply_malloced = true,
    },
};

int handshaker_cmd_init(void)
{
    return unix_server_cmds_register(g_urpc_handshaker_task_cmd, URPC_HANDSHAKER_CMD_ID_MAX);
}

void handshaker_cmd_uninit(void)
{
    unix_server_cmds_unregister(g_urpc_handshaker_task_cmd, URPC_HANDSHAKER_CMD_ID_MAX);
}