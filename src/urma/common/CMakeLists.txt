# SPDX-License-Identifier: MIT
# Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.

aux_source_directory(${CMAKE_CURRENT_LIST_DIR} URMA_COMMON_SRCS)
if(NOT "${PERF_CYCLE}" STREQUAL "enable")
    list(REMOVE_ITEM URMA_COMMON_SRCS "${CMAKE_CURRENT_LIST_DIR}/ub_perf_cycle.c")
endif()

add_library(urma_common SHARED
    ${URMA_COMMON_SRCS}
)
add_library(urma_common_static STATIC
    ${URMA_COMMON_SRCS}
)

set_target_properties(urma_common
    PROPERTIES
        C_STANDARD 99
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
)
set_target_properties(urma_common_static
    PROPERTIES
        C_STANDARD 99
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
)

target_include_directories(urma_common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(urma_common_static
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(urma_common
    PRIVATE
        dl rt
)
target_link_libraries(urma_common_static
    PRIVATE
        dl rt
)
if("${PERF_CYCLE}" STREQUAL "enable")
    target_link_libraries(urma_common PRIVATE m)
endif()

install(TARGETS urma_common
    DESTINATION /usr/lib64
)
