# SPDX-License-Identifier: MIT
# Copyright (c) Huawei Technologies Co., Ltd. 2020-2025. All rights reserved.

include_directories(${CMAKE_CURRENT_LIST_DIR}/../../../include/framework)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../../include/umq)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../util)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../)
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../../../urma/common/include)

file(GLOB PERFTEST_COMMON_FILES
    "${CMAKE_CURRENT_LIST_DIR}/../perftest_thread.c"
    "${CMAKE_CURRENT_LIST_DIR}/../perftest_latency.c"
    "${CMAKE_CURRENT_LIST_DIR}/../perftest_qps.c"
    "${CMAKE_CURRENT_LIST_DIR}/../perftest_util.c"
    "${CMAKE_CURRENT_LIST_DIR}/../perftest_common.c"
)

add_executable(umq_perftest
    ${PERFTEST_COMMON_FILES}
    umq_perftest_param.c
    umq_perftest_qps.c
    umq_perftest_latency.c
    umq_perftest.c)

set_property(TARGET umq_perftest PROPERTY C_STANDARD 11)
target_link_libraries(umq_perftest urma urma_common umq umq_buf pthread m)
install(TARGETS umq_perftest DESTINATION /usr/bin)

if(NOT TARGET uninstall)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake_uninstall.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/../../../cmake_uninstall.cmake"
        IMMEDIATE @ONLY
    )

    add_custom_target(uninstall COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/../../../cmake_uninstall.cmake)
endif()