/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <signal.h>

#include <linux/smc.h>
#include <netlink/netlink.h>
#include <netlink/socket.h>
#include <netlink/msg.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <linux/rtnetlink.h>

typedef struct pnet_opt {
    char *pnet_id;
    char *eth_name;
    char *ub_name;
    int ub_port;
    unsigned char cmd;
} pnet_opt;

static int cb_handler(struct nl_msg *msg, void *arg)
{
    (void)msg;
    (void)arg;
    return NL_OK;
}