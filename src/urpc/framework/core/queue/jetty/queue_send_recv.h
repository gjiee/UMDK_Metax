/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: use jetty to realize send recv
 */

#ifndef QUEUE_SEND_RECV_H
#define QUEUE_SEND_RECV_H

#include <pthread.h>
#include "urma_types.h"
#include "queue.h"
#include "urpc_bitmap.h"
#include "urpc_hmap.h"
#include "urpc_list.h"
#include "urpc_slist.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum jdtm_jetty_node_state {
    JDTM_JETTY_NODE_STATE_IDLE = 0,
    JDTM_JETTY_NODE_STATE_ERROR, // jetty node with error CR
    JDTM_JETTY_NODE_STATE_FATAL, // jetty node with modify failure
    JDTM_JETTY_NODE_STATE_WORKING,
} jdtm_jetty_node_state_t;

typedef struct urpc_mapping_jetty_fe_idx_in {
    uint32_t fe_idx;
    uint32_t jetty_id;
} urpc_mapping_jetty_fe_idx_in_t;

typedef struct urpc_mapping_jetty_fe_idx_out {
    int ret;
} urpc_mapping_jetty_fe_idx_out_t;

/* APIs & Definitions based on jetty disorder transmission mode(jdtm) */
// hmap node in working map, manage a list of jetty nodes with same dst eid
typedef struct jetty_work_list_node {
    struct urpc_hmap_node node;
    urpc_list_t head; // list of working jetty nodes
    urpc_eid_t eid;   // dst eid of these working jetty node
} jetty_work_list_node_t;

// io_ctx for each io
typedef struct jdtm_io_ctx {
    void *jetty_node; // ptr to jdtm_jetty_node_t
    uint64_t user_ctx;
    uint32_t index; // bitmap index
} jdtm_io_ctx_t;

// jetty node for each jetty, 1 jetty node must in error list, or idle list, or working list
typedef struct jdtm_jetty_node {
    jdtm_jetty_node_state_t state;

    urpc_list_t idle_list_node;
    urpc_list_t error_list_node;
    urpc_list_t fatal_list_node;
    struct {
        urpc_list_t work_list_node;
        jetty_work_list_node_t *work_list_head; // ptr to jetty_work_list_node_t
    };

    urma_jetty_t *jetty;
    jdtm_io_ctx_t *io_ctx; // size is urpc_bitmap_nbytes(depth), we should ensure alloc io_ctx when available_depth > 0
    urpc_bitmap_t bitmap;  // size is urpc_bitmap_nbytes(depth), only use jetty depth bits
    uint32_t vtpn;  // in most cases, vtpn is same in one jetty_work_list_node_t. vtpn changed means dst is re-imported,
                    // we need to find 1 same vtpn jetty_node or choose 1 new idle jetty_node
    uint32_t depth; // jetty depth
    uint32_t available_depth; // jdtm jetty node is sendable only when available_depth >= 1
    uint32_t jetty_state : 8;
    uint32_t rsvd : 24;
    char buf[0];              // jdtm jetty node buffer pool, used for jdtm_io_ctx_t and bitmap
} jdtm_jetty_node_t;

typedef struct jdtm_queue_local {
    queue_local_t local_q;          // placed at the beginning of the definition to facilitate conversion of types
    urma_jfc_t *jfs_jfc;
    urma_jfc_t *jfr_jfc;
    urma_jfce_t *jfce;

    uint32_t jetty_num;
    // used for new disorder queue management
    uint32_t jetty_node_size; // jetty_node + bitmap + io_ctx
    uint32_t fatal_list_num;

    pthread_spinlock_t lock; // notice: this lock used to manage jetty node
    struct urpc_hmap working_list;
    urpc_list_t idle_list;
    urpc_list_t error_list; // jetty node with CR error
    urpc_list_t fatal_list; // jetty node with modify failure
    urpc_list_t work_list_nodes_head;

    urpc_list_t flush_op_used_list;
    urpc_list_t flush_op_unused_list;

    jetty_work_list_node_t *work_list_nodes; // size is jetty_num
    jdtm_jetty_node_t *jetty_nodes;     // size is jetty_num
    char buf[0]; // jdtm queue buffer pool, used for jdtm_jetty_node_t and jetty_work_list_node_t
} jdtm_queue_local_t;

#ifdef __cplusplus
}
#endif

#endif
