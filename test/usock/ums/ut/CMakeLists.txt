#
# SPDX-License-Identifier: MIT
# Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
#

cmake_minimum_required(VERSION 3.12.1)

function(add_ut) 
    set(UT_NAME ${ARGV0})

    file(GLOB UT_FILE ./${UT_NAME}.cpp)

    add_executable(${UT_NAME} 
        ${UT_FILE}
    )

    target_include_directories(
        ${UT_NAME} PUBLIC
        include
        ${CMAKE_CURRENT_LIST_DIR}
        /usr/include/libnl3
        /usr/local/lib64
    )

    target_link_libraries(
        ${UT_NAME}  PUBLIC
        libgtest.a
        libgtest_main.a
        nl-genl-3
        nl-3
    )

    install(TARGETS ${UT_NAME})
endfunction()

add_definitions(-DLOCAL_IP=\"${LOCAL_IP}\")
add_definitions(-DREMOTE_IP=\"${REMOTE_IP}\")
add_definitions(-DLOCAL_IPV6=\"${LOCAL_IPV6}\")
add_definitions(-DREMOTE_IPV6=\"${REMOTE_IPV6}\")

add_ut(ums_test_ut)
add_ut(ums_test_ut_client)
add_ut(ums_test_ut_server)
add_subdirectory(kmode_ut)
