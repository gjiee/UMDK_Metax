// SPDX-License-Identifier: GPL-2.0
/*
 * UB Memory based Socket(UMS)
 *
 * Description:UMS DFX support functions implementation
 *
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2025. All rights reserved.
 *
 * UMS implementation:
 *     Author: LI Yuxing
 */

#include <net/net_namespace.h>
#include <net/netns/generic.h>

#include <net/sock.h>
#include <net/tcp.h>

#include <linux/version.h>
#include <linux/init.h>
#include <linux/sysctl.h>

#ifndef KERNEL_VERSION_4
#include "ums_dim.h"
#endif
#include "ums_ubcore.h"
#include "ums_core.h"
#include "ums_ns.h"
#include "ums_dfx.h"

#define PADNUM 150
#define IPV4_CHAR_SIZE 20
#define IPV6_CHAR_SIZE 40
#define IPV4_MOVE_STEP1 24
#define IPV4_MOVE_STEP2 16
#define IPV4_MOVE_STEP3 8
#define IPV6_MOVE_STEP 16
#define SOCKET_STATE_MAX_SIZE 30

static u32 g_snd_min = UMS_BUF_MIN_SIZE;
static u32 g_snd_max = (UINT32_MAX >> 1);
static u32 g_autocork_min = 1;
static u32 g_autocork_max = (UINT32_MAX >> 2); /* half of g_snd_max */
static u32 g_rcv_min = UMS_BUF_MIN_SIZE;
static u32 g_rcv_max = (UINT32_MAX >> 1);
static u32 g_dim_enable_min = 0;
static u32 g_dim_enable_max = 1;

static char *g_ums_fallback_state[] = {"False", "True"};
static char *g_ums_socket_state[] = {
	"Invalid", "UMS_ACTIVE", "UMS_INIT", "Invalid", "Invalid",
	"Invalid", "Invalid", "UMS_CLOSED", "Invalid", "Invalid",
	"UMS_LISTEN", "Invalid", "Invalid", "Invalid", "Invalid",
	"Invalid", "Invalid", "Invalid", "Invalid", "Invalid",
	"UMS_PEERCLOSEWAIT1", "UMS_PEERCLOSEWAIT2", "UMS_APPCLOSEWAIT1", "UMS_APPCLOSEWAIT2",
	"UMS_APPFINCLOSEWAIT",
	"UMS_PEERFINCLOSEWAIT", "UMS_PEERABORTWAIT", "UMS_PROCESSABORT", "Invalid", "Invalid"
};

/* proc file system support */
static char *ums_convert_ip(u32 ip)
{
	char *res = kzalloc(IPV4_CHAR_SIZE, GFP_KERNEL);
	if (!res) {
		UMS_LOGE("Fail to alloc IP res mem");
		return NULL;
	}

	if (snprintf(res, IPV4_CHAR_SIZE, "%u.%u.%u.%u",
		(ip >> IPV4_MOVE_STEP1), ((ip >> IPV4_MOVE_STEP2) & 0X00FF),
		((ip >> IPV4_MOVE_STEP3) & 0x0000FF), (ip & 0x000000FF)) < 0) {
		UMS_LOGE("sprintf failed");
		kfree(res);
		return NULL;
	}

	return res;
}

static char *ums_convert_ipv6(struct in6_addr ip6)
{
	char *res = kzalloc(IPV6_CHAR_SIZE, GFP_KERNEL);
	if (!res) {
		UMS_LOGE("Fail to alloc IP res mem");
		return NULL;
	}

	if (snprintf(res, IPV6_CHAR_SIZE, "[%X:%X:%X:%X:%X:%X:%X:%X]",
		((ntohl(ip6.s6_addr32[0]) >> IPV6_MOVE_STEP) & 0XFFFF),
		(ntohl(ip6.s6_addr32[0]) & 0X0000FFFF),
		((ntohl(ip6.s6_addr32[1]) >> IPV6_MOVE_STEP) & 0XFFFF),
		(ntohl(ip6.s6_addr32[1]) & 0X0000FFFF),
		((ntohl(ip6.s6_addr32[2]) >> IPV6_MOVE_STEP) & 0XFFFF),
		(ntohl(ip6.s6_addr32[2]) & 0X0000FFFF),
		((ntohl(ip6.s6_addr32[3]) >> IPV6_MOVE_STEP) & 0XFFFF),
		(ntohl(ip6.s6_addr32[3]) & 0X0000FFFF)) < 0) {
		UMS_LOGE("snprintf failed");
		kfree(res);
		return NULL;
	}

	return res;
}

static void format_eid(const uint8_t eid[16], char *buf, size_t buf_size)
{
	if (snprintf(buf, buf_size,
		"%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:"
		"%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x",
		eid[0], eid[1], eid[2], eid[3],
		eid[4], eid[5], eid[6], eid[7],
		eid[8], eid[9], eid[10], eid[11],
		eid[12], eid[13], eid[14], eid[15]) < 0)
		UMS_LOGE("snprintf failed");
}

