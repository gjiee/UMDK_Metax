/*
 * UMS(UB Memory based Socket)
 *
 * Description: ums admin, provide dfx capabilities
 *
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *
 * UMS implementation:
 *     Author(s): SunFang
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

#include <netlink/netlink.h>
#include <netlink/socket.h>
#include <netlink/msg.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <netlink/errno.h>
#include <linux/smc.h>
#include <linux/rtnetlink.h>
#include <linux/sock_diag.h>
#include <linux/smc_diag.h>

#define UMS_ADMIN_ARG_CNT 2

typedef enum tool_cmd_type {
    TOOL_CMD_SHOW,
    TOOL_CMD_NUM
} tool_cmd_type_t;

typedef struct tool_config {
    tool_cmd_type_t cmd;
} tool_config_t;

typedef struct tool_cmd {
    char *cmd;
    tool_cmd_type_t type;
} tool_cmd_t;

typedef struct ums_cmd_query_res {
    struct {
        uint32_t type;
        uint32_t key;
        uint32_t key_ext;
        uint32_t key_cnt;
    } in;
    struct {
        uint64_t addr;
        uint32_t len;
        uint64_t save_ptr; /* save ubcore address for second ioctl */
    } out;
} ums_cmd_query_res_t;

typedef struct ums_cmd_hdr {
    uint32_t command;
    uint32_t args_len;
    uint64_t args_addr;
} ums_cmd_hdr_t;

enum {
    UBCORE_ATTR_UNSPEC,
    UBCORE_HDR_COMMAND,
    UBCORE_HDR_ARGS_LEN,
    UBCORE_HDR_ARGS_ADDR,
    UBCORE_ATTR_NS_MODE,
    UBCORE_ATTR_DEV_NAME,
    UBCORE_ATTR_NS_FD,
    UBCORE_ATTR_AFTER_LAST
};

enum {
    UMS_NLA_LGR_R_UNSPEC,
    UMS_NLA_LGR_R_ID, /* u32 */
    UMS_NLA_LGR_R_ROLE, /* u8 */
    UMS_NLA_LGR_R_CONNS_NUM, /* u32 */
    UMS_NLA_LINK_ID, /* U8 */
    UMS_NLA_LINK_STATE, /* u32 */
    UMS_NLA_LINK_CONN_CNT, /* u32 */
    UMS_NLA_SND_BUF_CNT, /* u32 */
    UMS_NLA_SND_BUF_REG, /* u32 */
    UMS_NLA_SND_BUF_USED, /* u32 */
    UMS_NLA_RMB_BUF_CNT, /* u32 */
    UMS_NLA_RMB_BUF_REG, /* u32 */
    UMS_NLA_RMB_BUF_USED, /* u32 */
    UMS_NLA_CONN_ID_LOCAL, /* u32 */
    UMS_NLA_CONN_RTOKEN_IDX, /* u32 */
    UMS_NLA_CONN_PEND_TX_WR, /* u32 */
    UMS_NLA_CONN_TX_RX_REFCNT, /* u32 */
    __UMS_NLA_LGR_R_MAX,
    UMS_NLA_LGR_R_MAX = __UMS_NLA_LGR_R_MAX - 1
};

enum {
    UMS_NETLINK_GET_LGR_STATUS = 1,
    UMS_NETLINK_GET_LINK_STATUS,
    UMS_NETLINK_GET_CONN_STATUS,
};

#define UMS_GENL_FAMILY_DFX_NAME    "UMS_GENL_DFX"
#define UMS_GENL_FAMILY_DFX_VERSION    1

