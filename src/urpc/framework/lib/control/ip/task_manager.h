/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: define task manager function
 */
#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include <unistd.h>

#include "queue.h"
#include "task_engine.h"
#include "urpc_hmap.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct urpc_task_table {
    struct urpc_hmap hmap;
    pthread_rwlock_t rw_lock;
    pthread_mutex_t workflow_lock;
    atomic_uint running_cnt;
} urpc_task_table_t;

typedef struct urpc_task_activation_manager {
    urpc_list_t list;
    pthread_mutex_t lock;
} urpc_task_activation_manager_t;

typedef struct urpc_task_timeout_manager {
    urpc_list_t list;
    uint32_t total_cnt;
    pthread_mutex_t lock;
    bool is_outer_lock;
} urpc_task_timeout_manager_t;

typedef struct task_summary_info {
    uint32_t total_tasks;
    uint32_t running_tasks;
} task_summary_info_t;

typedef struct system_task_statistics {
    task_summary_info_t client;
    task_summary_info_t server;
} system_task_statistics_t;

typedef struct task_query_info {
    int task_id;
    task_engine_task_state_t state;
    task_workflow_type_t workflow_type;
    uint32_t step;
    uint32_t channel_id;
} task_query_info_t;

void task_manager_timeout_manager_remove(urpc_async_task_ctx_t *entry);
void task_manager_timeout_manager_insert(urpc_async_task_ctx_t *entry);
urpc_async_task_ctx_t *task_manager_server_task_get(task_instance_key_t *key);
urpc_async_task_ctx_t *task_manager_client_task_get(int task_id);
void task_manager_server_task_insert(urpc_async_task_ctx_t *entry);
void task_manager_client_task_insert(urpc_async_task_ctx_t *entry);
void task_manager_client_task_remove(urpc_async_task_ctx_t *entry);
void task_manager_server_task_remove(urpc_async_task_ctx_t *entry);
void task_manager_running_task_activate(void);
int task_manager_client_task_create(urpc_channel_info_t *channel, task_init_params_t *params);
int task_manager_task_cancel(int task_id);
urpc_async_task_ctx_t *task_manager_ready_queue_pop(urpc_channel_info_t *channel);
void task_manager_activation_manager_insert(urpc_async_task_ctx_t *task_entry);
int task_manager_init(void);
void task_manager_uninit(void);
int task_manager_task_info_get(uint32_t channel_id, uint32_t task_id, char **output, uint32_t *output_size);
void task_manager_summary_info_get(system_task_statistics_t *info);
void task_manager_client_task_clear(urpc_async_task_ctx_t *task);
void task_manager_timeout_manager_lock(void);
void task_manager_timeout_manager_unlock(void);
void task_manager_workflow_lock(void);
void task_manager_workflow_unlock(void);
bool task_manager_task_num_validate(void);

#ifdef __cplusplus
}
#endif

#endif