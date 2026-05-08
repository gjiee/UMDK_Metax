/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: umq qbuf Singly-linked list
 */
#ifndef UMQ_BUF_LIST_H
#define UMQ_BUF_LIST_H

#include "umq_types.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef struct umq_buf_list {
    umq_buf_t *first;
} umq_buf_list_t;

#define QBUF_LIST_FIRST(head)    ((head)->first)

#define QBUF_LIST_INIT(head) do {                                               \
    QBUF_LIST_FIRST((head)) = NULL;                                             \
} while (0)

#define QBUF_LIST_EMPTY(head)    ((head)->first == NULL)

#define QBUF_LIST_NEXT(element)    ((element)->qbuf_next)

#define QBUF_LIST_INSERT_HEAD(head, element) do {                               \
    QBUF_LIST_NEXT((element)) = QBUF_LIST_FIRST((head));                        \
    QBUF_LIST_FIRST((head)) = (element);                                        \
} while (0)

#define	QBUF_LIST_INSERT_AFTER(element1, element2) do {                         \
    QBUF_LIST_NEXT(element2) = QBUF_LIST_NEXT(element1);                        \
    QBUF_LIST_NEXT(element1) = (element2);                                      \
} while (0)

#define QBUF_LIST_FOR_EACH(element, head)                                       \
    for ((element) = QBUF_LIST_FIRST((head));                                   \
        (element);                                                              \
        (element) = QBUF_LIST_NEXT(element))

#define QBUF_LIST_FOR_EACH_SAFE(element, head, next)                            \
    for ((element) = QBUF_LIST_FIRST((head));                                   \
        (element) && ((next) = QBUF_LIST_NEXT(element), 1);                     \
        (element) = (next))

#define QBUF_LIST_REMOVE_HEAD(head) do {                                        \
    QBUF_LIST_FIRST((head)) = QBUF_LIST_NEXT(QBUF_LIST_FIRST((head)));          \
} while (0)

#define QBUF_LIST_REMOVE_AFTER(element) do {                                    \
    QBUF_LIST_NEXT(element) = QBUF_LIST_NEXT(QBUF_LIST_NEXT(element));          \
} while (0)

#define QBUF_LIST_REMOVE(head, element, type) do {                              \
    if (QBUF_LIST_FIRST((head)) == (element)) {                                 \
        QBUF_LIST_REMOVE_HEAD((head));                                          \
    } else if (QBUF_LIST_FIRST((head)) != NULL) {                               \
        struct type *curelement = QBUF_LIST_FIRST(head);                        \
        while (QBUF_LIST_NEXT(curelement) != (element))                         \
            curelement = QBUF_LIST_NEXT(curelement);                            \
        QBUF_LIST_REMOVE_AFTER(curelement);                                     \
    }                                                                           \
} while (0)

#ifdef __cplusplus
}
#endif

#endif
