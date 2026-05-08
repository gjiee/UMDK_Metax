#
# SPDX-License-Identifier: MIT
# Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
#

# Ums test code makefile
cmake_minimum_required(VERSION 3.12.1)

project(ums_test)

set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}")

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../src/usock/ums ${CMAKE_BINARY_DIR}/ums/)
add_subdirectory(ut ${CMAKE_BINARY_DIR}/ums/test/ut)