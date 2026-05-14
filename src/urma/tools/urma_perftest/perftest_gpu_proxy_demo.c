/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2026. All rights reserved.
 *
 * 文件: perftest_gpu_proxy_demo.c
 *
 * 这是什么 (What):
 *   一个 *最小可独立编译运行* 的 demo, 用来端到端演示
 *   perftest_mem_gpu_proxy.c 提供的"GPU 内存 + 无 GDR 驱动 + 不依赖内核"
 *   后端是否真正工作.
 *
 *   它模拟一台机器上 perftest send 与 perftest recv 两侧, 但去掉了 URMA /
 *   网络 / 内核 / GPU SDK 这些重依赖, 用普通 memcpy 代替"NIC DMA 与
 *   cuMemcpy", 让你只关心:
 *
 *      stage_to_nic_buffer   --   把"GPU 数据"拉到 host shadow
 *      [NIC DMA shadow]      --   demo 里用 memcpy 模拟两个 shadow 之间复制
 *      stage_from_nic_buffer --   把 host shadow 灌回"GPU"
 *
 *   完整的"无 GDR 跑 GPU 内存"的数据通路一目了然.
 *
 * 怎么用 (How):
 *   见同目录 Makefile.demo:
 *     $ make -f Makefile.demo
 *     $ ./perftest_gpu_proxy_demo
 *
 *   一切只依赖 libc, 不需要 cmake / urma 头 / kernel 头.
 *
 * 给内核老兵的提示:
 *   - 把 send 侧的 "GPU buffer" 想成内核里的 device buffer;
 *   - 两侧 shadow 之间的 memcpy 等价于"网络一跳 + NIC DMA-engine 两次";
 *   - 真实部署里 shadow_send -> shadow_recv 这一步由 URMA write_bw 完成,
 *     在这里压缩成 memcpy 是为了演示业务逻辑而非性能.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "perftest_memory.h"

#define DEMO_SIZE   (4096u)   /* 4KB, 一个普通 page, 足够看清楚 */
#define DEMO_ALIGN  (4096u)
#define DEMO_PATTERN_FROM_SEND 0xC3 /* sender "GPU" 上初始数据 */

