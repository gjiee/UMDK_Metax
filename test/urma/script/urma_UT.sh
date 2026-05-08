#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
#
set -e

SCRIPT_PATH=$(cd $(dirname $0);pwd)

function start_test()
{
    echo $SCRIPT_PATH
    mkdir -p ../../../src/build
    cd $SCRIPT_PATH/../../../src/build/
    if [ -d ./build ]; then
        rm -r build;
    fi
    cmake .. -D BUILD_ALL=disable -D BUILD_URMA=enable
    make install -j
    cd $SCRIPT_PATH/..
    mkdir -p build
    cd build
    cmake ..
    make -j
    ./urma_gtest
    echo -e "\033[32mSUCCESS\033[0m"
    date

    exit 0
}

start_test
