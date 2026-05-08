# SPDX-License-Identifier: MIT
# Copyright (c) Huawei Technologies Co., Ltd. 2020-2025. All rights reserved.

set(DLOCK_CLIENT_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/ulock/dlock/lib/dlock/include)

include_directories(${DLOCK_CLIENT_INCLUDE_DIR})

add_executable(dlock_client_test client_test.cpp)
set_property(TARGET dlock_client_test PROPERTY CXX_STANDARD 17)
target_link_libraries(dlock_client_test dlockc dlockm pthread)

add_executable(dlock_client_object_test client_test_object.cpp)
set_property(TARGET dlock_client_object_test PROPERTY CXX_STANDARD 17)
target_link_libraries(dlock_client_object_test dlockc dlockm pthread)

install(TARGETS dlock_client_test DESTINATION /usr/bin)
install(TARGETS dlock_client_object_test DESTINATION /usr/bin)