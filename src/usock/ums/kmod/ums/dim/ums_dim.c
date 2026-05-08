// SPDX-License-Identifier: GPL-2.0
/*
 * UB Memory based Socket(UMS)
 *
 * Description:UMS Dynamic Interrupt Moderation (DIM) implementation
 *
 * Copyright (c) 2022, Alibaba Group.
 * Copyright (c) 2019, Mellanox Technologies inc.  All rights reserved.
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2025. All rights reserved.
 *
 * UMS implementation:
 *     Author(s): ZHANG Weicheng
 */

#include <linux/cpufreq.h>
#include "ums_ns.h"
#include "ums_dim.h"

#define UMS_CPMS_THRESHOLD 1
#define UMS_CPERATIO_THRESHOLD 5
#define UMS_MAX_FLUCTUATIONS 3
#define CPU_IDLE_UTIL_THRESHOLD 5
#define CPU_SOFTIRQ_UTIL_THRESHOLD 10
#define UMS_CPM_DECISION_THRESHOLD 50
#define UMS_NUM_100 100L
#define UMS_DIM_PARAMS_NUM_PROFILES 4
#define UMS_DIM_START_PROFILE 0

/*
 * { timer, 0, counter, 0 }
 * timer means how long we should trigger an interrupt
 * counter means how many packages we should trigger an interrupt
 */
static const struct dim_cq_moder UMS_DIM_PROFILE[UMS_DIM_PARAMS_NUM_PROFILES] = {
	{ 1, 0, 2, 0 },
	{ 4, 0, 8, 0 },
	{ 16, 0, 16, 0 },
	{ 32, 0, 32, 0 },
};

static inline bool ums_is_significant_diff(int val, int ref, int threshold)
{
	int diff_v, abs_v;

	if (ref <= 0)
		return false;

	diff_v = val - ref;
	abs_v = diff_v < 0 ? -diff_v : diff_v;

	return (UMS_NUM_100 * abs_v / ref) >= threshold;
}

static void ums_dim_work(struct work_struct *w)
{
	struct ubcore_jfc_attr jfc_attr;
	struct ums_ubcore_jfc *ums_jfc;
	struct dim *dim;

	dim = container_of(w, struct dim, work);
	ums_jfc = dim->priv;

	jfc_attr.mask = (int)UBCORE_JFC_MODERATE_COUNT | (int)UBCORE_JFC_MODERATE_PERIOD;
	jfc_attr.moderate_count = UMS_DIM_PROFILE[dim->profile_ix].comps;
	jfc_attr.moderate_period = UMS_DIM_PROFILE[dim->profile_ix].usec;

	dim->state = DIM_START_MEASURE;

	ums_jfc->jfc->ub_dev->ops->modify_jfc(ums_jfc->jfc, &jfc_attr, NULL);
}

void ums_dim_init(struct ums_ubcore_jfc *ums_jfc)
{
	struct ums_dim *ums_dim;
	struct dim *dim;

	if (!ums_jfc->jfc->ub_dev->ops->modify_jfc)
		return;

	ums_dim = kzalloc(sizeof(*ums_dim), GFP_KERNEL);
	if (!ums_dim)
		return;

	ums_dim->use_dim = (READ_ONCE(g_ums_sysctl_conf.dim_enable) == 1) ? true : false;
	dim = to_dim(ums_dim);
	dim->state = DIM_START_MEASURE;
	dim->tune_state = DIM_GOING_RIGHT;
	dim->profile_ix = UMS_DIM_START_PROFILE;
	dim->priv = ums_jfc;
	ums_jfc->dim = dim;
	INIT_WORK(&dim->work, ums_dim_work);
}

void ums_dim_destroy(struct ums_ubcore_jfc *ums_jfc)
{
	if (!ums_jfc->dim)
		return;

	(void)cancel_work_sync(&ums_jfc->dim->work);
	kfree(ums_jfc->dim);
	ums_jfc->dim = NULL;
}

static inline void ums_dim_param_clear(struct dim *dim)
{
	dim->steps_right = 0;
	dim->steps_left = 0;
	dim->tired = 0;
	dim->profile_ix = UMS_DIM_START_PROFILE;
	dim->tune_state = DIM_GOING_RIGHT;
}

static inline void ums_dim_reset(struct dim *dim)
{
	int prev_ix = dim->profile_ix;

	ums_dim_param_clear(dim);
	if (prev_ix != dim->profile_ix)
		(void)schedule_work(&dim->work);
	else
		dim->state = DIM_START_MEASURE;
}

static int ums_dim_step(struct dim *dim)
{
	if (dim->tune_state == (int)DIM_GOING_LEFT) {
		if (dim->profile_ix == 0)
			return (int)DIM_ON_EDGE;
		dim->profile_ix--;
		dim->steps_left++;
	}

	if (dim->tune_state == (int)DIM_GOING_RIGHT) {
		if (dim->profile_ix == (UMS_DIM_PARAMS_NUM_PROFILES - 1))
			return (int)DIM_ON_EDGE;
		dim->profile_ix++;
		dim->steps_right++;
	}

	return (int)DIM_STEPPED;
}

