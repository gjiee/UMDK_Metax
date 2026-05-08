# SPDX-License-Identifier: MIT
# Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.

add_executable(urma_sample
    urma_sample.c
)

set_target_properties(urma_sample
    PROPERTIES
        C_STANDARD 99
)

target_link_libraries(urma_sample urma pthread)

install(TARGETS urma_sample
    DESTINATION /usr/bin
)
install(FILES
    ${CMAKE_SOURCE_DIR}/urma/examples/README.md
    DESTINATION /usr/share/doc/umdk-examples/urma_example
)