static int ums_dfx_cb_handler(struct nl_msg *msg, void *arg)
{
    struct nlmsghdr *hdr = nlmsg_hdr(msg);
    struct genlmsghdr *genlhdr = genlmsg_hdr(hdr);
    struct nlattr *attr_ptr = genlmsg_data(genlhdr);
    int len = genlmsg_attrlen(genlhdr, 0);
    struct nlattr *nla;
    int rem;

    nla_for_each_attr(nla, attr_ptr, len, rem) {
        int type = nla_type(nla);
        if (type == UMS_NLA_LGR_R_ID) {
            (void)printf("link group id is           :%u\n", nla_get_u32(nla));
        }
        if (type == UMS_NLA_LGR_R_CONNS_NUM) {
            (void)printf("In link group, conn num is :%u\n", nla_get_u32(nla));
        }
        if (type == UMS_NLA_LGR_R_ROLE) {
            (void)printf("role is                    :%u\n", (uint32_t)nla_get_u8(nla));
        }
        if (type == UMS_NLA_LINK_ID) {
            (void)printf("link id is                 :%u\n", (uint32_t)nla_get_u8(nla));
        }
        if (type == UMS_NLA_LINK_STATE) {
            (void)printf("link state is              :%u\n", nla_get_u32(nla));
        }
        if (type == UMS_NLA_LINK_CONN_CNT) {
            (void)printf("conn cnt is                :%u\n", nla_get_s32(nla));
        }
        if (type == UMS_NLA_SND_BUF_CNT) {
            (void)printf("send buf cnt is            :%u\n", nla_get_u32(nla));
        }
        if (type == UMS_NLA_SND_BUF_REG) {
            (void)printf("send buf reg is            :%u\n", nla_get_u32(nla));
        }
        if (type == UMS_NLA_SND_BUF_USED) {
            (void)printf("send buf used is           :%u\n", nla_get_u32(nla));
        }
        if (type == UMS_NLA_RMB_BUF_CNT) {
            (void)printf("rmb buf cnt is             :%u\n", nla_get_u32(nla));
        }
        if (type == UMS_NLA_RMB_BUF_REG) {
            (void)printf("rmb buf reg is             :%u\n", nla_get_u32(nla));
        }
        if (type == UMS_NLA_RMB_BUF_USED) {
            (void)printf("rmb buf used is            :%u\n", nla_get_u32(nla));
        }
        if (type == UMS_NLA_CONN_ID_LOCAL) {
            (void)printf("conn id is                 :%u\n", nla_get_u32(nla));
        }
        if (type == UMS_NLA_CONN_RTOKEN_IDX) {
            (void)printf("conn rtoken idx is         :%u\n", nla_get_u32(nla));
        }
        if (type == UMS_NLA_CONN_PEND_TX_WR) {
            (void)printf("conn pend tx wr is         :%u\n", nla_get_s32(nla));
        }
        if (type == UMS_NLA_CONN_TX_RX_REFCNT) {
            (void)printf("conn tx rx recnt is        :%u\n ", nla_get_s32(nla));
        }
    }

    (void)printf("\n");
    return 0;
}

static int admin_show_res(const tool_config_t *cfg)
{
    int rc = 0;
    int id, nlmsg_flags = 0;
    struct nl_sock *sk;
    struct nl_msg *msg;
    ums_cmd_query_res_t *arg;
    ums_cmd_hdr_t hdr;

    /* Allocate a netlink socket and connect to it */
    sk = nl_socket_alloc();
    if (!sk) {
        perror("create socket error");
        return -1;
    }
    rc = genl_connect(sk);
    if (rc) {
        rc = -1;
        goto free_nl_socket;
    }
    id = genl_ctrl_resolve(sk, UMS_GENL_FAMILY_DFX_NAME);
    if (id < 0) {
        rc = -1;
        if (id == -NLE_OBJ_NOTFOUND) {
            fprintf(stderr, "UMS module not loaded\n");
        } else {
            printf("ctrl resolve error: %d\n", id);
        }
        goto close_nl;
    }
    nl_socket_modify_cb(sk, NL_CB_VALID, NL_CB_CUSTOM, ums_dfx_cb_handler, NULL);

    /* Allocate a netlink message and set header information. */
    msg = nlmsg_alloc();
    if (!msg) {
        printf("nlmsg alloc error\n");
        rc = -1;
        goto close_nl;
    }

    arg = (ums_cmd_query_res_t *)calloc(1, sizeof(ums_cmd_query_res_t));
    if (arg == NULL) {
        rc = -1;
        goto free_nlmsg;
    }

    hdr.command = (uint32_t)UMS_NETLINK_GET_LGR_STATUS;
    hdr.args_len = (uint32_t)sizeof(ums_cmd_query_res_t);
    hdr.args_addr = (uint64_t)arg;

    nlmsg_flags = NLM_F_DUMP;
    if (!genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, id, 0, nlmsg_flags,
        (uint8_t)hdr.command, UMS_GENL_FAMILY_DFX_VERSION)) {
        printf("genlmsg put error\n");
        rc = -1;
        goto free_arg;
    }

    rc = nla_put_u32(msg, UBCORE_HDR_ARGS_LEN, hdr.args_len);
    if (rc < 0) {
        (void)printf("Unable to add args_len: %d\n", rc);
        rc = -1;
        goto free_arg;
    }

    rc = nla_put_u64(msg, UBCORE_HDR_ARGS_ADDR, hdr.args_addr);
    if (rc < 0) {
        (void)printf("Unable to add args_addr: %d\n", rc);
        rc = -1;
        goto free_arg;
    }

    /* Send message */
    rc = nl_send_auto(sk, msg);
    if (rc < 0) {
        printf("nl send auto error\n");
        rc = -1;
        goto free_arg;
    }

    /* Receive reply message, returns number of cb invocations. */
    rc = nl_recvmsgs_default(sk);
    if (rc == -NLE_PERM) {
        printf("Permission error, are you a superuser?\n");
        rc = -1;
        goto free_arg;
    } else if (rc < 0) {
        printf("nl recvmsgs error rc: %d\n", rc);
        rc = -1;
        goto free_arg;
    }
    rc = 0;