static int ums_dim_stats_compare(struct dim_stats *curr, struct dim_stats *prev)
{
	/* first stat */
	if (prev->cpms == 0)
		return (int)DIM_STATS_BETTER;

	if (ums_is_significant_diff(curr->cpms, prev->cpms, UMS_CPMS_THRESHOLD))
		return (curr->cpms > prev->cpms) ? DIM_STATS_BETTER : DIM_STATS_WORSE;

	if (ums_is_significant_diff(curr->cpe_ratio, prev->cpe_ratio, UMS_CPERATIO_THRESHOLD))
		return (curr->cpe_ratio > prev->cpe_ratio) ? DIM_STATS_BETTER : DIM_STATS_WORSE;

	return (int)DIM_STATS_SAME;
}

static void ums_dim_exit_parking(struct dim *dim)
{
	dim->tune_state = dim->profile_ix != 0 ? DIM_GOING_LEFT : DIM_GOING_RIGHT;
	(void)ums_dim_step(dim);
	dim->tired = 0;
}

static inline bool ums_decision(struct dim *dim, const struct dim_stats *curr_stats, int prev_state,
	int prev_ix)
{
	if (prev_state != (int)DIM_PARKING_ON_TOP || dim->tune_state != (int)DIM_PARKING_ON_TOP)
		dim->prev_stats = *curr_stats;

	return dim->profile_ix != prev_ix;
}

static bool ums_dim_decision(struct dim_stats *curr_stats, struct dim *dim)
{
	int prev_state = dim->tune_state;
	int prev_ix = dim->profile_ix;
	int stats_res = ums_dim_stats_compare(curr_stats, &dim->prev_stats);

	if (curr_stats->cpms < UMS_CPM_DECISION_THRESHOLD) {
		ums_dim_param_clear(dim);
		return ums_decision(dim, curr_stats, prev_state, prev_ix);
	}

	switch (dim->tune_state) {
	case DIM_PARKING_ON_TOP:
		if (stats_res != (int)DIM_STATS_SAME) {
			if (dim->tired++ > UMS_MAX_FLUCTUATIONS)
				ums_dim_exit_parking(dim);
		} else {
			dim->tired = 0;
		}
		break;
	case DIM_GOING_RIGHT:
	case DIM_GOING_LEFT:
		if (stats_res != (int)DIM_STATS_BETTER) {
			dim_turn(dim);
		} else if (dim_on_top(dim)) {
			dim_park_on_top(dim);
			break;
		}

		if (ums_dim_step(dim) == (int)DIM_ON_EDGE)
			dim_park_on_top(dim);
		break;
	default:
		break;
	}

	return ums_decision(dim, curr_stats, prev_state, prev_ix);
}

static bool ums_dim_check_utilization(struct dim *dim)
{
	u64 wall, diff_wall, softirq, wall_idle;
	u64 idle_percent, softirq_percent;
	struct kernel_cpustat kcpustat;
	struct ums_dim *ums_dim;
	int cpu;

	ums_dim = to_umsdim(dim);
	cpu = smp_processor_id();
	wall_idle = get_cpu_idle_time((unsigned int)cpu, &wall, 1);
	kcpustat_cpu_fetch(&kcpustat, cpu);

	softirq = div_u64(kcpustat_field(&kcpustat, CPUTIME_SOFTIRQ, cpu), NSEC_PER_USEC);
	diff_wall = wall - ums_dim->prev_wall;
	idle_percent = div64_u64(UMS_NUM_100 * (wall_idle - ums_dim->prev_idle), diff_wall);
	softirq_percent = div64_u64(UMS_NUM_100 * (softirq - ums_dim->prev_softirq), diff_wall);

	ums_dim->prev_softirq = softirq;
	ums_dim->prev_idle = wall_idle;
	ums_dim->prev_wall = wall;

	return idle_percent < CPU_IDLE_UTIL_THRESHOLD && softirq_percent >= CPU_SOFTIRQ_UTIL_THRESHOLD;
}

void ums_dim(struct dim *dim, u64 completions)
{
	struct dim_sample *curr_sample;
	struct dim_stats curr_stats;
	struct ums_dim *ums_dim;
	int nevents;

	ums_dim = to_umsdim(dim);
	if (!ums_dim->use_dim)
		return;

	curr_sample = &dim->measuring_sample;
	dim_update_sample_with_comps(
		curr_sample->event_ctr + 1, 0, 0, curr_sample->comp_ctr + completions, &dim->measuring_sample);

	switch (dim->state) {
	case DIM_MEASURE_IN_PROGRESS:
		nevents = curr_sample->event_ctr - dim->start_sample.event_ctr;
		if (nevents < DIM_NEVENTS)
			break;
		if (!ums_dim_check_utilization(dim)) {
			ums_dim_reset(dim);
			break;
		}
		dim_calc_stats(&dim->start_sample, curr_sample, &curr_stats);
		if (ums_dim_decision(&curr_stats, dim)) {
			dim->state = DIM_APPLY_NEW_PROFILE;
			(void)schedule_work(&dim->work);
			break;
		}
		fallthrough;
	case DIM_START_MEASURE:
		dim->state = DIM_MEASURE_IN_PROGRESS;
		dim_update_sample_with_comps(curr_sample->event_ctr, 0, 0,
					curr_sample->comp_ctr, &dim->start_sample);
		break;
	case DIM_APPLY_NEW_PROFILE:
		break;
	default:
		break;
	}
}

#ifdef UMS_UT_TEST
EXPORT_SYMBOL(ums_dim);
#endif
