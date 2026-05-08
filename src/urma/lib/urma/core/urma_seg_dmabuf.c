/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2026. All rights reserved.
 *
 * 文件: urma_seg_dmabuf.c
 *
 * 这是什么 (What):
 *   liburma 公共层中, 与 urma_register_seg / urma_unregister_seg 平级的 GDR
 *   (GPUDirect-RDMA 等价) 入口实现. 提供两个新对外 API:
 *
 *      urma_register_seg_dmabuf(ctx, seg_cfg, fd, offset)
 *      urma_query_dmabuf_caps(dev, &caps)
 *
 *   声明位于 include/urma_api.h.
 *
 * 怎么实现 (How, 给内核老兵):
 *   这一层是"控制面 thin wrapper", 类似你在内核里写的 vfs 层 -> fs ops:
 *
 *     1. 校验参数 (NULL, fd >= 0, va/len/access);
 *     2. 取设备 ops 表 ctx->ops;
 *     3. 如果 ops->register_seg_dmabuf 已实现 -> 调它 (具体 hw provider 比如
 *        udma 真正跟内核 ubcore 走 IOCTL, 由 ubcore 内核侧 dma_buf_get +
 *        dma_buf_attach + dma_buf_map_attachment 拿 sg_table 写 NIC MAPT);
 *     4. 否则返回 NULL + errno=ENOTSUP, 调用方 (perftest) 退化到 host-proxy.
 *
 *   urma_query_dmabuf_caps 同理: 转给 ops->query_dmabuf_caps, 该 op 会读取
 *   设备能力位 (urma_device_attr_t.dev_cap.feature.bs.dmabuf, 见
 *   docs/urma_gdr_analysis.md 5.2 第 16 项).
 *
 * 当前阶段 (Current state):
 *   - register_seg_dmabuf / query_dmabuf_caps 这两个 ops 字段的"实际加进
 *     urma_ops 结构"是 ABI 改动, 必须在内核 + liburma + 所有 hw provider
 *     之间一起改. 为了让这次提交"能编过, 假装运行成功", 这里的实现采用
 *     "comment-only ABI"风格:
 *
 *        - 不实际访问 ctx->ops->register_seg_dmabuf (该字段尚不存在);
 *        - 仅 fprintf 一行说明被调用 + 参数, 然后:
 *            register_seg_dmabuf  --> 返回 NULL  (errno = ENOTSUP)
 *            query_dmabuf_caps    --> 填 caps = {0,0,0,0} 后返回 URMA_SUCCESS
 *        - 用户代码 (perftest) 看到 NULL 即可触发 host-proxy fallback.
 *
 *   - 等 5.1 ABI patch (urma_ops 加字段) 落地后, 把下面的 #if 0 区块解开就
 *     是真正实现.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "urma_api.h"
#include "urma_log.h"
#include "urma_private.h"
#include "urma_provider.h"
#include "urma_types.h"

/*
 * 这里没有 ENOTSUP 在 urma_opcode.h, 所以本地映射成 EOPNOTSUPP (errno).
 */
#ifndef URMA_DMABUF_LOG
#define URMA_DMABUF_LOG(fmt, ...) \
    fprintf(stderr, "[urma-dmabuf] " fmt "\n", ##__VA_ARGS__)
#endif

urma_target_seg_t *urma_register_seg_dmabuf(urma_context_t *ctx,
                                            urma_seg_cfg_t *seg_cfg,
                                            int dmabuf_fd,
                                            uint64_t offset)
{
    /*
     * 参数校验 (kernel 侧也会再校一次, 但用户态先挡一道是惯例):
     *   - ctx / seg_cfg 都不能为空;
     *   - dmabuf_fd 必须 >= 0 (-1 是 host 路径的 sentinel, 那种情况调用方
     *     应当走 urma_register_seg() 而不是这里);
     *   - len 不能为 0;
     *   - flag.bs.non_pin 与 dma-buf pinned 语义冲突, 必须 0.
     */
    if (ctx == NULL || seg_cfg == NULL || dmabuf_fd < 0 || seg_cfg->len == 0) {
        URMA_DMABUF_LOG("register_seg_dmabuf: invalid arg (ctx=%p cfg=%p fd=%d len=%lu)",
                        (void *)ctx, (void *)seg_cfg, dmabuf_fd,
                        seg_cfg ? (unsigned long)seg_cfg->len : 0UL);
        errno = EINVAL;
        return NULL;
    }
    if (seg_cfg->flag.bs.non_pin) {
        URMA_DMABUF_LOG("register_seg_dmabuf: flag.bs.non_pin must be 0 for dmabuf");
        errno = EINVAL;
        return NULL;
    }

    /*
     * 这里就是真实现要分发到 hw provider 的地方:
     *
     *     urma_ops_t *ops = ctx->ops;
     *     if (ops == NULL || ops->register_seg_dmabuf == NULL) {
     *         errno = EOPNOTSUPP;
     *         return NULL;
     *     }
     *     return ops->register_seg_dmabuf(ctx, seg_cfg, dmabuf_fd, offset);
     *
     * urma_ops 加新字段属于 ABI 改动, 该字段尚未真正加入结构, 因此本骨架
     * 只是打印一行并以 ENOTSUP 返回, 让 perftest 知道驱动还不支持 dma-buf,
     * 自动切到 host-proxy 模式继续跑.
     */
    URMA_DMABUF_LOG("register_seg_dmabuf: stub -- driver dma-buf path not yet wired "
                    "(va=0x%lx len=%lu fd=%d off=%lu) -> ENOTSUP",
                    (unsigned long)seg_cfg->va, (unsigned long)seg_cfg->len,
                    dmabuf_fd, (unsigned long)offset);
    errno = EOPNOTSUPP;
    return NULL;
}

urma_status_t urma_query_dmabuf_caps(urma_device_t *dev,
                                     urma_dmabuf_caps_t *caps)
{
    if (dev == NULL || caps == NULL) {
        URMA_DMABUF_LOG("query_dmabuf_caps: invalid arg (dev=%p caps=%p)",
                        (void *)dev, (void *)caps);
        return URMA_EINVAL;
    }

    /*
     * 真实现会读取 urma_device_attr_t 中"是否支持 dmabuf"的 feature 位,
     * 它由 hw provider 在 query_device 时填.
     *
     *   urma_device_attr_t attr;
     *   if (urma_query_device(dev, &attr) != URMA_SUCCESS) return URMA_FAIL;
     *   caps->support_dmabuf       = attr.dev_cap.feature.bs.dmabuf;
     *   caps->support_dynamic      = attr.dev_cap.feature.bs.dmabuf_dynamic;
     *   caps->max_seg_size         = attr.dev_cap.max_seg_size;
     *   caps->supported_alignments = (1u<<12) | (1u<<21);
     *
     * 当前阶段 feature.bs.dmabuf 字段尚未加入 ABI, 直接报告"不支持",
     * perftest 看到这个就会走 host-proxy.
     */
    memset(caps, 0, sizeof(*caps));
    caps->support_dmabuf       = 0;
    caps->support_dynamic      = 0;
    caps->max_seg_size         = 0;
    caps->supported_alignments = 0;

    URMA_DMABUF_LOG("query_dmabuf_caps: stub -> support_dmabuf=0 (driver feature bit not wired)");
    return URMA_SUCCESS;
}
