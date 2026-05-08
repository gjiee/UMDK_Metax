# SPDX-License-Identifier: MIT
# Copyright (c) Huawei Technologies Co., Ltd. 2020-2025. All rights reserved.

include_directories(/usr/include/ub/umdk)
include_directories(/usr/include/ub/umdk/urma)
include_directories(/usr/include/ub/umdk/urpc)
include_directories(${CMAKE_CURRENT_LIST_DIR})
include_directories(${CMAKE_CURRENT_LIST_DIR}/../../include/framework)

add_executable(urpc_framework_example
    urpc_framework_example.c
    urpc_framework_client.c
    urpc_framework_server.c)

set_property(TARGET urpc_framework_example PROPERTY C_STANDARD 11)
target_link_libraries(urpc_framework_example urpc)
install(TARGETS urpc_framework_example DESTINATION /usr/bin)
install(DIRECTORY DESTINATION /usr/share/doc/umdk-examples/urpc_example/urpc_framework_example)