static void show_ums_jetty_info(struct seq_file *f, struct ums_connection *conn)
{
	char l_jetty_id_eid[UMS_PRINT_EID_STRING_SIZE];
	char r_jetty_id_eid[UMS_PRINT_EID_STRING_SIZE];

	if (conn == NULL)
		return;

	if (conn->jetty_info.is_ums_conn) {
		format_eid(conn->jetty_info.l_jetty_id.eid.raw, l_jetty_id_eid, UMS_PRINT_EID_STRING_SIZE);
		format_eid(conn->jetty_info.r_jetty_id.eid.raw, r_jetty_id_eid, UMS_PRINT_EID_STRING_SIZE);
		seq_printf(f, "     %s, %8u     %s, %8u", l_jetty_id_eid, conn->jetty_info.l_jetty_id.id,
		r_jetty_id_eid, conn->jetty_info.r_jetty_id.id);
	} else {
		seq_printf(f, "%44s, %8s %43s, %8s", "N/A", "N/A", "N/A", "N/A");
	}
}

static void show_ums4_info(struct sock *sk, struct seq_file *f, struct ums_iter_state *st)
{
	struct ums_sock *ums = ums_sk(sk);
	bool need_free_dest = true;
	bool need_free_src = true;
	char *state = NULL;
	char *dest = NULL;
	char *src = NULL;
	u16 destp = 0;
	u16 srcp = 0;

	if ((st == NULL) || (ums == NULL) || (f == NULL) || (ums->sk.sk_state >= SOCKET_STATE_MAX_SIZE))
		return;

	state = g_ums_socket_state[ums->sk.sk_state];

	if ((ums->clcsock != NULL) && (ums->clcsock->sk != NULL)) {
		src = ums_convert_ip(ntohl(ums->clcsock->sk->sk_rcv_saddr));
		dest = ums_convert_ip(ntohl(ums->clcsock->sk->sk_daddr));
		srcp = ums->clcsock->sk->sk_num;
		destp = ntohs(ums->clcsock->sk->sk_dport);
	}
	if (src == NULL) {
		need_free_src = false;
		src = "--";
	}
	if (dest == NULL) {
		need_free_dest = false;
		dest = "--";
	}

	seq_printf(f, "%4d:   %15s:%-5d %15s:%-5d %21s %10s",
		st->offset, src, srcp, dest, destp, state,
		g_ums_fallback_state[(u8)(ums->use_fallback)]);

	show_ums_jetty_info(f,  &ums->conn);

	st->offset++;

	if (need_free_src)
		kfree(src);
	if (need_free_dest)
		kfree(dest);
}

int ums4_seq_show(struct seq_file *seq, void *v)
{
	struct ums_iter_state *st = seq->private;
	struct sock *sk = (struct sock *)v;

	seq_setwidth(seq, PADNUM - 1);
	if (v == SEQ_START_TOKEN) {
		/* output the header to seq file. Pay attention to the format align */
		seq_puts(seq, "Index            SRC_IP:Port          DEST_IP:Port\
	            State   Fallback                                     SRC_EID, JETTY_ID\
                                    DEST_EID, JETTY_ID");
		goto out;
	}

	if (sk)
		show_ums4_info(sk, seq, st);
	else
		UMS_LOGW("Try to show a IPV4 sock that is NULL. Return.");
out:
	seq_pad(seq, '\n');
	return 0;
}

static void show_ums6_info(struct sock *sk, struct seq_file *f, struct ums_iter_state *st)
{
	struct ums_sock *ums = ums_sk(sk);
	bool need_free_dest6 = true;
	bool need_free_src6 = true;
	char *state6 = NULL;
	char *dest6 = NULL;
	char *src6 = NULL;
	u16 destp6 = 0;
	u16 srcp6 = 0;

	if ((st == NULL) || (ums == NULL) || (f == NULL) || (ums->sk.sk_state >= SOCKET_STATE_MAX_SIZE))
		return;

	state6 = g_ums_socket_state[ums->sk.sk_state];

	if ((ums->clcsock != NULL) && (ums->clcsock->sk != NULL)) {
		src6 = ums_convert_ipv6(ums->clcsock->sk->sk_v6_rcv_saddr);
		dest6 = ums_convert_ipv6(ums->clcsock->sk->sk_v6_daddr);
		srcp6 = ums->clcsock->sk->sk_num;
		destp6 = ntohs(ums->clcsock->sk->sk_dport);
	}
	if (src6 == NULL) {
		need_free_src6 = false;
		src6 = "--";
	}
	if (dest6 == NULL) {
		need_free_dest6 = false;
		dest6 = "--";
	}

	seq_printf(f, "%4d:   %41s:%-5d %41s:%-5d %21s %10s",
		st->offset, src6, srcp6, dest6, destp6, state6,
		g_ums_fallback_state[(u8)(ums->use_fallback)]);

	show_ums_jetty_info(f,  &ums->conn);

	st->offset++;

	if (need_free_src6)
		kfree(src6);
	if (need_free_dest6)
		kfree(dest6);
}

