#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
# Des: Build script for RivulNET

set -e

CODE_PATH="$(
  cd $(dirname $0)/..
  pwd
)"
server="false"
client="false"
local_ip=""
remote_ip=""
local_ipv6=""
remote_ipv6=""
cmake_args=()

function usage()
{
    echo "Usage:"
    echo "$0   [options]"
    echo "-c                        clean and resume environment"
    echo "-C                        run client"
    echo "-S                        run server"
    echo "-l <local_ip>             local ip"
    echo "-r <remote_ip>            remote ip"
    echo "-L <local_ipv6>           local ipv6"
    echo "-R <remote_ipv6>          remote ipv6"
}

function check_options()
{
    if [[ "${local_ip}" == "" ]]; then
        echo "error, local_ip is not set"
        exit 1
    fi

    if [[ "${remote_ip}" == "" ]]; then
        echo "error, remote_ip is not set"
        exit 1
    fi

    if [[ "${local_ipv6}" == "" ]]; then
        echo "error, local_ipv6 is not set"
        exit 1
    fi

    if [[ "${remote_ipv6}" == "" ]]; then
        echo "error, remote_ipv6 is not set"
        exit 1
    fi
    cmake_args+=(-DLOCAL_IP=${local_ip} -DREMOTE_IP=${remote_ip} -DLOCAL_IPV6=${local_ipv6} -DREMOTE_IPV6=${remote_ipv6})
}

function build_test_ums()
{
    cd ${CODE_PATH}
    rm -rf build/
    mkdir build
    cd build
    cmake_args+=(-DBUILD_WITH_UT=ON)
    cmake ${cmake_args[@]} ..
    make -j

    insmod ./ums/kmod/ums/ums.ko
    sysctl -w net.ums.dim_enable=1

    insmod ./ums/test/ut/kmode_ut/ums_test.ko

    if [[ "${client}" == "true" ]]; then
        echo 2097152 > /proc/sys/net/ums/snd_buf
    fi
}

function run_ut()
{
    cd ${CODE_PATH}/build

    if [[ "${server}" == "true" ]]; then
        ./ums/test/ut/ums_test_ut_server
    elif [[ "${client}" == "true" ]]; then
        ./ums/test/ut/ums_test_ut_client
    else
        ./ums/test/ut/ums_test_ut
    fi

    sleep 15
}

function resume_environment()
{
    rmmod ums_test || true
    rmmod ums || true
}

while getopts "cCl:r:SL:R:" arg
do
    case "${arg}" in
        c)
            resume_environment
            exit $?
            ;;
        C)
            client="true"
            ;;
        l)
            local_ip="${OPTARG}"
            ;;
        r)
            remote_ip="${OPTARG}"
            ;;
        S)
            server="true"
            ;;
        L)
            local_ipv6="${OPTARG}"
            ;;
        R)
            remote_ipv6="${OPTARG}"
            ;;
        ?)
            usage
            exit 1
            ;;
    esac
done

check_options
build_test_ums
run_ut
resume_environment

exit 0
