# SPDX-License-Identifier: MIT
# Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.

# perftest_mem_*.c: legacy pluggable-memory stubs (compile-time only).
# Active GPU path: perftest_gpu_alloc + cuda/gdrcopy loaders + validation.
option(PERFTEST_ENABLE_GPU_DEBUG
    "Enable GPU validation/gdr-verify diagnostics in urma_perftest" OFF)

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
    perftest_mem_gpu_proxy.c
    perftest_gpu_alloc.c
    perftest_cuda_loader.c
    perftest_gdrcopy_loader.c
    perftest_validation.c
)

set_target_properties(urma_perftest
    PROPERTIES
        C_STANDARD 99
)

if(PERFTEST_ENABLE_GPU_DEBUG)
    target_compile_definitions(urma_perftest PRIVATE PERFTEST_ENABLE_GPU_DEBUG=1)
endif()

target_include_directories(urma_perftest
    PRIVATE
        ${CMAKE_SOURCE_DIR}/urma/lib/urma/core/include
        ${CMAKE_SOURCE_DIR}/urma/lib/urma/bond/include
        ${CMAKE_SOURCE_DIR}/urma/common/include
)

target_link_libraries(urma_perftest
    PRIVATE
        urma pthread m
        dl
)

install(TARGETS urma_perftest
    DESTINATION /usr/bin
)