int ums6_seq_show(struct seq_file *seq, void *v)
{
	struct ums_iter_state *st = seq->private;
	struct sock *sk = (struct sock *)v;

	seq_setwidth(seq, PADNUM - 1);
	if (v == SEQ_START_TOKEN) {
		/* output the header to seq file. Pay attention to the format align */
		seq_puts(seq, "Index                                      SRC_IP:Port\
                                    DEST_IP:Port	                State   Fallback\
                                     SRC_EID, JETTY_ID                          \
          DEST_EID, JETTY_ID");
		goto out;
	}

	if (sk)
		show_ums6_info(sk, seq, st);
	else
		UMS_LOGW("Try to show a IPV6 sock that is NULL. Return.");
out:
	seq_pad(seq, '\n');
	return 0;
}

static inline struct ums_hashinfo *ums_get_hinfo(struct seq_file *seq)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
	return pde_data(file_inode(seq->file));
#else
	return PDE_DATA(file_inode(seq->file));
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0) */
}

static void *ums_get_sk(struct seq_file *seq, loff_t pos)
{
	struct ums_hashinfo *hinfo = ums_get_hinfo(seq);
	struct hlist_head *head = &hinfo->ht;
	loff_t cur = pos;
	struct sock *sk;

	sk_for_each(sk, head) {
		if (sock_net(sk) == seq_file_net(seq)) {
			if (cur == 0)
				return sk;
			--cur;
		}
	}

	return NULL;
}

void *ums_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(&hinfo->lock)
{
	struct ums_hashinfo *hinfo = ums_get_hinfo(seq);
	struct ums_iter_state *st = seq->private;

	read_lock(&hinfo->lock);

	if (*pos == 0) {
		st->offset = 0;
		return SEQ_START_TOKEN;
	}

	st->offset--;

	return ums_get_sk(seq, (*pos - 1));
}

void *ums_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	loff_t nxt = *pos;

	++*pos;

	return ums_get_sk(seq, nxt);
}

void ums_seq_stop(struct seq_file *seq, void *v)
	__releases(&hinfo->lock)
{
	struct ums_hashinfo *hinfo = ums_get_hinfo(seq);
 
	read_unlock(&hinfo->lock);
}

/* sysctl support */
static struct ctl_table g_ums_table[] = {
	{
		.procname     = "autocorking_size",
		.data         = &g_ums_sysctl_conf.sysctl_autocorking_size,
		.maxlen       = sizeof(unsigned int),
		.mode         = 0644,
		.proc_handler = proc_dointvec_minmax,
		.extra1       = &g_autocork_min,
		.extra2       = &g_autocork_max,
	},
	{
		.procname	  = "snd_buf",
		.data		  = &g_ums_sysctl_conf.sysctl_sndbuf,
		.maxlen		  = sizeof(int),
		.mode		  = 0644,
		.proc_handler = proc_dointvec_minmax,
		.extra1		  = &g_snd_min,
		.extra2		  = &g_snd_max,
	},
	{
		.procname	  = "rcv_buf",
		.data		  = &g_ums_sysctl_conf.sysctl_rcvbuf,
		.maxlen		  = sizeof(int),
		.mode		  = 0644,
		.proc_handler = proc_dointvec_minmax,
		.extra1		  = &g_rcv_min,
		.extra2		  = &g_rcv_max,
	},
	{
		.procname	  = "dim_enable",
		.data		  = &g_ums_sysctl_conf.dim_enable,
		.maxlen		  = sizeof(int),
		.mode		  = 0644,
		.proc_handler = proc_dointvec_minmax,
		.extra1		  = &g_dim_enable_min,
		.extra2		  = &g_dim_enable_max,
	},
	{  }
};

int __net_init ums_sysctl_net_init(struct net *net)
{
	if (net != &init_net) {
		UMS_LOGW_ONCE("UMS not support muti-net.");
		return 0;
	}

	g_ums_sysctl_conf.ums_hdr = register_net_sysctl(net, "net/ums", g_ums_table);
	if (!g_ums_sysctl_conf.ums_hdr)
		return -ENOMEM;

	WRITE_ONCE(g_ums_sysctl_conf.sysctl_autocorking_size, UMS_AUTOCORKING_DEFAULT_SIZE);
	WRITE_ONCE(g_ums_sysctl_conf.sysctl_sndbuf, UMS_SNDBUF_DEFAULT_SIZE);
	WRITE_ONCE(g_ums_sysctl_conf.sysctl_rcvbuf, UMS_RCVBUF_DEFAULT_SIZE);
	WRITE_ONCE(g_ums_sysctl_conf.dim_enable, 0);

	return 0;
}

void __net_exit ums_sysctl_net_exit(const struct net *net)
{
	struct ctl_table *sysctl_table;
	if (net != &init_net)
		return;

	sysctl_table = g_ums_sysctl_conf.ums_hdr->ctl_table_arg;
	unregister_net_sysctl_table(g_ums_sysctl_conf.ums_hdr);
}