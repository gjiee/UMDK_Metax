# SPDX-License-Identifier: MIT
# Copyright (c) Huawei Technologies Co., Ltd. 2020-2025. All rights reserved.

# Find required packages
find_package(GLIB REQUIRED COMPONENTS gobject)

include_directories(${CMAKE_SOURCE_DIR}/ulock/dlock/lib/include)
include_directories(include)

add_subdirectory(server)
add_subdirectory(client)
