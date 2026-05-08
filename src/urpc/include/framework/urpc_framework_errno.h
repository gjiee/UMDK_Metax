/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: Public header file of URPC errno
 * Create: 2024-4-19
 * Note:
 * History: 2024-4-19
 */

#ifndef URPC_ERRNO_H
#define URPC_ERRNO_H

#ifdef __cplusplus
extern "C" {
#endif

#define URPC_SUCCESS                                (0)
#define URPC_FAIL                                   (-1)

#define URPC_U16_FAIL                               (0xFFFF)
#define URPC_U32_FAIL                               (0xFFFFFFFF)
#define URPC_U64_FAIL                               (0xFFFFFFFFFFFFFFFF)

#define URPC_PARTIAL_SUCCESS                        (1)

#define URPC_TRUE                                   (1)
#define URPC_FALSE                                  (0)

#define URPC_INVALID_HANDLE                         (0)
#define URPC_INVALID_ID_U8                          (0xFF)
#define URPC_INVALID_ID_U32                         (0xFFFFFFFF)
#define URPC_INVALID_FUNC_ID                        (0xFFFFFFFFFFFFFFFF)
#define URPC_INVALID_FD                             (-1)

#define URPC_ERR_TRANSPORT_ERR                      (0x0202)
#define URPC_ERR_SERVER_DROP                        (0x0204)
#define URPC_ERR_SESSION_CLOSE                      (0x0205)
#define URPC_ERR_TIMEOUT                            (0x0206)
#define URPC_ERR_REM_LEN_ERR                        (0x020B)
#define URPC_ERR_CIPHER_ERR                         (0x020F)
#define URPC_ERR_FUNC_NULL                          (0x0210)
#define URPC_ERR_INIT_PART_FAIL                     (0x0214)
#define URPC_ERR_LOCAL_QUEUE_ERR                    (0x0215)
#define URPC_ERR_REMOTE_QUEUE_ERR                   (0x0216)
#define URPC_ERR_VERSION_ERR                        (0x0224)
#define URPC_ERR_URPC_HDR_ERR                       (0x0225)
#define URPC_ERR_JFC_ERROR                          (0x0226)
#define URPC_ERR_JETTY_ERROR                        (0x0227)
/* reserve 20 for urpc system errno (0x02B0-0x02C3) */
#define URPC_ERR_EPERM                              (0x02B0)
#define URPC_ERR_EAGAIN                             (0x02B1)
#define URPC_ERR_ENOMEM                             (0x02B2)
#define URPC_ERR_EBUSY                              (0x02B3)
#define URPC_ERR_EEXIST                             (0x02B4)
#define URPC_ERR_EINVAL                             (0x02B5)
/* reserve 30 for urma_cr_status (0x02C4-0x02E1) */
#define URPC_ERR_CR_UNDEFINED                       (0x02C4)
#define URPC_ERR_CR_UNSUPPORTED_OPCODE_ERR          (0x02C5)
#define URPC_ERR_CR_LOC_LEN_ERR                     (0x02C6)
#define URPC_ERR_CR_LOC_OPERATION_ERR               (0x02C7)
#define URPC_ERR_CR_LOC_ACCESS_ERR                  (0x02C8)
#define URPC_ERR_CR_REM_RESP_LEN_ERR                (0x02C9)
#define URPC_ERR_CR_REM_UNSUPPORTED_REQ_ERR         (0x02CA)
#define URPC_ERR_CR_REM_OPERATION_ERR               (0x02CB)
#define URPC_ERR_CR_REM_ACCESS_ABORT_ERR            (0x02CC)
#define URPC_ERR_CR_ACK_TIMEOUT_ERR                 (0x02CD)
#define URPC_ERR_CR_RNR_RETRY_CNT_EXC_ERR           (0x02CE)
#define URPC_ERR_CR_WR_FLUSH_ERR                    (0x02CF)
#define URPC_ERR_CR_WR_SUSPEND_DONE                 (0x02D0)
#define URPC_ERR_CR_WR_FLUSH_ERR_DONE               (0x02D1)
#define URPC_ERR_CR_WR_UNHANDLED                    (0x02D2)
#define URPC_ERR_CR_LOC_DATA_POISON                 (0x02D3)
#define URPC_ERR_CR_REM_DATA_POISON                 (0x02D4)
/* reserve 30 for urma_async_event_type (0x02E2-0x02FF) */
#define URPC_ERR_EVENT_UNDEFINED                    (0x02E2)
#define URPC_ERR_EVENT_JFC_ERR                      (0x02E3)
#define URPC_ERR_EVENT_JFR_ERR                      (0x02E5)
#define URPC_ERR_EVENT_JFR_LIMIT                    (0x02E6)
#define URPC_ERR_EVENT_JETTY_ERR                    (0x02E7)
#define URPC_ERR_EVENT_JETTY_LIMIT                  (0x02E8)

/* queue is ready for work */
#define URPC_EVENT_QUEUE_READY                  (0x11000)
/* queue has seen a failure but expects to recover */
#define URPC_ERR_EVENT_QUEUE_FAILURE            (0x11001)
/* queue has seen a failure that it cannot recover from */
#define URPC_ERR_EVENT_QUEUE_SHUTDOWN           (0x11002)
/* request timeout */
#define URPC_ERR_EVENT_REQ_TIMEOUT              (0x11005)
/* channel has seen a failure that it cannot recover from */
#define URPC_ERR_EVENT_CHANNEL_FAULT            (0x11010)

/* finish a request */
#define URPC_EVENT_REQ_SUCCESS                  (0x12000)
/* function is not supported */
#define URPC_EVENT_FUNCTION_NOT_SUPPORT         (0x12001)
/* argument buffer of the server is insufficient */
#define URPC_EVENT_NO_MEM                       (0x12002)
/* uRPC call timed out */
#define URPC_EVENT_TIMEOUT                      (0x12003)
/* urpc protocol mismatch */
#define URPC_EVENT_PROTO_NOT_SUPPORT            (0x12004)

#ifdef __cplusplus
}
#endif

#endif