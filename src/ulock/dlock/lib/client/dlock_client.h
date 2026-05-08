/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 * File Name     : dlock_client.h
 * Description   : dlock client
 * History       : create file & add functions
 * 1.Date        : 2021-06-16
 * Author        : zhangjun
 * Modification  : Created file
 */

#ifndef __DLOCK_CLIENT_H__
#define __DLOCK_CLIENT_H__

#include <arpa/inet.h>
#include <ctime>
#include <shared_mutex>
#include <chrono>

#include "dlock_types.h"
#include "dlock_common.h"
#include "client_entry_c.h"
#include "ub_util.h"
#include "dlock_connection.h"
namespace dlock {
using client_map_t = std::unordered_map<int32_t, client_entry_c*>;

class dlock_client {
public:
    static dlock_client &instance();
    int init(const struct client_cfg *p_client_cfg);
    void deinit();
    int init_client(int *p_client_id, const char *ip_str);
    int reinit_client(int client_id, const char *ip_str);
    int deinit_client(int client_id);
    int heartbeat(int client_id, unsigned int timeout);
    int get_lock(int client_id, const struct lock_desc *p_desc, int *p_lock_id);
    int release_lock(int client_id, int lock_id);
    int get_client_debug_stats(int client_id, struct debug_stats *stats);
    int clear_client_debug_stats(int client_id);
    int trylock(int client_id, const struct lock_request *req, void *result);
    int unlock(int client_id, int lock_id, void *result);
    int lock_extend(int client_id, const struct lock_request *req, void *result);
    int batch_get_lock(int client_id, unsigned int lock_num, const struct lock_desc *p_descs, int *p_lock_ids);
    int batch_release_lock(int client_id, unsigned int lock_num, int *p_lock_ids);
    int batch_trylock(int client_id, unsigned int lock_num, const struct lock_request *p_reqs, void *p_results);
    int batch_unlock(int client_id, unsigned int lock_num, const int *p_lock_ids, void *p_results);
    int batch_lock_extend(int client_id, unsigned int lock_num, const struct lock_request *p_reqs, void *p_results);
    int update_all_locks(int client_id);
    int reinit_done(int client_id);
    int async_lock_request(int client_id, const struct lock_request *req);
    int async_result_check(int client_id, void *result);
    int get_lock_entry(client_entry_c &client_entry, lock_entry_c **p_lock_entry);
    int trylock_result_check(client_entry_c &client_entry, void *result);
    int unlock_or_extend_result_check(client_entry_c &client_entry, void *result);
    int atomic64_create(int client_id, const struct umo_atomic64_desc *p_desc, uint64_t init_val, int *p_obj_id);
    int atomic64_destroy(int client_id, int obj_id);
    int atomic64_get(int client_id, const struct umo_atomic64_desc *p_desc, int *p_obj_id);
    int atomic64_release(int client_id, int obj_id);
    int atomic64_faa(int client_id, int obj_id, uint64_t add_val, uint64_t *res_val);
    int atomic64_cas(int client_id, int obj_id, uint64_t cmp_val, uint64_t swap_val);
    int atomic64_get_snapshot(int client_id, int obj_id, uint64_t *res_val);

