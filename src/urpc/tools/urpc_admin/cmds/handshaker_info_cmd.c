/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: urpc handshaker cmd process
 * Create: 2025-7-11
 */

#include "handshaker_info.h"
#include "task_manager.h"
#include "urpc_framework_types.h"
#include "urpc_admin_cmd.h"
#include "urpc_admin_log.h"
#include "urpc_admin_param.h"

static int handshaker_request_create(urpc_ipc_ctl_head_t *req_ctl, char **request, urpc_admin_config_t *cfg)
{
    urpc_handshaker_cmd_input_t *task_cmd = (urpc_handshaker_cmd_input_t *)malloc(sizeof(urpc_handshaker_cmd_input_t));
    if (task_cmd == NULL) {
        LOG_PRINT("malloc request info failed\n");
        return -1;
    }

    task_cmd->channel_id = cfg->channel_id;
    task_cmd->task_id = (uint32_t)cfg->task_id;
    *request = (char *)task_cmd;
    req_ctl->module_id = (uint16_t)cfg->module_id;
    req_ctl->cmd_id = (uint16_t)cfg->cmd_id;
    req_ctl->error_code = 0;
    req_ctl->data_size = (uint32_t)sizeof(urpc_handshaker_cmd_input_t);

    return 0;
}

static int all_handshaker_response_process(
    urpc_ipc_ctl_head_t *rsp_ctl, char *reply, urpc_admin_config_t *cfg __attribute__((unused)))
{
    if (rsp_ctl->error_code != 0) {
        LOG_PRINT("recv error code %d\n", rsp_ctl->error_code);
        return -1;
    }

    if (rsp_ctl->data_size == 0 || rsp_ctl->data_size != sizeof(system_task_statistics_t)) {
        LOG_PRINT("recv size invaild, recv size: %u, except size: %zu\n",
            rsp_ctl->data_size, sizeof(system_task_statistics_t));
        return -1;
    }

    (void)printf("-------------------------------------------------------------------\n");
    (void)printf("Client total num:                  %u\n", ((system_task_statistics_t *)reply)->client.total_tasks);
    (void)printf("Client running num:                %u\n", ((system_task_statistics_t *)reply)->client.running_tasks);
    (void)printf("Server total num:                  %u\n", ((system_task_statistics_t *)reply)->server.total_tasks);
    (void)printf("Server running num:                %u\n", ((system_task_statistics_t *)reply)->server.running_tasks);
    (void)printf("-------------------------------------------------------------------\n");

    return 0;
}

static int single_handshaker_response_process(
    urpc_ipc_ctl_head_t *rsp_ctl, char *reply, urpc_admin_config_t *cfg __attribute__((unused)))
{
    if (rsp_ctl->error_code != 0) {
        LOG_PRINT("recv error code %d\n", rsp_ctl->error_code);
        return -1;
    }

    if (rsp_ctl->data_size == 0 || rsp_ctl->data_size != sizeof(task_query_info_t)) {
        LOG_PRINT("recv size invaild, recv size: %u, except size: %zu\n",
            rsp_ctl->data_size, sizeof(task_query_info_t));
        return -1;
    }

    task_query_info_t *task = (task_query_info_t *)reply;
    (void)printf("-------------------------------------------------------------------\n");
    (void)printf("task id:                           %d\n", task->task_id);
    (void)printf("task chid:                         %u\n", task->channel_id);
    (void)printf("task state:                        %u\n", task->state);
    (void)printf("task type:                         %u\n", task->workflow_type);
    (void)printf("task step:                         %u\n", task->step);
    (void)printf("-------------------------------------------------------------------\n");

    return 0;
}

static urpc_admin_cmd_t g_handshaker_cmd[URPC_HANDSHAKER_CMD_ID_MAX] = {
    {
        .module_id = (uint16_t)URPC_IPC_MODULE_HANDSHAKER,
        .cmd_id = (uint16_t)URPC_HANDSHAKER_CMD_ID_ALL_HANDSHAKER,
        .create_request = handshaker_request_create,
        .process_response = all_handshaker_response_process,
    }, {
        .module_id = (uint16_t)URPC_IPC_MODULE_HANDSHAKER,
        .cmd_id = (uint16_t)URPC_HANDSHAKER_CMD_ID_BY_TASK_ID,
        .create_request = handshaker_request_create,
        .process_response = single_handshaker_response_process,
    },
};

static void __attribute__((constructor)) urpc_handshaker_cmd_init(void)
{
    urpc_admin_cmds_register(g_handshaker_cmd, URPC_HANDSHAKER_CMD_ID_MAX);
}