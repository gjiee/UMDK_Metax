# SPDX-License-Identifier: MIT
# Copyright (c) Huawei Technologies Co., Ltd. 2020-2025. All rights reserved.

set(DLOCK_SERVER_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/ulock/dlock/lib/dlock/include)

include_directories(${DLOCK_SERVER_INCLUDE_DIR})

add_executable(dlock_primary_test primary_test.cpp)
set_property(TARGET dlock_primary_test PROPERTY CXX_STANDARD 17)
target_link_libraries(dlock_primary_test dlocks dlockm pthread)
install(TARGETS dlock_primary_test DESTINATION /usr/bin)