    inline client_entry_c *dlock_get_client_entry(std::shared_mutex &client_map_rwlock,
        client_map_t &client_map, int client_id)
    {
        std::shared_lock<std::shared_mutex> shared_locker(client_map_rwlock);
        client_map_t::iterator iter = client_map.find(client_id);
        if (iter == client_map.end()) {
            return nullptr;
        }

        return iter->second;
    }

protected:
    dlock_client() noexcept;
    ~dlock_client();

private:
    int connect_to_server(const char *ip_str) const;
    int add_client(uint8_t *buff, dlock_connection *p_conn, jetty_mgr &p_jetty_mgr, int *p_client_id,
        bool reinit_flag);
    int delete_client(int client_id);
    int init_client_do(int *p_client_id, const char *ip_str, bool reinit_flag);
    uint8_t *construct_batch_get_lock_req(int client_id, unsigned int lock_num,
        uint16_t message_id, const struct lock_desc *p_descs, int *p_lock_ids, size_t &msg_len) const;
    int batch_add_local_lock_entry(client_entry_c &p_client_entry, uint8_t *buff,
        unsigned int lock_num, const struct lock_desc *p_descs, int *p_lock_ids);
    uint8_t *construct_batch_release_lock_req(client_entry_c &p_client_entry,
        unsigned int lock_num, uint16_t message_id, int *p_lock_ids, size_t &msg_len);
    void batch_delete_local_lock_entry(client_entry_c &p_client_entry,
        unsigned int lock_num, int *p_lock_ids);
    uint32_t batch_trylock_construct_cmd_msg(client_entry_c &p_client_entry, unsigned int lock_num,
        const struct lock_request *p_reqs, struct lock_op_res *op_res, uint8_t *buf);
    void batch_trylock_update_state_with_cmd_msg(client_entry_c &p_client_entry, unsigned int lock_num,
        const struct lock_request *p_reqs, struct lock_op_res *op_res);
    uint32_t batch_unlock_construct_cmd_msg(client_entry_c &p_client_entry, unsigned int lock_num,
        const int *p_lock_ids, struct lock_op_res *op_res, uint8_t *buf);
    void batch_unlock_update_state_with_cmd_msg(client_entry_c &p_client_entry, unsigned int lock_num,
        const int *p_lock_ids, struct lock_op_res *op_res);
    uint32_t batch_lock_extend_construct_cmd_msg(client_entry_c &p_client_entry, unsigned int lock_num,
        const struct lock_request *p_reqs, struct lock_op_res *op_res, uint8_t *buf);
    void batch_lock_extend_update_state_with_cmd_msg(client_entry_c &p_client_entry, unsigned int lock_num,
        const struct lock_request *p_reqs, struct lock_op_res *op_res);
    void batch_set_op_ret(const client_entry_c &p_client_entry, struct lock_op_res *op_res, int op_ret) const;
    void batch_trylock_update_ref_count_on_bad_resp(client_entry_c &p_client_entry, unsigned int lock_num,
        const struct lock_request *p_reqs, uint8_t *buf);
    void batch_unlock_update_ref_count_on_bad_resp(client_entry_c &p_client_entry,
        unsigned int lock_num, const int *p_lock_ids);
    bool check_bad_resp_err(int ret, uint32_t comp_len, uint32_t cmdmsg_len, lock_entry_c *p_lock_entry,
        bool reentrant) const;
    int async_request(client_entry_c &client_entry, lock_entry_c &lock_entry, const struct lock_request *req) const;
    int check_resp_control_msg_hdr_status(int32_t status) const;
    int check_resp_control_msg_hdr(const struct dlock_control_hdr &msg_hdr,
        enum dlock_control_msg type, size_t expected_hdr_len, uint16_t expected_message_id) const;
    int check_resp_lock_cmd_msg(const struct lock_cmd_msg &lock_cmd_req,
        const struct lock_cmd_msg &lock_cmd_resp) const;
    int check_batch_resp_lock_cmd_msg(const struct lock_cmd_msg *lock_cmd_reqs,
        const struct lock_cmd_msg *lock_cmd_resps, uint32_t cmd_num) const;
    int update_locks_response_handler(client_entry_c &p_client_entry, uint8_t *buf, uint32_t buf_len,
        pthread_t update_locks_requester_tid, uint16_t next_resp_message_id);
    int recv_update_locks_response(client_entry_c &p_client_entry,
        uint8_t *buf, uint32_t buf_len, uint16_t expected_message_id);
    int process_update_locks_response(client_entry_c &p_client_entry,
        uint8_t *buf, uint32_t buf_len);
    int update_lock_entry(client_entry_c &p_client_entry, struct update_lock_body *p_msg_update);
    template <typename T>
    dlock_status_t xchg_cmd_msg(client_entry_c &p_client_entry, T &req, T **p_resp);
    dlock_status_t xchg_batch_lock_cmd_msg(client_entry_c &p_client_entry,
        struct urma_buf *p_tx_buf, uint32_t msg_len, struct urma_buf **p_rx_buf);
    void clear_m_client_map();
    int check_stats_api_invoking_freq(void);
    int trans_trylock_op_to_extend(client_entry_c *p_client, lock_entry_c *p_lock_entry,
        struct lock_cmd_msg *p_cmd_ret, void *result) const;
    void erase_obj_desc_map_by_id(client_entry_c &p_client_entry, int obj_id);
    bool add_object_entry(unsigned int len, unsigned char *buf, client_entry_c &p_client_entry,
                          const struct object_get_body *body);
    bool check_object_get(client_entry_c &p_client_entry, const struct object_get_body *body);
    void release_local_cache(client_entry_c &p_client_entry, int obj_id);
    int check_resp_object_get_body(const struct object_get_body *body) const;

    dlock_status_t post_read_and_get_res(client_entry_c &p_client_entry, uint32_t offset, uint64_t *res_val) const;
    dlock_status_t post_faa_and_get_res(client_entry_c &p_client_entry, uint32_t offset,
        uint64_t add_val, uint64_t *res_val) const;
    dlock_status_t post_cas_and_get_res(client_entry_c &p_client_entry, uint32_t offset,
        uint64_t cmp_val, uint64_t swap_val) const;

    static dlock_client _instance;
    bool m_is_inited;
    client_map_t m_client_map;
    urma_ctx *m_p_urma_ctx;
    std::shared_mutex m_map_rwlock;
    std::shared_mutex m_client_map_rwlock;
    std::mutex m_mutex_lock;
    int m_primary_port;
    bool m_ssl_enable;
    ssl_init_attr_t m_ssl_init_attr;
    trans_mode_t m_tp_mode;

    std::chrono::steady_clock::time_point m_stats_access_tp_prev;
    uint32_t m_stats_access_cnt;
    std::mutex m_stats_access_lock;
};
};
#endif