free_arg:
    free(arg);
free_nlmsg:
    nlmsg_free(msg);
close_nl:
    nl_close(sk);
free_nl_socket:
    nl_socket_free(sk);
    return rc;
}

static int execute_command(const tool_config_t *cfg)
{
    int ret;

    switch (cfg->cmd) {
        case TOOL_CMD_SHOW:
            ret = admin_show_res(cfg);
            break;
        case TOOL_CMD_NUM:
        default:
            ret = -1;
            break;
    }

    return ret;
}

static inline int ums_admin_check_arg_cnt(int argc)
{
    if (argc != UMS_ADMIN_ARG_CNT) {
        return -1;
    }

    return 0;
}

#define  MAX_CMDLINE_LEN 896   /* must less than MAX_LOG_LEN */
static int admin_check_cmd_len(int argc, char *argv[])
{
    uint32_t len = 0;
    for (int i = 0; i < argc; i++) {
        uint32_t tmp_len = (uint32_t)strnlen(argv[i], MAX_CMDLINE_LEN + 1);
        if (tmp_len == MAX_CMDLINE_LEN + 1) {
            return -1;
        }

        len += tmp_len;
        if (len > (uint32_t)(MAX_CMDLINE_LEN - argc)) {
            return -1;
        }
    }
    return 0;
}

static tool_cmd_type_t parse_command(const char *argv1)
{
    int i;

    tool_cmd_t cmd[] = {
        {"show",                TOOL_CMD_SHOW},
    };

    for (i = 0; i < (int)TOOL_CMD_NUM; i++) {
        if (strlen(argv1) != strlen(cmd[i].cmd)) {
            continue;
        }
        if (strcmp(argv1, cmd[i].cmd) == 0) {
            return cmd[i].type;
        }
    }

    return TOOL_CMD_NUM;
}

static int admin_parse_args(int argc, char *argv[], tool_config_t *cfg)
{
    if (argc == 1 || cfg == NULL) {
        return -1;
    }
    /* First parse the command */
    cfg->cmd = parse_command(argv[1]);

    /* Increase illegal cmd return error */
    if (cfg->cmd == TOOL_CMD_NUM) {
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    int ret;
    tool_config_t tool_cfg = {0};

    if (ums_admin_check_arg_cnt(argc) != 0) {
        (void)printf("user: %s, invalid argument count.\n", getlogin());
        return -1;
    }

    if (admin_check_cmd_len(argc, argv) != 0) {
        (void)printf("user: %s, cmd len out of range.\n", getlogin());
        return -1;
    }

    ret = admin_parse_args(argc, argv, &tool_cfg);
    if (ret != 0) {
        (void)printf("Invalid parameter.\n");
        return ret;
    }
    if (tool_cfg.cmd == TOOL_CMD_NUM) {
        return 0;
    }

    ret = execute_command(&tool_cfg);
    if (ret != 0) {
        (void)printf("Failed to execute command.\n");
        return ret;
    }

    return ret;
}
