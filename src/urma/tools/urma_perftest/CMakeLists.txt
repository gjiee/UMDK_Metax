# SPDX-License-Identifier: MIT
# Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.

#
# 注: 下面 4 个 perftest_mem_*.c 是 GDR (GPU Direct RDMA) 框架引入的
# "可插拔内存后端"骨架. 当前只是 stub + 注释 + print, 假装运行成功,
# 让 perftest_memory_ops 抽象在编译期闭环. 真正的 CUDA / ROCm 库链接
# 会在后续 commit 中通过 option(URMA_WITH_CUDA ON) / option(URMA_WITH_ROCM ON)
# 条件加上.
# 详细设计与 patch 清单见 docs/urma_gdr_analysis.md.
#
add_executable(urma_perftest
    urma_perftest.c
    perftest_parameters.c
    perftest_communication.c
    perftest_resources.c
    perftest_run_test.c
    perftest_mem_host.c
    perftest_mem_cuda.c
    perftest_mem_rocm.c
    perftest_mem_dmabuf.c
)

set_target_properties(urma_perftest
    PROPERTIES
        C_STANDARD 99
)

target_include_directories(urma_perftest
    PRIVATE
        ${CMAKE_SOURCE_DIR}/urma/lib/urma/core/include
        ${CMAKE_SOURCE_DIR}/urma/lib/urma/bond/include
        ${CMAKE_SOURCE_DIR}/urma/common/include
)

target_link_libraries(urma_perftest
    PRIVATE
        urma pthread m
)

install(TARGETS urma_perftest
    DESTINATION /usr/bin
)
