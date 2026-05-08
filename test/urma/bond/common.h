/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2020-2020. All rights reserved.
 * Description: Common header for UBUS tests
 * Author: Lilijun
 * Create: 2020-10-19
 * Note:
 * History:
 */
#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#if defined(__cplusplus)
extern "C" {
#endif

#define ERROR_EXIT(...)                 \
    do {                                \
        fprintf(stderr, "%s:%d ", __FILE__, __LINE__);      \
        fprintf(stderr, ##__VA_ARGS__); \
        exit(EXIT_FAILURE);             \
    } while (0)

#define SUCCESS_EXIT()                 \
    do {                                \
        exit(EXIT_SUCCESS);             \
    } while (0)

#define ASSERT_AND_EXIT(cond, ...) \
    if ((cond) == false) {          \
        fprintf(stderr, "%s:%d ", __FILE__, __LINE__);      \
        fprintf(stderr, ##__VA_ARGS__); \
        exit(EXIT_FAILURE);             \
    }
#define ASSERT_CLOSE_AND_EXIT(cond, close_function,...) \
    if ((cond) == false) {          \
        fprintf(stderr, "%s:%d ", __FILE__, __LINE__);      \
        fprintf(stderr, ##__VA_ARGS__); \
        if ((close_function) != NULL){  \
            close_function();  \
            printf("ASSERT AND CLOSED\n"); \
        } \
        exit(EXIT_FAILURE);             \
    }
 
#if defined(__cplusplus)
}
#endif

#endif /* _TEST_COMMON_H_ */
