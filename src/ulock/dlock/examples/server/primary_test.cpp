/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 * File Name     : primary_test.cpp
 * Description   : test entrance of dlock primary
 * History       : create file & add functions
 * 1.Date        : 2021-06-11
 * Author        : zhangjun
 * Modification  : Created file
 */

#include <cstddef>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>

#include "dlock_server_api.h"
#include "example_common.h"

using namespace dlock;

#define SERVER_LIFE_TIME 6000

static char *g_dev_name = nullptr;
static char *g_eid_str = nullptr;
static char *g_server_ip = nullptr;
static uint32_t g_recovery_client_num = 0;
static int g_server_port = 0;
static int g_loglevel = LOG_WARNING;
static trans_mode_t g_tp_mode = SEPERATE_CONN;

int main(int argc, char *argv[]) {
    printf("this is primary\n");
    int opt;

    while ((opt = getopt(argc, argv, "c:i:e:d:p:m:g:")) != -1) {
        switch (opt) {
            case 'c': // recovery client number
                g_recovery_client_num = atoi(optarg);
                break;
            case 'i': // server IP
                g_server_ip = strdup(optarg);
                break;
            case 'e': // eid string
                g_eid_str = strdup(optarg);
                break;
            case 'd': // device name
                g_dev_name = strdup(optarg);
                break;
            case 'p': // server port
                g_server_port = atoi(optarg);
                break;
            case 'm': // transport mode
                g_tp_mode = (trans_mode_t)atoi(optarg);
                break;
            case 'g': // log level
                g_loglevel = atoi(optarg);
                break;
            default:
                printf("Usage: %s [-c recovery_client_num] [-i server_ip] [-e eid] [-d dev_name] \
                    [-p server_port] [-m transport_mode] [-g log_level]\n", argv[0]);
                printf("Options: "
                    "-c NUM    Recovery client number \n"
                    "-i IP     Server IP address \n"
                    "-e EID    EID string \n"
                    "-d DEV    Device name \n"
                    "-p PORT   Server port \n"
                    "-m MODE   Transport mode \n"
                    "-g NUM    Log level\n");
                return -1;
        }
    }
    if (g_server_ip == nullptr) {
        printf("Error: Server IP must be provided\n");
        return -1;
    }

    if (g_eid_str == nullptr && g_dev_name == nullptr) {
        printf("Error: At least one of eid or dev_name must be provided\n");
        return -1;
    }

    if (g_server_port <= 0) {
        printf("Error: Server port is either not provided or provided with invalid value\n");
        return -1;
    }

    if (g_loglevel < 0 || g_loglevel > 7) {
        printf("Error: Invalid log level\n");
        return -1;
    }

    int ret;
    unsigned int max_server_num = 10;
    int server_id;
    struct server_cfg cfg;
    cfg.type = SERVER_PRIMARY;
    cfg.dev_name = g_dev_name;
    cfg.primary.num_of_replica = 0;
    cfg.primary.recovery_client_num = g_recovery_client_num;
    cfg.primary.ctrl_cpuset = nullptr;
    cfg.primary.cmd_cpuset = nullptr;
    cfg.primary.server_ip_str = g_server_ip;
    cfg.primary.server_port = g_server_port;
    cfg.primary.replica_port = 0;
    cfg.primary.replica_enable = false;
    cfg.log_level = g_loglevel;
    cfg.tp_mode = g_tp_mode;

    cfg.ssl.ssl_enable = false;
    cfg.ssl.ca_path = nullptr;
    cfg.ssl.crl_path = nullptr;
    cfg.ssl.cert_path = nullptr;
    cfg.ssl.prkey_path = nullptr;
    cfg.ssl.cert_verify_cb = nullptr;
    cfg.ssl.prkey_pwd_cb = nullptr;
    cfg.ssl.erase_prkey_cb = nullptr;

    if (g_eid_str != nullptr) {
        ret = str_to_urma_eid(g_eid_str, &cfg.eid);
        if (ret != 0) {
            printf("invalid eid: %s\n", g_eid_str);
            return -1;
        }
    } else {
        cfg.eid = {0};
    }

    ret = dserver_lib_init(max_server_num);
    if (ret != 0) {
        printf("dlock server lib init failed! ret: %d\n", ret);
        return -1;
    }

    ret = server_start(cfg, server_id);
    if (ret != 0) {
        printf("server start failed! ret: %d", ret);
        return -1;
    }
    printf("server start successfully! server_id: %d, server_type: %d, recovery_client_num: %d, "
        " SERVER_LIFE_TIME: %ds\n", server_id, cfg.type, cfg.primary.recovery_client_num, SERVER_LIFE_TIME);

    sleep(SERVER_LIFE_TIME);

    ret = server_stop(server_id);
    if (ret != 0) {
        printf("server stop failed! ret: %d, server_id: %d\n", ret, server_id);
        return -1;
    }
    printf("server stop successfully! server_id: %d\n", server_id);

    dserver_lib_deinit();

    if (g_server_ip != nullptr) {
        free(g_server_ip);
    }

    if (g_eid_str != nullptr) {
        free(g_eid_str);
    }

    if (g_dev_name != nullptr) {
        free(g_dev_name);
    }

    pthread_exit(NULL);
    return 0;
}
