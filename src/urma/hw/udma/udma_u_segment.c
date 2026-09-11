// SPDX-License-Identifier: MIT
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <ummu_api.h>
#include "urma_api.h"
#include "udma_u_buf.h"
#include "udma_u_common.h"
#include "udma_u_segment.h"

/*
 * Kernel ABI (udata of URMA_CMD_REGISTER_SEG):
 *   is_gpu_seg  : 0 = host pages (pin_user_pages), 1 = GPU VA
 *   gpu_vendor  : urma_gpu_vendor_t. 0 = unspecified; kernels that only
 *                 understood NVIDIA treat 0+is_gpu_seg as nvidia_p2p.
 *                 1 = NVIDIA, 2 = Muxi/MetaX (沐曦).
 */
struct udma_register_seg_ucmd {
	uint32_t is_gpu_seg;
	uint32_t gpu_vendor;
};

static void udma_u_init_seg_cfg(urma_context_t *urma_ctx, urma_seg_cfg_t *seg_cfg,
				struct udma_u_segment *seg)
{
	seg->urma_tseg.seg.ubva.eid = urma_ctx->eid;
	seg->urma_tseg.seg.ubva.uasid = urma_ctx->uasid;
	seg->urma_tseg.seg.ubva.va = seg_cfg->va;
	seg->urma_tseg.seg.len = seg_cfg->len;
	seg->urma_tseg.seg.attr.value = seg_cfg->flag.value;
	seg->urma_tseg.seg.token_id = seg_cfg->token_id->token_id;
	seg->urma_tseg.urma_ctx = urma_ctx;
	seg->urma_tseg.token_id = seg_cfg->token_id;
	seg->len = seg_cfg->len;
	seg->va = seg_cfg->va;
	seg->is_gpu_seg = (seg_cfg->is_gpu_seg != 0);
	seg->gpu_vendor = seg_cfg->gpu_vendor;
	if (seg_cfg->flag.bs.token_policy != URMA_TOKEN_NONE) {
		seg->token_value.token = seg_cfg->token_value.token;
		seg->token_value_valid = true;
	} else {
		seg->token_value_valid = false;
	}
}

static enum ummu_mapt_perm udma_u_get_seg_perm(urma_seg_attr_t *attr)
{
	bool local_only_flag = attr->bs.access & URMA_ACCESS_LOCAL_ONLY;
	bool atomic_flag = attr->bs.access & URMA_ACCESS_ATOMIC;
	bool write_flag = attr->bs.access & URMA_ACCESS_WRITE;
	bool read_flag = attr->bs.access & URMA_ACCESS_READ;

	/* After setting ACCESS_LOCAL, other operations cannot be configured. */
	if (local_only_flag && !atomic_flag && !write_flag && !read_flag)
		return MAPT_PERM_ATOMIC_RW;

	/* Atomic require additional configuration of write and read. */
	if (!local_only_flag && atomic_flag && write_flag && read_flag)
		return MAPT_PERM_ATOMIC_RW;

	/* Write require additional configuration of read. */
	if (!local_only_flag && !atomic_flag && write_flag && read_flag)
		return MAPT_PERM_RW;

	if (!local_only_flag && !atomic_flag && !write_flag && read_flag)
		return MAPT_PERM_R;

	/* All other configurations are illegal. */
	return (enum ummu_mapt_perm)0;
}

static int udma_u_grant_segment(struct udma_u_segment *seg)
{
	struct udma_u_tid *udma_tid = to_udma_u_tid(seg->urma_tseg.token_id);
	struct ummu_token_info token;
	struct ummu_seg_attr seg_attr = {
		.token = &token,
		.e_bit = (enum ummu_ebit_state )seg->urma_tseg.seg.attr.bs.access & URMA_ACCESS_LOCAL_ONLY,
	};
	enum ummu_mapt_perm perm;
	int ret;

	perm = udma_u_get_seg_perm(&seg->urma_tseg.seg.attr);

	if (seg->token_value_valid) {
		token.input = UDMA_TOKEN_VALUE_INPUT;
		token.tokenVal = seg->token_value.token;
	} else {
		seg_attr.token = NULL;
	}

	ret = ummu_grant(udma_tid->tid, (void *)seg->urma_tseg.seg.ubva.va,
			 seg->urma_tseg.seg.len, perm, &seg_attr);
	token.tokenVal = 0;

	return ret;
}

static int udma_exec_register_seg_cmd(urma_context_t *urma_ctx,
				      urma_seg_cfg_t *seg_cfg,
				      urma_target_seg_t *urma_tseg)
{
	struct udma_register_seg_ucmd ucmd = { 0 };
	urma_cmd_udrv_priv_t udata = {};
	int ret;

	if (!to_udma_u_ctx(urma_ctx)->sva_sep_mode_en && seg_cfg->flag.bs.non_pin)
		return 0;

	if (seg_cfg->is_gpu_seg) {
		uint32_t vendor = seg_cfg->gpu_vendor;

		if (vendor == URMA_GPU_VENDOR_NONE)
			vendor = (uint32_t)urma_detect_gpu_vendor();

		ucmd.is_gpu_seg = 1;
		ucmd.gpu_vendor = vendor;
		udata.in_addr = (uint64_t)(uintptr_t)&ucmd;
		udata.in_len = (uint32_t)sizeof(ucmd);
		UDMA_LOG_INFO("register GPU seg va=0x%lx len=%lu vendor=%u (%s)\n",
			      (unsigned long)seg_cfg->va,
			      (unsigned long)seg_cfg->len, vendor,
			      urma_gpu_vendor_name((urma_gpu_vendor_t)vendor));
		(void)fprintf(stderr,
			      "[udma-seg] GPU register va=0x%lx len=%lu vendor=%s\n",
			      (unsigned long)seg_cfg->va,
			      (unsigned long)seg_cfg->len,
			      urma_gpu_vendor_name((urma_gpu_vendor_t)vendor));
	}

	ret = urma_cmd_register_seg(urma_ctx, urma_tseg, seg_cfg, &udata);
	if (ret)
		UDMA_LOG_ERR("pin segment failed, ret = %d.\n", ret);

	return ret;
}

