/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 * File Name     : client_wrapper.cpp
 * Description   : wrapper of client
 * History       : create file & add functions
 * 1.Date        : 2021-06-16
 * Author        : zhangjun
 * Modification  : Created file
 */

#include "dlock_client_api.h"

#include <sys/time.h>
#include <unistd.h>

#include "dlock_common.h"
#include "dlock_client.h"
#include "dlock_log.h"
#include "dlock_types.h"
#include "utils.h"

namespace dlock {
extern "C" {
int dclient_lib_init(const struct client_cfg *p_client_cfg)
{
    DLOCK_LOG_DEBUG("init client lib");
    if (p_client_cfg == nullptr) {
        DLOCK_LOG_ERR("nullptr client cfg");
        return static_cast<int>(DLOCK_EINVAL);
    }

    if (check_port_range_invalid(p_client_cfg->primary_port)) {
        DLOCK_LOG_ERR("invalid server port: %d", p_client_cfg->primary_port);
        return static_cast<int>(DLOCK_EINVAL);
    }

    if (!check_ssl_cfg_valid(p_client_cfg->ssl)) {
        DLOCK_LOG_ERR("invalid ssl cfg");
        return static_cast<int>(DLOCK_EINVAL);
    }

    dlock_client &clientMgr = dlock_client::instance();
    return clientMgr.init(p_client_cfg);
}

void dclient_lib_deinit()
{
    dlock_client &clientMgr = dlock_client::instance();

    DLOCK_LOG_DEBUG("deinit client lib");
    clientMgr.deinit();
}

int client_init(int *p_client_id, const char *ip_str)
{
    dlock_client &clientMgr = dlock_client::instance();
    if ((p_client_id == nullptr) || (ip_str == nullptr)) {
        DLOCK_LOG_ERR("client_id or ip_str is nullptr");
        return static_cast<int>(DLOCK_EINVAL);
    }

    return clientMgr.init_client(p_client_id, ip_str);
}

int client_reinit(int client_id, const char *ip_str)
{
    dlock_client &clientMgr = dlock_client::instance();
    if (ip_str == nullptr) {
        DLOCK_LOG_ERR("client_id or ip_str is nullptr");
        return static_cast<int>(DLOCK_EINVAL);
    }

    DLOCK_LOG_DEBUG("reinit client");
    return clientMgr.reinit_client(client_id, ip_str);
}

int client_deinit(int client_id)
{
    dlock_client &clientMgr = dlock_client::instance();

    DLOCK_LOG_DEBUG("deinit client");
    return clientMgr.deinit_client(client_id);
}

int client_heartbeat(int client_id, unsigned int timeout)
{
    dlock_client &clientMgr = dlock_client::instance();

    DLOCK_LOG_DEBUG("client heartbeat!");
    return clientMgr.heartbeat(client_id, timeout);
}

int get_lock(int client_id, const struct lock_desc *desc, int *lock_id)
{
    if ((lock_id == nullptr) || (desc == nullptr)) {
        DLOCK_LOG_ERR("lock_id or lock_desc is nullptr");
        return static_cast<int>(DLOCK_EINVAL);
    }

    if ((desc->len == 0u) || (desc->len > MAX_LOCK_DESC_LEN) || (desc->p_desc == nullptr)) {
        DLOCK_LOG_ERR("invalid lock_desc.len %d or lock_desc.p_desc is nullptr", desc->len);
        return static_cast<int>(DLOCK_EINVAL);
    }

    if (desc->lock_type >= static_cast<uint32_t>(DLOCK_MAX)) {
        DLOCK_LOG_ERR("unsupported lock type %d", desc->lock_type);
        return static_cast<int>(DLOCK_EINVAL);
    }

    dlock_client &clientMgr = dlock_client::instance();

    *lock_id = get_hash(desc->len, reinterpret_cast<unsigned char *>(desc->p_desc));
    DLOCK_LOG_DEBUG("try to get lock %d by client", *lock_id);
    return clientMgr.get_lock(client_id, desc, lock_id);
}

int release_lock(int client_id, int lock_id)
{
    dlock_client &clientMgr = dlock_client::instance();

    DLOCK_LOG_DEBUG("release lock %d by client", lock_id);
    return clientMgr.release_lock(client_id, lock_id);
}

int get_client_debug_stats(int client_id, struct debug_stats *stats)
{
    if (stats == nullptr) {
        DLOCK_LOG_ERR("debug_stats is nullptr");
        return static_cast<int>(DLOCK_EINVAL);
    }

    dlock_client &clientMgr = dlock_client::instance();
    return clientMgr.get_client_debug_stats(client_id, stats);
}

int clear_client_debug_stats(int client_id)
{
    dlock_client &clientMgr = dlock_client::instance();
    return clientMgr.clear_client_debug_stats(client_id);
}

static inline bool check_lock_request_valid(const struct lock_request &req)
{
    if ((req.lock_op != static_cast<int>(LOCK_EXCLUSIVE)) && (req.lock_op != static_cast<int>(LOCK_SHARED))) {
        DLOCK_LOG_DEBUG("invalid lock op %d for lock request", req.lock_op);
        return false;
    }

    return true;
}

int trylock(int client_id, const struct lock_request *req, void *result)
{
    if ((req == nullptr) || (result == nullptr)) {
        DLOCK_LOG_DEBUG("lock_request or result is nullptr");
        return static_cast<int>(DLOCK_EINVAL);
    }

    if (!check_lock_request_valid(*req)) {
        DLOCK_LOG_DEBUG("invalid trylock request");
        return static_cast<int>(DLOCK_EINVAL);
    }

    dlock_client &clientMgr = dlock_client::instance();

    DLOCK_LOG_DEBUG("trylock %d by client", req->lock_id);
    return clientMgr.trylock(client_id, req, result);
}

int lock(int client_id, const struct lock_request *req, void *result)
{
    struct timeval tv_start;
    struct timeval tv_end;
    int ret;

    if ((req == nullptr) || (result == nullptr)) {
        DLOCK_LOG_DEBUG("lock_request or result is nullptr");
        return static_cast<int>(DLOCK_EINVAL);
    }

    if (!check_lock_request_valid(*req)) {
        DLOCK_LOG_DEBUG("invalid lock request");
        return static_cast<int>(DLOCK_EINVAL);
    }

    DLOCK_LOG_DEBUG("lock %d by client", req->lock_id);
    static_cast<void>(gettimeofday(&tv_start, nullptr));
    dlock_client &clientMgr = dlock_client::instance();
    do {
        ret = clientMgr.trylock(client_id, req, result);
        if (ret != static_cast<int>(DLOCK_SUCCESS)) {
            static_cast<void>(usleep(SLEEP_INTERVAL));
        }
        static_cast<void>(gettimeofday(&tv_end, nullptr));
    } while ((ret != static_cast<int>(DLOCK_SUCCESS)) && (ret != static_cast<int>(DLOCK_ALREADY_LOCKED)) &&
             ((tv_end.tv_usec - tv_start.tv_usec) + (tv_end.tv_sec - tv_start.tv_sec) * ONE_MILLION < LOCK_TIMEOUT));
    return ret;
}

int unlock(int client_id, int lock_id, void *result)
{
    if (result == nullptr) {
        DLOCK_LOG_DEBUG("unlock result is nullptr");
        return static_cast<int>(DLOCK_EINVAL);
    }

    dlock_client &clientMgr = dlock_client::instance();

    DLOCK_LOG_DEBUG("unlock %d by client", lock_id);
    return clientMgr.unlock(client_id, lock_id, result);
}

int lock_extend(int client_id, const struct lock_request *req, void *result)
{
    if ((req == nullptr) || (result == nullptr)) {
        DLOCK_LOG_DEBUG("lock_request or result is nullptr");
        return static_cast<int>(DLOCK_EINVAL);
    }
    if ((req->lock_op != static_cast<int>(EXTEND_LOCK_EXCLUSIVE)) &&
        (req->lock_op != static_cast<int>(EXTEND_LOCK_SHARED))) {
        DLOCK_LOG_DEBUG("invalid lock op %d for extend lock", req->lock_op);
        return static_cast<int>(DLOCK_EINVAL);
    }
    dlock_client &clientMgr = dlock_client::instance();

    DLOCK_LOG_DEBUG("extend lock %d by client", req->lock_id);
    return clientMgr.lock_extend(client_id, req, result);
}

int batch_get_lock(int client_id, unsigned int lock_num, const struct lock_desc *descs, int *lock_ids)
{
    if ((lock_ids == nullptr) || (descs == nullptr)) {
        DLOCK_LOG_ERR("lock_ids or lock_desc is nullptr");
        return static_cast<int>(DLOCK_EINVAL);
    }

    if ((lock_num == 0u) || (lock_num > MAX_LOCK_BATCH_SIZE)) {
        DLOCK_LOG_ERR("invalid lock_num %d", lock_num);
        return static_cast<int>(DLOCK_EINVAL);
    }
    for (int i = 0; i < static_cast<int>(lock_num); i++) {
        if ((descs[i].len == 0u) || (descs[i].len > MAX_LOCK_DESC_LEN) || (descs[i].p_desc == nullptr)) {
            DLOCK_LOG_ERR("lock_desc[%d]: invalid lock_desc.len %d or lock_desc.p_desc is nullptr", i, descs[i].len);
            return static_cast<int>(DLOCK_EINVAL);
        }
        if (descs[i].lock_type >= static_cast<uint8_t>(DLOCK_MAX)) {
            DLOCK_LOG_ERR("lock_desc[%d]: unsupported lock type %d", i, descs[i].lock_type);
            return static_cast<int>(DLOCK_EINVAL);
        }
        lock_ids[i] = static_cast<int>(get_hash(descs[i].len, reinterpret_cast<unsigned char *>(descs[i].p_desc)));
        DLOCK_LOG_DEBUG("lock_desc[%d]: try to get lock %d by client", i, lock_ids[i]);
    }

    dlock_client &clientMgr = dlock_client::instance();
    return clientMgr.batch_get_lock(client_id, lock_num, descs, lock_ids);
}

int batch_release_lock(int client_id, unsigned int lock_num, int *lock_ids)
{
    if (lock_ids == nullptr) {
        DLOCK_LOG_ERR("lock_ids is nullptr");
        return static_cast<int>(DLOCK_EINVAL);
    }

    if ((lock_num == 0u) || (lock_num > MAX_LOCK_BATCH_SIZE)) {
        DLOCK_LOG_ERR("invalid lock_num %d", lock_num);
        return static_cast<int>(DLOCK_EINVAL);
    }

    DLOCK_LOG_DEBUG("release %d locks by client", lock_num);
    dlock_client &clientMgr = dlock_client::instance();
    return clientMgr.batch_release_lock(client_id, lock_num, lock_ids);
}

int batch_check_param_validity(unsigned int lock_num, const struct lock_request *reqs, void *results)
{
    if ((reqs == nullptr) || (results == nullptr)) {
        DLOCK_LOG_DEBUG("lock_request or results is nullptr");
        return static_cast<int>(DLOCK_EINVAL);
    }

    if ((lock_num == 0u) || (lock_num > MAX_LOCK_BATCH_SIZE)) {
        DLOCK_LOG_DEBUG("invalid lock_num %d", lock_num);
        return static_cast<int>(DLOCK_EINVAL);
    }

    return static_cast<int>(DLOCK_SUCCESS);
}

int batch_trylock(int client_id, unsigned int lock_num, const struct lock_request *reqs, void *results)
{
    int ret = batch_check_param_validity(lock_num, reqs, results);
    if (ret != static_cast<int>(DLOCK_SUCCESS)) {
        DLOCK_LOG_DEBUG("invalid batch_trylock parameters");
        return ret;
    }

    for (int i = 0; i < static_cast<int>(lock_num); i++) {
        if (!check_lock_request_valid(reqs[i])) {
            DLOCK_LOG_DEBUG("invalid batch trylock request, reqs[%d]", i);
            return static_cast<int>(DLOCK_EINVAL);
        }
        DLOCK_LOG_DEBUG("reqs[%d]: trylock %d by client", i, reqs[i].lock_id);
    }

    dlock_client &clientMgr = dlock_client::instance();

    return clientMgr.batch_trylock(client_id, lock_num, reqs, results);
}

int batch_unlock(int client_id, unsigned int lock_num, const int *lock_ids, void *results)
{
    if ((lock_ids == nullptr) || (results == nullptr)) {
        DLOCK_LOG_DEBUG("lock_ids or results is nullptr");
        return static_cast<int>(DLOCK_EINVAL);
    }

    if ((lock_num == 0u) || (lock_num > MAX_LOCK_BATCH_SIZE)) {
        DLOCK_LOG_DEBUG("invalid lock_num %d", lock_num);
        return static_cast<int>(DLOCK_EINVAL);
    }

    dlock_client &clientMgr = dlock_client::instance();

    DLOCK_LOG_DEBUG("unlock %d locks by client", lock_num);
    return clientMgr.batch_unlock(client_id, lock_num, lock_ids, results);
}

int batch_lock_extend(int client_id, unsigned int lock_num, const struct lock_request *reqs, void *results)
{
    int ret;

    ret = batch_check_param_validity(lock_num, reqs, results);
    if (ret != static_cast<int>(DLOCK_SUCCESS)) {
        DLOCK_LOG_DEBUG("invalid batch_lock_extend parameters");
        return ret;
    }

    for (int i = 0; i < static_cast<int>(lock_num); i++) {
        if ((reqs[i].lock_op != static_cast<int>(EXTEND_LOCK_EXCLUSIVE)) &&
            (reqs[i].lock_op != static_cast<int>(EXTEND_LOCK_SHARED))) {
            DLOCK_LOG_DEBUG("reqs[%d]: invalid lock op %d for extend lock", i, reqs[i].lock_op);
            return static_cast<int>(DLOCK_EINVAL);
        }
        DLOCK_LOG_DEBUG("reqs[%d]: extend lock %d by client", i, reqs[i].lock_id);
    }

    dlock_client &clientMgr = dlock_client::instance();

    return clientMgr.batch_lock_extend(client_id, lock_num, reqs, results);
}

int update_all_locks(int client_id)
{
    dlock_client &clientMgr = dlock_client::instance();

    return clientMgr.update_all_locks(client_id);
}

int client_reinit_done(int client_id)
{
    dlock_client &clientMgr = dlock_client::instance();

    return clientMgr.reinit_done(client_id);
}

static inline bool check_lock_request_async_valid(const struct lock_request &req)
{
    if ((req.lock_op < 0) || (req.lock_op >= static_cast<int>(LOCK_OPS_MAX))) {
        DLOCK_LOG_DEBUG("invalid lock op %d for async lock request", req.lock_op);
        return false;
    }

    if ((req.lock_op == static_cast<int>(LOCK_EXCLUSIVE)) || (req.lock_op == static_cast<int>(LOCK_SHARED))) {
        if (req.expire_time == 0u) {
            DLOCK_LOG_DEBUG("0 expire time not supported in async lock op");
            return false;
        }
    }

    return true;
}

int lock_request_async(int client_id, const struct lock_request *req)
{
    if (req == nullptr) {
        DLOCK_LOG_ERR("lock req is nullptr");
        return static_cast<int>(DLOCK_EINVAL);
    }

    if (!check_lock_request_async_valid(*req)) {
        DLOCK_LOG_DEBUG("invalid lock request");
        return static_cast<int>(DLOCK_EINVAL);
    }

    dlock_client &clientMgr = dlock_client::instance();

    DLOCK_LOG_DEBUG("async lock req %d by client", req->lock_id);
    return clientMgr.async_lock_request(client_id, req);
}

int lock_result_check(int client_id, void *result)
{
    if (result == nullptr) {
        DLOCK_LOG_ERR("lock result is nullptr");
        return static_cast<int>(DLOCK_EINVAL);
    }
    dlock_client &clientMgr = dlock_client::instance();

    DLOCK_LOG_DEBUG("async lock result check for client");
    return clientMgr.async_result_check(client_id, result);
}

int umo_atomic64_create(int client_id, const struct umo_atomic64_desc *desc, uint64_t init_val, int *obj_id)
{
    if ((obj_id == nullptr) || (desc == nullptr)) {
        DLOCK_LOG_ERR("obj_id or desc is nullptr");
        return static_cast<int>(DLOCK_EINVAL);
    }

    if ((desc->len == 0) || (desc->len > MAX_UMO_ATOMIC64_DESC_LEN) || (desc->p_desc == nullptr)) {
        DLOCK_LOG_ERR("invalid desc len %d or p_desc is nullptr", desc->len);
        return static_cast<int>(DLOCK_EINVAL);
    }

    if (desc->lease_time == 0) {
        DLOCK_LOG_ERR("desc lease time is 0");
        return static_cast<int>(DLOCK_EINVAL);
    }

    dlock_client &clientMgr = dlock_client::instance();

    return clientMgr.atomic64_create(client_id, desc, init_val, obj_id);
}

int umo_atomic64_destroy(int client_id, int obj_id)
{
    dlock_client &clientMgr = dlock_client::instance();

    DLOCK_LOG_DEBUG("destroy object %d by client", obj_id);
    return clientMgr.atomic64_destroy(client_id, obj_id);
}

int umo_atomic64_get(int client_id, const struct umo_atomic64_desc *desc, int *obj_id)
{
    if ((obj_id == nullptr) || (desc == nullptr)) {
        DLOCK_LOG_ERR("obj_id or desc is nullptr");
        return static_cast<int>(DLOCK_EINVAL);
    }

    if ((desc->len == 0) || (desc->len > MAX_UMO_ATOMIC64_DESC_LEN) || (desc->p_desc == nullptr)) {
        DLOCK_LOG_ERR("invalid desc len %d or p_desc is nullptr", desc->len);
        return static_cast<int>(DLOCK_EINVAL);
    }

    if (desc->lease_time == 0) {
        DLOCK_LOG_ERR("desc lease time is 0");
        return static_cast<int>(DLOCK_EINVAL);
    }

    dlock_client &clientMgr = dlock_client::instance();

    return clientMgr.atomic64_get(client_id, desc, obj_id);
}

int umo_atomic64_release(int client_id, int obj_id)
{
    dlock_client &clientMgr = dlock_client::instance();

    DLOCK_LOG_DEBUG("release object %d", obj_id);
    return clientMgr.atomic64_release(client_id, obj_id);
}

int umo_atomic64_faa(int client_id, int obj_id, uint64_t add_val, uint64_t *res_val)
{
    if (res_val == nullptr) {
        DLOCK_LOG_DEBUG("[FAA] nullptr");
        return static_cast<int>(DLOCK_EINVAL);
    }

    dlock_client &clientMgr = dlock_client::instance();

    DLOCK_LOG_DEBUG("[FAA] object %d", obj_id);
    return clientMgr.atomic64_faa(client_id, obj_id, add_val, res_val);
}

int umo_atomic64_cas(int client_id, int obj_id, uint64_t cmp_val, uint64_t swap_val)
{
    dlock_client &clientMgr = dlock_client::instance();

    DLOCK_LOG_DEBUG("[CAS] object %d", obj_id);
    return clientMgr.atomic64_cas(client_id, obj_id, cmp_val, swap_val);
}

int umo_atomic64_get_snapshot(int client_id, int obj_id, uint64_t *res_val)
{
    if (res_val == nullptr) {
        DLOCK_LOG_DEBUG("[get_snapshot] nullptr");
        return static_cast<int>(DLOCK_EINVAL);
    }

    dlock_client &clientMgr = dlock_client::instance();

    DLOCK_LOG_DEBUG("[get_snapshot] object %d", obj_id);
    return clientMgr.atomic64_get_snapshot(client_id, obj_id, res_val);
}
}
};  // namespace dlock