static int run_demo(void)
{
    perftest_memory_ops_t *send_ops = NULL;
    perftest_memory_ops_t *recv_ops = NULL;
    void *send_shadow = NULL, *recv_shadow = NULL;
    void *send_gpu, *recv_gpu;
    int send_fd = 0, recv_fd = 0;
    uint64_t send_off = 0, recv_off = 0;
    int rc = -1;

    /* -------- step 1: 在 send 侧创建后端 + 协商 + 分配 -------- */
    fprintf(stderr, "\n==[ %s: SEND side setup ]==\n", __func__);
    send_ops = perftest_gpu_proxy_create(NULL);
    if (send_ops == NULL) {
        fprintf(stderr, "%s: send create failed\n", __func__);
        goto out;
    }
    if (send_ops->init(send_ops, NULL) != 0) {
        fprintf(stderr, "%s: send init failed\n", __func__);
        goto out;
    }
    if (send_ops->negotiate_gdr_mode(send_ops, NULL) != 0) {
        fprintf(stderr, "%s: send negotiate failed\n", __func__);
        goto out;
    }
    if (send_ops->alloc_buffer(send_ops, DEMO_SIZE, DEMO_ALIGN,
                               &send_shadow, &send_fd, &send_off) != 0) {
        fprintf(stderr, "%s: send alloc_buffer failed\n", __func__);
        goto out;
    }
    if (send_fd != -1) {
        fprintf(stderr, "%s: BUG send_fd=%d, host-proxy must report -1\n",
                __func__, send_fd);
        goto out;
    }
    send_gpu = perftest_gpu_proxy_get_gpu_ptr(send_ops);
    fprintf(stderr, "%s: send shadow=%p gpu=%p size=%zu\n",
            __func__, send_shadow, send_gpu, perftest_gpu_proxy_get_size(send_ops));

    /* 模拟应用层在 GPU 上准备数据 (memset 模拟 cuMemsetD8). */
    memset(send_gpu, DEMO_PATTERN_FROM_SEND, DEMO_SIZE);
    fprintf(stderr, "%s: send: filled GPU buffer with pattern 0x%02X\n",
            __func__, DEMO_PATTERN_FROM_SEND);

    /* -------- step 2: 在 recv 侧创建后端 + 协商 + 分配 -------- */
    fprintf(stderr, "\n==[ %s: RECV side setup ]==\n", __func__);
    recv_ops = perftest_gpu_proxy_create(NULL);
    if (recv_ops == NULL ||
        recv_ops->init(recv_ops, NULL) != 0 ||
        recv_ops->negotiate_gdr_mode(recv_ops, NULL) != 0 ||
        recv_ops->alloc_buffer(recv_ops, DEMO_SIZE, DEMO_ALIGN,
                               &recv_shadow, &recv_fd, &recv_off) != 0) {
        fprintf(stderr, "%s: recv setup failed\n", __func__);
        goto out;
    }
    recv_gpu = perftest_gpu_proxy_get_gpu_ptr(recv_ops);
    fprintf(stderr, "%s: recv shadow=%p gpu=%p size=%zu\n",
            __func__, recv_shadow, recv_gpu, perftest_gpu_proxy_get_size(recv_ops));

    /* -------- step 3: 走完整 IO 通路 --------
     *
     * 真 urma_perftest 主循环里这一段是:
     *
     *     stage_to_nic_buffer(send);   // GPU -> shadow
     *     urma_post_jfs_wr(...);       // shadow -> NIC -> network -> NIC -> shadow
     *     poll_jfc(...);
     *     stage_from_nic_buffer(recv); // shadow -> GPU
     *
     * 这里我们把 URMA 那一跳压缩成 memcpy.
     */
    fprintf(stderr, "\n==[ %s: IO path ]==\n", __func__);

    if (send_ops->stage_to_nic_buffer(send_ops, send_shadow, send_gpu, DEMO_SIZE) != 0) {
        fprintf(stderr, "%s: stage_to_nic failed\n", __func__);
        goto out;
    }

    /* 模拟"NIC 把 send_shadow 通过网络送到 recv_shadow". */
    fprintf(stderr, "%s: simulating NIC DMA: send_shadow -> recv_shadow (%u bytes)\n",
            __func__, DEMO_SIZE);
    memcpy(recv_shadow, send_shadow, DEMO_SIZE);

    if (recv_ops->stage_from_nic_buffer(recv_ops, recv_gpu, recv_shadow, DEMO_SIZE) != 0) {
        fprintf(stderr, "%s: stage_from_nic failed\n", __func__);
        goto out;
    }

    /* -------- step 4: 校验 recv 侧 GPU 缓冲拿到了 sender pattern -------- */
    fprintf(stderr, "\n==[ %s: verify ]==\n", __func__);
    {
        unsigned char *p = (unsigned char *)recv_gpu;
        size_t i, bad = 0;
        for (i = 0; i < DEMO_SIZE; i++) {
            if (p[i] != DEMO_PATTERN_FROM_SEND) {
                bad++;
            }
        }
        if (bad == 0) {
            fprintf(stderr, "%s: PASS -- recv GPU buffer matches send pattern 0x%02X "
                    "across all %u bytes\n", __func__, DEMO_PATTERN_FROM_SEND, DEMO_SIZE);
            rc = 0;
        } else {
            fprintf(stderr, "%s: FAIL -- %zu / %u bytes mismatched\n",
                    __func__, bad, DEMO_SIZE);
        }
    }

out:
    /* -------- step 5: 清理 -------- */
    fprintf(stderr, "\n==[ %s: teardown ]==\n", __func__);
    if (send_ops) {
        if (send_shadow) send_ops->free_buffer(send_ops, send_shadow, send_fd, DEMO_SIZE);
        send_ops->destroy(send_ops);
        free(send_ops->priv);
        free(send_ops);
    }
    if (recv_ops) {
        if (recv_shadow) recv_ops->free_buffer(recv_ops, recv_shadow, recv_fd, DEMO_SIZE);
        recv_ops->destroy(recv_ops);
        free(recv_ops->priv);
        free(recv_ops);
    }
    return rc;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    fprintf(stderr,
            "%s: urma_perftest GPU host-proxy minimal demo "
            "(no CUDA, no URMA, no kernel)\n", __func__);
    return run_demo() == 0 ? 0 : 1;
}
