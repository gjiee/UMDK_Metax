/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 * File Name     : client_entry_c.h
 * Description   : header file for client entry class in the client lib
 * History       : create file & add functions
 * 1.Date        : 2021-06-16
 * Author        : zhangjun
 * Modification  : Created file
 */

#ifndef __CLIENT_ENTRY_C_H__
#define __CLIENT_ENTRY_C_H__

#include <unordered_map>
#include <sys/time.h>
#include <shared_mutex>

#include "lock_entry_c.h"
#include "jetty_mgr.h"
#include "ub_util.h"
#include "dlock_connection.h"
#include "object_entry_c.h"
#include "dlock_descriptor.h"

namespace dlock {
class dlock_client;

using lock_map_t = std::unordered_map<int32_t, lock_entry_c*>;
using object_map_t = std::unordered_map<int32_t, object_entry_c*>;
using object_desc_map_t = std::unordered_map<dlock_descriptor*, int32_t, hash_dlock_desc, equal_dlock_desc>;

class client_entry_c {
    friend class dlock_client;
public:
    struct debug_stats m_stats;

    client_entry_c() = delete;
    client_entry_c(int client_id, dlock_connection *p_conn, jetty_mgr *p_jetty_mgr);
    ~client_entry_c();

    void update_locks_requester(void);
    inline bool check_lock_async_state(int lock_id) const
    {
        return ((m_async_flag) && (m_async_id == lock_id));
    }
    inline void delete_local_lock_entry(std::shared_mutex &m_map_rwlock, const lock_map_t::iterator &lock_iter)
    {
        std::shared_lock<std::shared_mutex> shared_locker(m_map_rwlock);
        delete lock_iter->second;
        static_cast<void>(m_lock_map.erase(lock_iter));
        shared_locker.unlock();
    }

    inline bool get_m_obj_invalid_flag(void) const
    {
        return m_obj_invalid_flag;
    }

    inline void set_m_obj_invalid_flag(void)
    {
        m_obj_invalid_flag = true;
    }

    inline void clear_m_obj_invalid_flag(void)
    {
        m_obj_invalid_flag = false;
    }

private:
    int m_client_id;
    dlock_connection *m_p_conn;
    int m_update_lock_state;
    int m_update_lock_num;
    lock_map_t m_lock_map;
    lock_map_t m_update_map;
    bool m_async_flag;
    int m_async_op;
    int m_async_id;
    struct timeval m_async_start;
    jetty_mgr *m_p_jetty_mgr;
    unsigned long m_batch_bitmap[BITS_TO_LONGS(MAX_LOCK_BATCH_SIZE)];

    object_map_t m_object_map;
    std::shared_mutex m_omap_rwlock;

    object_desc_map_t m_obj_desc_map;
    std::mutex m_obj_desc_lock;

    /* If m_obj_invalid_flag == true, client reinit is being executed
     * and the original atomic64 object information is invalid.
     */
    bool m_obj_invalid_flag;

    void clear_m_lock_map(void) noexcept;
    void clear_m_update_map(void) noexcept;
    void update_associated_client_pointer(void);
    void clear_m_object_map(void) noexcept;
    void clear_m_obj_desc_map(void) noexcept;
};
};
#endif