static int udma_u_check_seg_cfg(const urma_seg_cfg_t *seg_cfg)
{
	if (seg_cfg->flag.bs.access >= UDMA_SEGMENT_ACCESS_GUARD ||
	    !seg_cfg->token_id || seg_cfg->flag.bs.token_policy >= URMA_TOKEN_SIGNED) {
		UDMA_LOG_ERR("invalid segment input, access = %u, "
			     "token_policy = %u, or NULL tid.\n",
			     seg_cfg->flag.bs.access, seg_cfg->flag.bs.token_policy);
		return EINVAL;
	}

	return 0;
}

static void udma_u_ungrant_seg(struct udma_u_segment *seg)
{
	struct udma_u_tid *udma_tid = to_udma_u_tid(seg->urma_tseg.token_id);
	int ret;

	ret = ummu_ungrant(udma_tid->tid, (void *)seg->urma_tseg.seg.ubva.va,
			   seg->urma_tseg.seg.len);
	if (ret)
		UDMA_LOG_ERR("ungrant segment failed, ret = %d.\n", ret);
}

urma_target_seg_t *udma_u_register_seg(urma_context_t *urma_ctx,
				       urma_seg_cfg_t *seg_cfg)
{
	struct udma_u_segment *seg;
	int ret;

	ret = udma_u_check_seg_cfg(seg_cfg);
	if (ret)
		return NULL;

	seg = (struct udma_u_segment *)calloc(1, sizeof(*seg));
	if (!seg) {
		UDMA_LOG_ERR("alloc segment failed.\n");
		return NULL;
	}

	udma_u_init_seg_cfg(urma_ctx, seg_cfg, seg);

	/* Host and GPU segs both need MAPT grant (GPU skip was a test mistake). */
	ret = udma_u_grant_segment(seg);
	if (ret) {
		UDMA_LOG_ERR("segment grant failed, access = %d, ret = %d.\n",
			     seg_cfg->flag.bs.access, ret);
		goto err_ummu_grant;
	}

	ret = udma_exec_register_seg_cmd(urma_ctx, seg_cfg, &seg->urma_tseg);
	if (ret)
		goto err_pin_segment;

	return &seg->urma_tseg;

err_pin_segment:
	udma_u_ungrant_seg(seg);
err_ummu_grant:
	free(seg);
	return NULL;
}

urma_status_t udma_u_unregister_seg(urma_target_seg_t *target_seg)
{
	struct udma_u_context *udma_ctx = to_udma_u_ctx(target_seg->urma_ctx);
	struct udma_u_segment *seg = to_udma_u_seg(target_seg);
	int ret;

	if (!udma_ctx->sva_sep_mode_en && seg->urma_tseg.seg.attr.bs.non_pin)
		goto common_process;

	ret = urma_cmd_unregister_seg(target_seg);
	if (ret) {
		UDMA_LOG_ERR("urma cmd unregister segment failed, ret = %d.\n", ret);
		return URMA_FAIL;
	}

common_process:
	udma_u_ungrant_seg(seg);
	seg->token_value.token = 0;
	free(seg);

	return URMA_SUCCESS;
}

urma_target_seg_t *udma_u_import_seg(urma_context_t *ctx, urma_seg_t *seg,
				     urma_token_t *token, uint64_t addr,
				     urma_import_seg_flag_t flag)
{
	struct udma_u_segment *tseg;

	RTE_SET_USED(addr);
	RTE_SET_USED(flag);
	if (seg->attr.bs.token_policy > URMA_TOKEN_PLAIN_TEXT) {
		UDMA_LOG_ERR("invalid token policy = %d.\n",
			     seg->attr.bs.token_policy);
		return NULL;
	}

	tseg = (struct udma_u_segment *)calloc(1, sizeof(*tseg));
	if (!tseg) {
		UDMA_LOG_ERR("alloc target seg failed.\n");
		return NULL;
	}

	tseg->urma_tseg.urma_ctx = ctx;
	tseg->urma_tseg.seg = *seg;

	if (seg->attr.bs.token_policy != URMA_TOKEN_NONE) {
		tseg->token_value_valid = true;
		tseg->token_value = *token;
	}

	tseg->tid = seg->token_id >> UDMA_TID_SHIFT;

	return &tseg->urma_tseg;
}

urma_status_t udma_u_unimport_seg(urma_target_seg_t *target_seg)
{
	struct udma_u_segment *seg = to_udma_u_seg(target_seg);

	seg->token_value.token = 0;
	free(seg);

	return URMA_SUCCESS;
}
