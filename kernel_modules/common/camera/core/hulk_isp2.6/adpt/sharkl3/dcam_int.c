/*
 * Copyright (C) 2021-2022 UNISOC Communications Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include "isp_hw.h"
#include "sprd_img.h"
#include <sprd_mm.h>

#include "dcam_reg.h"
#include "dcam_int.h"
#include "dcam_path.h"
#include "cam_queue.h"
#include "cam_types.h"

/* Macro Definitions */
#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "DCAM_INT: %d %d %s : "\
	fmt, current->pid, __LINE__, __func__

/*
 * track interrupt count for debug use
 */
//#define DCAM_INT_RECORD 1
#ifdef DCAM_INT_RECORD
#define INT_RCD_SIZE 0x10000
static uint32_t dcam_int_recorder[DCAM_HW_CONTEXT_MAX][DCAM_IRQ_NUMBER][INT_RCD_SIZE];
static uint32_t int_index[DCAM_HW_CONTEXT_MAX][DCAM_IRQ_NUMBER];
#endif

static uint32_t dcam_int_tracker[DCAM_HW_CONTEXT_MAX][DCAM_IRQ_NUMBER];
static char *dcam_dev_name[] = {"DCAM0",
				"DCAM1",
				"DCAM2"
				};

static inline void dcamint_dcam_int_record(uint32_t idx, uint32_t status)
{
	uint32_t i;

	for (i = 0; i < DCAM_IRQ_NUMBER; i++) {
		if (status & BIT(i))
			dcam_int_tracker[idx][i]++;
	}

#ifdef DCAM_INT_RECORD
	{
		uint32_t cnt, time, int_no;
		timespec cur_ts;

		ktime_get_ts(&cur_ts);
		time = (uint32_t)(cur_ts.tv_sec & 0xffff);
		time <<= 16;
		time |= (uint32_t)((cur_ts.tv_nsec / (NSEC_PER_USEC * 100)) & 0xffff);
		for (int_no = 0; int_no < DCAM_IRQ_NUMBER; int_no++) {
			if (status & BIT(int_no)) {
				cnt = int_index[idx][int_no];
				dcam_int_recorder[idx][int_no][cnt] = time;
				cnt++;
				int_index[idx][int_no] = (cnt & (INT_RCD_SIZE - 1));
			}
		}
	}
#endif
}

/*
 * Dequeue a frame from result queue.
 */
static struct camera_frame *dcamint_frame_prepare(struct dcam_hw_context *dcam_hw_ctx, struct dcam_sw_context *sw_ctx,
				enum dcam_path_id path_id)
{
	struct dcam_path_desc *path = NULL;
	struct camera_frame *frame = NULL;
	timespec *ts = NULL;
	uint32_t dev_fid;

	if (unlikely(!dcam_hw_ctx || !is_path_id(path_id)) || !sw_ctx)
		return NULL;

	path = &sw_ctx->path[path_id];
	if (atomic_read(&path->set_frm_cnt) <= 1) {
		pr_warn_ratelimited("warning: DCAM%u %s cnt %d, deci %u, out %u, result %u\n",
			dcam_hw_ctx->hw_ctx_id, dcam_path_name_get(path_id),
			atomic_read(&path->set_frm_cnt), path->frm_deci,
			cam_queue_cnt_get(&path->out_buf_queue),
			cam_queue_cnt_get(&path->result_queue));
		return NULL;
	}

	if (atomic_read(&sw_ctx->state) == STATE_RUNNING) {
		frame = cam_queue_dequeue(&path->result_queue, struct camera_frame, list);
		if (!frame) {
			pr_err("fail to get buf,DCAM%u %s output buffer unavailable\n",
				dcam_hw_ctx->hw_ctx_id, dcam_path_name_get(path_id));
			return NULL;
		}
	} else {
		pr_warn("warning: DCAM%u state %d %s\n", dcam_hw_ctx->hw_ctx_id,
			atomic_read(&sw_ctx->state), dcam_path_name_get(path_id));
		return NULL;
	}

	atomic_dec(&path->set_frm_cnt);
	if (unlikely(frame->is_reserved)) {
		if (path_id != DCAM_PATH_3DNR)
			pr_warn("warning: DCAM%u %s use reserved buffer, out %u, result %u, fid %d\n",
				dcam_hw_ctx->hw_ctx_id, dcam_path_name_get(path_id),
				cam_queue_cnt_get(&path->out_buf_queue),
				cam_queue_cnt_get(&path->result_queue), frame->fid);
		cam_queue_enqueue(&path->reserved_buf_queue, &frame->list);
		return NULL;
	}

	/* assign same SOF time here for each path */
	dev_fid = frame->fid - sw_ctx->base_fid;
	ts = &sw_ctx->frame_ts[tsid(dev_fid)];
	frame->sensor_time.tv_sec = ts->tv_sec;
	frame->sensor_time.tv_usec = ts->tv_nsec / NSEC_PER_USEC;
	frame->boot_sensor_time = sw_ctx->frame_ts_boot[tsid(dev_fid)];
	if (dev_fid == 0)
		frame->frame_interval_time = 0;
	else
		frame->frame_interval_time = frame->boot_sensor_time - sw_ctx->frame_ts_boot[tsid(dev_fid - 1)];

	pr_debug("DCAM%u %s: TX DONE, fid %u, %lld\n",
		dcam_hw_ctx->hw_ctx_id, dcam_path_name_get(path_id), frame->fid, frame->frame_interval_time);

	if (!sw_ctx->rps && !frame->boot_sensor_time) {
		pr_info("DCAM%u %s fid %u invalid 0 timestamp\n",
			dcam_hw_ctx->hw_ctx_id, dcam_path_name_get(path_id), frame->fid);
		if (frame->is_reserved)
			cam_queue_enqueue(&path->reserved_buf_queue, &frame->list);
		else
			cam_queue_enqueue(&path->out_buf_queue, &frame->list);
		frame = NULL;
	}

	return frame;
}

/*
 * Add timestamp and dispatch frame.
 */
static void dcamint_frame_dispatch(struct dcam_hw_context *dcam_hw_ctx, struct dcam_sw_context *sw_ctx,
				enum dcam_path_id path_id,
				struct camera_frame *frame,
				enum dcam_cb_type type)
{
	uint32_t ret = 0;
	timespec cur_ts;

	if (unlikely(!dcam_hw_ctx || !frame || !is_path_id(path_id) || !sw_ctx))
		return;
	ktime_get_ts(&cur_ts);
	frame->time.tv_sec = cur_ts.tv_sec;
	frame->time.tv_usec = cur_ts.tv_nsec / NSEC_PER_USEC;
	frame->boot_time = ktime_get_boottime();
	pr_debug("DCAM%u path %d: time %06d.%06d\n", dcam_hw_ctx->hw_ctx_id, path_id,
			(int)frame->time.tv_sec, (int)frame->time.tv_usec);

	/* data path has independent buffer. statis share one same buffer */
	if (type == DCAM_CB_DATA_DONE) {
		ret = cam_buf_iommu_unmap(&frame->buf);
		if (ret) {
			pr_err("fail ro unmap path:%d buffer.\n", path_id);
			ret = cam_queue_enqueue(&sw_ctx->path[path_id].out_buf_queue, &frame->list);
			if (ret) {
				pr_err("fail to enqueue path:%d frame, q_cnt:%d.\n", path_id, sw_ctx->path[path_id].out_buf_queue.cnt);
				if (frame->buf.type == CAM_BUF_KERNEL) {
					cam_buf_free(&frame->buf);
					cam_queue_empty_frame_put(frame);
				}
			}
			return;
		}
	}

	sw_ctx->dcam_cb_func(type, frame, sw_ctx->cb_priv_data);
}

static void dcamint_sof_event_dispatch(struct dcam_sw_context *sw_ctx)
{
	struct camera_frame *frame = NULL;
	timespec *ts = NULL;

	if (!sw_ctx) {
		pr_err("fail to get valid input sw_ctx\n");
		return;
	}

	frame = cam_queue_empty_frame_get();
	if (frame) {
		ts = &sw_ctx->frame_ts[tsid(sw_ctx->frame_index)];
		frame->sensor_time.tv_sec = ts->tv_sec;
		frame->sensor_time.tv_usec = ts->tv_nsec / NSEC_PER_USEC;
		frame->boot_sensor_time = sw_ctx->frame_ts_boot[tsid(sw_ctx->frame_index)];
		frame->evt = IMG_TX_DONE;
		frame->irq_type = CAMERA_IRQ_DONE;
		frame->irq_property = IRQ_DCAM_SOF;
		frame->fid = sw_ctx->base_fid + sw_ctx->frame_index;
		sw_ctx->dcam_cb_func(DCAM_CB_IRQ_EVENT, frame, sw_ctx->cb_priv_data);
	}
}

static void dcamint_index_fix(struct dcam_sw_context *sw_ctx, uint32_t begin, uint32_t num_group)
{
	struct dcam_path_desc *path = NULL;
	struct camera_frame *frame = NULL;
	struct list_head head;
	uint32_t count = 0;
	int i = 0, j = 0;

	for (i = 0; i < DCAM_PATH_MAX; i++) {
		path = &sw_ctx->path[i];
		count = num_group;
		if (i == DCAM_PATH_BIN)
			count *= sw_ctx->slowmotion_count;

		if (atomic_read(&path->user_cnt) < 1 || atomic_read(&path->is_shutoff) > 0)
			continue;

		if (cam_queue_cnt_get(&path->result_queue) < count)
			continue;

		pr_info("path %s fix %u index to %u\n",dcam_path_name_get(i), count, begin);
		INIT_LIST_HEAD(&head);

		j = 0;
		while (j++ < count) {
			frame = cam_queue_dequeue_tail(&path->result_queue);
			list_add_tail(&frame->list, &head);
		}

		j = 0;
		while (j++ < count) {
			frame = list_last_entry(&head,
						struct camera_frame,
						list);
			list_del(&frame->list);
			frame->fid = sw_ctx->base_fid + begin - 1;
			if (i == DCAM_PATH_BIN) {
				frame->fid += j;
			} else if (i == DCAM_PATH_AEM) {
				frame->fid += j * sw_ctx->slowmotion_count;
			} else {
				frame->fid += (j - 1) * sw_ctx->slowmotion_count;
				frame->fid += 1;
			}
			cam_queue_enqueue(&path->result_queue, &frame->list);
		}
	}
}

/*
 * Check if frame in result queue of @path is occupied by hardware.
 */
static int dcamint_frame_check(struct dcam_hw_context *dcam_hw_ctx,
		struct dcam_path_desc *path)
{
	struct camera_frame *frame = NULL;
	uint32_t frame_addr = 0, reg_value = 0;
	unsigned long reg_addr = 0;

	frame = cam_queue_dequeue_peek(&path->result_queue, struct camera_frame, list);
	if (unlikely(!frame))
		return 0;

	frame_addr = frame->buf.iova[0];

	reg_addr = path->path_id == DCAM_PATH_BIN ?
		DCAM_BIN_BASE_WADDR0 : DCAM_FULL_BASE_WADDR;
	reg_value = DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, reg_addr);

	pr_debug("DCAM%u %s frame 0x%08x reg 0x%08x cnt %u\n",
		 dcam_hw_ctx->hw_ctx_id, dcam_path_name_get(path->path_id),
		 frame_addr, reg_value, cam_queue_cnt_get(&path->result_queue));

	return frame_addr == reg_value;
}

/* fix result */
enum dcam_fix_result {
	DEFER_TO_NEXT,
	INDEX_FIXED,
	BUFFER_READY,
};

/*
 * Use mipi_cap_frm_cnt to fix frame index error issued by interrupt delay.
 * Since max value of mipi_cap_frm_cnt is 0x3f, the max delay we can recover
 * from is 2.1s in normal scene or 0.525s in slow motion scene.
 */
static enum dcam_fix_result dcamint_fix_index_if_needed(struct dcam_hw_context *dcam_hw_ctx, struct dcam_sw_context *sw_ctx)
{
	uint32_t frm_cnt = 0, cur_cnt = 0;
	uint32_t old_index = 0, begin = 0, end = 0;
	uint32_t old_n = 0, cur_n = 0, old_d = 0, cur_d = 0, cur_rd = 0;
	timespec delta_ts;
	ktime_t delta_ns;

	frm_cnt = DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_CAP_FRM_CLR) & 0x3f;
	cur_cnt = tsid(sw_ctx->frame_index + 1);

	/* adjust frame index for current frame */
	if (cur_cnt != frm_cnt) {
		uint32_t diff = DCAM_FRAME_TIMESTAMP_COUNT;

		/*
		 * leave fixing work to next frame to make sure there's enough
		 * time for us in slow motion scene, assuming that next CAP_SOF
		 * is not delayed
		 */
		if (sw_ctx->slowmotion_count && !sw_ctx->need_fix) {
			dcam_hw_ctx->handled_bits = 0xFFFFFFFF;
			sw_ctx->need_fix = true;
			return DEFER_TO_NEXT;
		}

		diff = diff + frm_cnt - cur_cnt;
		diff &= DCAM_FRAME_TIMESTAMP_COUNT - 1;

		if (sw_ctx->frame_index)
			old_index = sw_ctx->frame_index - 1;
		sw_ctx->frame_index += diff;
		pr_info("DCAM%u adjust index by %u, new %u\n",
			dcam_hw_ctx->hw_ctx_id, diff, sw_ctx->frame_index);
	}

	/* record SOF timestamp for current frame */
	sw_ctx->frame_ts_boot[tsid(sw_ctx->frame_index)] = ktime_get_boottime();
	ktime_get_ts(&sw_ctx->frame_ts[tsid(sw_ctx->frame_index)]);

	if (frm_cnt == cur_cnt) {
		sw_ctx->index_to_set = sw_ctx->frame_index + 1;
		return INDEX_FIXED;
	}

	if (!sw_ctx->slowmotion_count) {
		struct dcam_path_desc *path = NULL;
		struct camera_frame *frame = NULL;
		int i = 0, vote = 0;

		/* fix index for last 1 frame */
		for (i = 0; i < DCAM_PATH_MAX; i++) {
			path = &sw_ctx->path[i];

			if (atomic_read(&path->set_frm_cnt) < 1)
				continue;

			/*
			 * Use BIN or FULL to check if SOF is missing. If SOF is
			 * missing, we should discard TX_DONE accordingly.
			 */
			if (path->path_id == DCAM_PATH_BIN
				|| path->path_id == DCAM_PATH_FULL)
				vote |= dcamint_frame_check(dcam_hw_ctx, path);
			frame = cam_queue_dequeue_tail(&path->result_queue);
			if (frame == NULL)
				continue;
			frame->fid = sw_ctx->base_fid + sw_ctx->frame_index;
			cam_queue_enqueue(&path->result_queue, &frame->list);
		}

		if (vote) {
			pr_info("DCAM%u more TX_DONE than SOF\n", dcam_hw_ctx->hw_ctx_id);
			dcam_hw_ctx->handled_bits = DCAMINT_ALL_TX_DONE;
		}

		sw_ctx->index_to_set = sw_ctx->frame_index + 1;
		return INDEX_FIXED;
	}

	sw_ctx->need_fix = false;

	/* restore timestamp and index for slow motion */
	delta_ns = ktime_sub(sw_ctx->frame_ts_boot[tsid(old_index)],
				sw_ctx->frame_ts_boot[tsid(old_index - 1)]);
	delta_ts = cam_timespec_sub(sw_ctx->frame_ts[tsid(old_index)],
				sw_ctx->frame_ts[tsid(old_index - 1)]);

	end = sw_ctx->frame_index;
	begin = max(rounddown(end, sw_ctx->slowmotion_count), old_index + 1);
	while (--end >= begin) {
		sw_ctx->frame_ts_boot[tsid(end)]
			= ktime_sub_ns(sw_ctx->frame_ts_boot[tsid(end + 1)],
					delta_ns);
		sw_ctx->frame_ts[tsid(end)]
			= cam_timespec_sub(sw_ctx->frame_ts[tsid(end + 1)],
					delta_ts);
	}

	/* still in-time if index not delayed to another group */
	old_d = old_index / sw_ctx->slowmotion_count;
	cur_d = sw_ctx->frame_index / sw_ctx->slowmotion_count;
	if (old_d == cur_d)
		return INDEX_FIXED;

	old_n = old_index % sw_ctx->slowmotion_count;
	cur_n = sw_ctx->frame_index % sw_ctx->slowmotion_count;
	cur_rd = rounddown(sw_ctx->frame_index, sw_ctx->slowmotion_count);
	if (old_n != sw_ctx->slowmotion_count - 1) {
		/* fix index for last 1~8 frames */
		dcam_hw_ctx->handled_bits = DCAMINT_ALL_TX_DONE;
		dcamint_index_fix(sw_ctx, cur_rd, 2);

		return BUFFER_READY;
	} else /* if (cur_n != dev->slowmotion_count - 1) */{
		/* fix index for last 1~4 frames */
		struct dcam_path_desc *path = &sw_ctx->path[DCAM_PATH_BIN];
		if (cam_queue_cnt_get(&path->result_queue)
		    <= sw_ctx->slowmotion_count) {
			/*
			 * ignore TX DONE if already handled in last interrupt
			 */
			dcam_hw_ctx->handled_bits = DCAMINT_ALL_TX_DONE;
		}
		dcamint_index_fix(sw_ctx, cur_rd, 1);

		return INDEX_FIXED;
	}
}

static void dcamint_debug_dump(struct dcam_hw_context *dcam_hw_ctx, struct dcam_sw_context *sw_ctx, struct dcam_dev_param *pm)
{
	int size;
	timespec *frame_ts;
	struct camera_frame *frame = NULL;
	struct debug_base_info *base_info;
	void *pm_data;

	if (!sw_ctx) {
		pr_err("fail to get valid input sw_ctx\n");
		return;
	}

	sw_ctx->dcam_cb_func(DCAM_CB_GET_PMBUF, (void *)&frame, sw_ctx->cb_priv_data);
	if (frame == NULL)
		return;

	base_info = (struct debug_base_info *)frame->buf.addr_k[0];
	if (base_info == NULL) {
		cam_queue_empty_frame_put(frame);
		return;
	}
	base_info->cam_id = -1;
	base_info->dcam_cid = dcam_hw_ctx->hw_ctx_id;
	base_info->isp_cid = -1;
	base_info->scene_id = PM_SCENE_PRE;
	base_info->frame_id = sw_ctx->base_fid + sw_ctx->frame_index;

	frame_ts = &sw_ctx->frame_ts[tsid(sw_ctx->frame_index)];
	base_info->sec =  frame_ts->tv_sec;
	base_info->usec = frame_ts->tv_nsec / NSEC_PER_USEC;

	frame->fid = base_info->frame_id;
	frame->sensor_time.tv_sec = base_info->sec;
	frame->sensor_time.tv_usec = base_info->usec;

	pm_data = (void *)(base_info + 1);
	size = dcam_k_dump_pm(pm_data, (void *)pm);
	if (size >= 0)
		base_info->size = (int32_t)size;
	else
		base_info->size = 0;

	pr_debug("dcam%d, scene %d  fid %d  dsize %d\n",
		base_info->dcam_cid, base_info->scene_id,
		base_info->frame_id, base_info->size);

	sw_ctx->dcam_cb_func(DCAM_CB_STATIS_DONE, frame, sw_ctx->cb_priv_data);
}

/*
 * Set buffer and update parameters. Fix potential index error issued by
 * interrupt delay.
 */
static void dcamint_cap_sof(void *param, struct dcam_sw_context *sw_ctx)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	struct cam_hw_info *hw = NULL;
	struct dcam_path_desc *path = NULL;
	struct dcam_hw_path_ctrl path_ctrl;
	enum dcam_fix_result fix_result;
	struct dcam_hw_auto_copy copyarg;
	unsigned long flag;
	int i;
	if (!sw_ctx) {
		pr_err("fail to get valid input sw_ctx\n");
		return;
	}

	if (sw_ctx->offline) {
		pr_debug("dcam%d offline\n", dcam_hw_ctx->hw_ctx_id);
		return;
	}

	pr_debug("DCAM%d cap_sof\n", dcam_hw_ctx->hw_ctx_id);
	hw = sw_ctx->dev->hw;
	fix_result = dcamint_fix_index_if_needed(dcam_hw_ctx, sw_ctx);
	if (fix_result == DEFER_TO_NEXT)
		return;

	if (sw_ctx->slowmotion_count) {
		uint32_t n = sw_ctx->frame_index % sw_ctx->slowmotion_count;

		/* auto copy at last frame of a group of slow motion frames */
		if (n == sw_ctx->slowmotion_count - 1) {
			/* This register write is time critical,do not modify
			 * fresh bin_auto_copy coef_auto_copy
			 */
			sw_ctx->auto_cpy_id |= DCAM_CTRL_BIN;
		}

		/* set buffer at first frame of a group of slow motion frames */
		if (n || fix_result == BUFFER_READY)
			goto dispatch_sof;

		sw_ctx->index_to_set = sw_ctx->frame_index + sw_ctx->slowmotion_count;
	}

	for (i = 0; i < DCAM_PATH_MAX; i++) {
		if (i == DCAM_PATH_RAW && sw_ctx->need_dcam_raw)
			continue;
		path = &sw_ctx->path[i];
		if (atomic_read(&path->user_cnt) < 1 || atomic_read(&path->is_shutoff) > 0)
			continue;

		/* TODO: frm_deci and frm_skip in slow motion */
		path->frm_cnt++;
		if (path->frm_cnt <= path->frm_skip)
			continue;

		/* @frm_deci is the frame index of output frame */
		if ((path->frm_deci_cnt++ >= path->frm_deci)
			|| sw_ctx->slowmotion_count) {
			path->frm_deci_cnt = 0;
			if (path->path_id == DCAM_PATH_FULL) {
				spin_lock_irqsave(&path->state_lock, flag);
				if (path->state == DCAM_PATH_PAUSE
					&& path->state_update) {
					atomic_inc(&path->set_frm_cnt);
					path_ctrl.idx = dcam_hw_ctx->hw_ctx_id;
					path_ctrl.path_id = path->path_id;
					path_ctrl.type = HW_DCAM_PATH_PAUSE;
					hw->dcam_ioctl(hw, DCAM_HW_CFG_PATH_CTRL, &path_ctrl);
				} else if (path->state == DCAM_PATH_RESUME
					&& path->state_update) {
					path_ctrl.idx = dcam_hw_ctx->hw_ctx_id;
					path_ctrl.path_id = path->path_id;
					path_ctrl.type = HW_DCAM_PATH_RESUME;
					hw->dcam_ioctl(hw, DCAM_HW_CFG_PATH_CTRL, &path_ctrl);
				}
				path->state_update = 0;
				if (path->state == DCAM_PATH_PAUSE) {
					spin_unlock_irqrestore(&path->state_lock, flag);
					continue;
				}
				spin_unlock_irqrestore(&path->state_lock, flag);
			}
			dcam_path_store_frm_set(sw_ctx, path);
		}
	}

dispatch_sof:
	sw_ctx->auto_cpy_id = DCAM_CTRL_ALL;
	copyarg.id = sw_ctx->auto_cpy_id;
	copyarg.idx = dcam_hw_ctx->hw_ctx_id;
	copyarg.glb_reg_lock = sw_ctx->glb_reg_lock;
	hw->dcam_ioctl(hw, DCAM_HW_CFG_AUTO_COPY, &copyarg);
	sw_ctx->auto_cpy_id = 0;

	if (!sw_ctx->slowmotion_count
		|| !(sw_ctx->frame_index % sw_ctx->slowmotion_count)) {
		dcamint_sof_event_dispatch(sw_ctx);
	}
	sw_ctx->iommu_status = (uint32_t)(-1);

	dcamint_debug_dump(dcam_hw_ctx, sw_ctx, &sw_ctx->ctx[0].blk_pm);
	sw_ctx->frame_index++;
}

/* for slow motion mode */
static void dcamint_preview_sof(void *param, struct dcam_sw_context *sw_ctx)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	struct dcam_path_desc *path = NULL;
	int i = 0;
	if (!sw_ctx) {
		pr_err("fail to get valid input sw_ctx\n");
		return;
	}

	if (sw_ctx->virtualsensor) {
		pr_debug("dcam%d virtual sensor \n", dcam_hw_ctx->hw_ctx_id);
		dcamint_sof_event_dispatch(sw_ctx);
		return;
	}
	if (sw_ctx->offline) {
		pr_debug("dcam%d offline\n", dcam_hw_ctx->hw_ctx_id);
		return;
	}

	sw_ctx->frame_index += sw_ctx->slowmotion_count;
	pr_debug("DCAM%u cnt=%d, fid: %u\n", dcam_hw_ctx->hw_ctx_id,
		DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_CAP_FRM_CLR) & 0x3f,
		sw_ctx->frame_index);

	for (i = 0; i < DCAM_PATH_MAX; i++) {
		path = &sw_ctx->path[i];
		if (atomic_read(&path->user_cnt) < 1 || atomic_read(&path->is_shutoff) > 0)
			continue;

		/* frame deci is deprecated in slow motion */
		dcam_path_store_frm_set(sw_ctx, path);
	}

	dcamint_sof_event_dispatch(sw_ctx);
}

static void dcamint_sensor_sof(void *param, struct dcam_sw_context *sw_ctx)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;

	if (!sw_ctx) {
		pr_err("fail to get valid input sw_ctx\n");
		return;
	}

	pr_debug("DCAM%d, dcamint_sensor_sof raw_callback = %d frame_index=%d\n",
		dcam_hw_ctx->hw_ctx_id, sw_ctx->raw_callback, sw_ctx->frame_index);

	if (sw_ctx->raw_callback) {
		if (sw_ctx->frame_index == 0)
			sw_ctx->frame_index++;
		else
			dcamint_cap_sof(param, sw_ctx);
	}
}

/* for Flash */
static void dcamint_sensor_eof(void *param, struct dcam_sw_context *sw_ctx)
{
	struct camera_frame *pframe = NULL;
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;

	if (!sw_ctx) {
		pr_err("fail to get valid input sw_ctx\n");
		return;
	}

	if (sw_ctx->offline) {
		pr_debug("dcam%d offline\n", dcam_hw_ctx->hw_ctx_id);
		return;
	}

	pframe = cam_queue_empty_frame_get();
	if (pframe) {
		pframe->evt = IMG_TX_DONE;
		pframe->irq_type = CAMERA_IRQ_DONE;
		pframe->irq_property = IRQ_DCAM_SN_EOF;
		sw_ctx->dcam_cb_func(DCAM_CB_IRQ_EVENT, pframe, sw_ctx->cb_priv_data);
	}
}

/*
 * cycling frames through FULL path
 */
static void dcamint_full_path_done(void *param, struct dcam_sw_context *sw_ctx)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	struct camera_frame *frame = NULL;
	struct dcam_path_desc *path = NULL;
	struct dcam_3dnrmv_ctrl *mv_state = NULL;
	uint32_t mv_ready = 0;

	if (!sw_ctx) {
		pr_err("fail to get valid input sw_ctx\n");
		return;
	}
	if (sw_ctx->virtualsensor) {
		if(sw_ctx->slice_num > 0)
			complete(&sw_ctx->slice_done);
	}

	path = &sw_ctx->path[DCAM_PATH_FULL];
	if ((frame = dcamint_frame_prepare(dcam_hw_ctx, sw_ctx, DCAM_PATH_FULL))) {
		if (sw_ctx->is_4in1) {
			if (sw_ctx->skip_4in1 > 0) {
				sw_ctx->skip_4in1--;
				/* need skip 1 frame when switch full source
				 * give buffer back to queue
				 */
				cam_buf_iommu_unmap(&frame->buf);
				sw_ctx->dcam_cb_func(DCAM_CB_RET_SRC_BUF, frame,
					sw_ctx->cb_priv_data);
				return;
			}
			if (!sw_ctx->lowlux_4in1) /* 4in1,send to hal for remosaic */
				frame->irq_type = CAMERA_IRQ_4IN1_DONE;
			else/* low lux, to isp as normal */
				frame->irq_type = CAMERA_IRQ_IMG;
		}

		if (sw_ctx->is_3dnr && !sw_ctx->dcam_slice_mode && !sw_ctx->is_4in1) {
			mv_ready = sw_ctx->nr3_me.full_path_mv_ready;
			if (mv_ready == 0) {
				sw_ctx->nr3_me.full_path_cnt++;
				cam_queue_enqueue(&path->middle_queue, &frame->list);
				return;
			} else if (mv_ready == 1) {
				mv_state = cam_queue_dequeue(&sw_ctx->fullpath_mv_queue, struct dcam_3dnrmv_ctrl, list);
				if (mv_state) {
					frame->nr3_me.mv_x = mv_state->nr3_me.mv_x;
					frame->nr3_me.mv_y = mv_state->nr3_me.mv_y;
					frame->nr3_me.project_mode = mv_state->nr3_me.project_mode;
					frame->nr3_me.sub_me_bypass = mv_state->nr3_me.sub_me_bypass;
					frame->nr3_me.src_height = mv_state->nr3_me.src_height;
					frame->nr3_me.src_width = mv_state->nr3_me.src_width;
					frame->nr3_me.valid = mv_state->nr3_me.valid;
					sw_ctx->nr3_me.full_path_mv_ready = 0;
					sw_ctx->nr3_me.full_path_cnt = 0;
					cam_queue_empty_mv_state_put(mv_state);
				}
				pr_debug("dcam %d,fid %d mv_x %d mv_y %d",dcam_hw_ctx->hw_ctx_id,frame->fid,frame->nr3_me.mv_x,frame->nr3_me.mv_y);
			}
		}
		dcamint_frame_dispatch(dcam_hw_ctx, sw_ctx, DCAM_PATH_FULL, frame, DCAM_CB_DATA_DONE);
	}

	if (sw_ctx->virtualsensor) {
		if ((sw_ctx->dcam_slice_mode == CAM_OFFLINE_SLICE_SW && sw_ctx->slice_count == 0)
				|| sw_ctx->dcam_slice_mode != CAM_OFFLINE_SLICE_SW) {
			/* there is source buffer for offline process */
			frame = cam_queue_dequeue(&sw_ctx->proc_queue,struct camera_frame, list);
			if (frame) {
				cam_buf_iommu_unmap(&frame->buf);
				sw_ctx->dcam_cb_func(DCAM_CB_RET_SRC_BUF, frame, sw_ctx->cb_priv_data);
			}
		}
		complete(&sw_ctx->frm_done);
	}
}

/*
 * cycling frames through BIN path
 */
static void dcamint_bin_path_done(void *param, struct dcam_sw_context *sw_ctx)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	struct dcam_path_desc *path = NULL;
	struct camera_frame *frame = NULL;
	struct dcam_3dnrmv_ctrl *mv_state = NULL;
	uint32_t mv_ready = 0;
	uint32_t i = 0, cnt = 0;

	if (!sw_ctx) {
		pr_err("fail to get valid input sw_ctx\n");
		return;
	}

	if (unlikely(dcam_hw_ctx->hw_ctx_id == DCAM_HW_CONTEXT_2))
		return;

	path = &sw_ctx->path[DCAM_PATH_BIN];
	cnt = atomic_read(&path->set_frm_cnt);
	if (cnt <= sw_ctx->slowmotion_count) {
		pr_warn("warning: DCAM%u BIN cnt %d, deci %u, out %u, result %u\n",
			dcam_hw_ctx->hw_ctx_id, cnt, path->frm_deci,
			cam_queue_cnt_get(&path->out_buf_queue),
			cam_queue_cnt_get(&path->result_queue));
		return;
	}

	if (sw_ctx->offline) {
		if(sw_ctx->slice_num > 0)
			complete(&sw_ctx->slice_done);
	}

	if ((sw_ctx->virtualsensor) && (atomic_read(&sw_ctx->virtualsensor_cap_en) == 0)) {
		if(sw_ctx->slice_num > 0)
			complete(&sw_ctx->slice_done);
	}

	if ((frame = dcamint_frame_prepare(dcam_hw_ctx, sw_ctx, DCAM_PATH_BIN))) {
		if (sw_ctx->dcam_slice_mode) {
			frame->dcam_idx = dcam_hw_ctx->sw_ctx_id;
			frame->sw_slice_num = sw_ctx->slice_num;
			frame->sw_slice_no = sw_ctx->slice_num - sw_ctx->slice_count;
			frame->slice_trim = sw_ctx->slice_trim;

			if (sw_ctx->slice_count > 0)
				sw_ctx->slice_count--;
			if (sw_ctx->slice_count > 0)
				pr_debug("offline_slice%d_done\n", sw_ctx->slice_num - (sw_ctx->slice_count + 1));
			else {
				pr_debug("offline_lastslice_done\n");
				sw_ctx->slice_num = 0;
			}
		}

		if (DCAM_FETCH_TWICE(sw_ctx) && DCAM_FIRST_FETCH(sw_ctx)) {
			struct camera_frame *frame2 = NULL;

			complete(&sw_ctx->frm_done);
			pr_debug("raw fetch done\n");
			if ((sw_ctx->dcam_slice_mode == CAM_OFFLINE_SLICE_SW && sw_ctx->slice_count == 0)
				|| sw_ctx->dcam_slice_mode != CAM_OFFLINE_SLICE_SW) {
				/* there is source buffer for offline process */
				frame2 = cam_queue_dequeue(&sw_ctx->proc_queue,
					struct camera_frame, list);
				if (frame2) {
					cam_buf_iommu_unmap(&frame2->buf);
					sw_ctx->dev->dcam_pipe_ops->cfg_path(sw_ctx,
						DCAM_PATH_CFG_OUTPUT_BUF, DCAM_PATH_BIN, frame2);
				}
			}

			dcam_core_context_unbind(sw_ctx);
			cam_buf_iommu_unmap(&frame->buf);
			sw_ctx->dcam_cb_func(DCAM_CB_DATA_DONE, frame, sw_ctx->cb_priv_data);
			return;
		}

		if (sw_ctx->is_3dnr && !sw_ctx->dcam_slice_mode) {
			mv_ready = sw_ctx->nr3_me.bin_path_mv_ready;
			if (mv_ready == 0) {
				sw_ctx->nr3_me.bin_path_cnt++;
				cam_queue_enqueue(&path->middle_queue, &frame->list);
				return;
			} else if (mv_ready == 1) {
				mv_state = cam_queue_dequeue(&sw_ctx->binpath_mv_queue, struct dcam_3dnrmv_ctrl, list);
				if (mv_state) {
					frame->nr3_me.mv_x = mv_state->nr3_me.mv_x;
					frame->nr3_me.mv_y = mv_state->nr3_me.mv_y;
					frame->nr3_me.project_mode = mv_state->nr3_me.project_mode;
					frame->nr3_me.sub_me_bypass = mv_state->nr3_me.sub_me_bypass;
					frame->nr3_me.src_height = mv_state->nr3_me.src_height;
					frame->nr3_me.src_width = mv_state->nr3_me.src_width;
					frame->nr3_me.valid = mv_state->nr3_me.valid;
					sw_ctx->nr3_me.bin_path_mv_ready = 0;
					sw_ctx->nr3_me.bin_path_cnt = 0;
					cam_queue_empty_mv_state_put(mv_state);
				}
				pr_debug("dcam %d,fid %d mv_x %d mv_y %d",dcam_hw_ctx->hw_ctx_id,frame->fid,frame->nr3_me.mv_x,frame->nr3_me.mv_y);
			}
		}
		dcamint_frame_dispatch(dcam_hw_ctx, sw_ctx, DCAM_PATH_BIN, frame, DCAM_CB_DATA_DONE);
	}

	i = 0;
	while (++i < sw_ctx->slowmotion_count)
		dcamint_frame_dispatch(dcam_hw_ctx, sw_ctx, DCAM_PATH_BIN,
				dcamint_frame_prepare(dcam_hw_ctx, sw_ctx, DCAM_PATH_BIN),
				DCAM_CB_DATA_DONE);

	if (sw_ctx->offline) {
		if ((sw_ctx->dcam_slice_mode == CAM_OFFLINE_SLICE_SW && sw_ctx->slice_count == 0)
			|| sw_ctx->dcam_slice_mode != CAM_OFFLINE_SLICE_SW) {
			/* there is source buffer for offline process */
			frame = cam_queue_dequeue(&sw_ctx->proc_queue,
				struct camera_frame, list);
			if (frame) {
				cam_buf_iommu_unmap(&frame->buf);
				sw_ctx->dcam_cb_func(DCAM_CB_RET_SRC_BUF, frame, sw_ctx->cb_priv_data);
			}
		}
		dcam_core_context_unbind(sw_ctx);
		complete(&sw_ctx->frm_done);
	}

	if ((sw_ctx->virtualsensor) && (atomic_read(&sw_ctx->virtualsensor_cap_en) == 0)) {
		if ((sw_ctx->dcam_slice_mode == CAM_OFFLINE_SLICE_SW && sw_ctx->slice_count == 0)
				|| sw_ctx->dcam_slice_mode != CAM_OFFLINE_SLICE_SW) {
			/* there is source buffer for offline process */
			frame = cam_queue_dequeue(&sw_ctx->proc_queue,struct camera_frame, list);
			if (frame) {
				cam_buf_iommu_unmap(&frame->buf);
				sw_ctx->dcam_cb_func(DCAM_CB_RET_SRC_BUF, frame, sw_ctx->cb_priv_data);
			}
		}
		complete(&sw_ctx->frm_done);
	}
}

/*
 * cycling frames through AEM path
 */
static void dcamint_aem_done(void *param, struct dcam_sw_context *sw_ctx)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	struct camera_frame *frame = NULL;

	if (unlikely(dcam_hw_ctx->hw_ctx_id == DCAM_HW_CONTEXT_2))
		return;

	if ((frame = dcamint_frame_prepare(dcam_hw_ctx, sw_ctx, DCAM_PATH_AEM)))
		dcamint_frame_dispatch(dcam_hw_ctx, sw_ctx, DCAM_PATH_AEM, frame, DCAM_CB_STATIS_DONE);
}

/*
 * cycling frames through PDAF path
 */
static void dcamint_pdaf_path_done(void *param, struct dcam_sw_context *sw_ctx)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	struct camera_frame *frame = NULL;

	if (unlikely(dcam_hw_ctx->hw_ctx_id == DCAM_HW_CONTEXT_2))
		return;

	if ((frame = dcamint_frame_prepare(dcam_hw_ctx, sw_ctx, DCAM_PATH_PDAF)))
		dcamint_frame_dispatch(dcam_hw_ctx, sw_ctx, DCAM_PATH_PDAF, frame, DCAM_CB_STATIS_DONE);
}

/*
 * cycling frames through VCH2 path
 */
static void dcamint_vch2_path_done(void *param, struct dcam_sw_context *sw_ctx)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	struct dcam_path_desc *path = NULL;
	struct camera_frame *frame = NULL;
	enum dcam_cb_type type;

	if (!sw_ctx) {
		pr_err("fail to get valid input sw_ctx\n");
		return;
	}

	if (!sw_ctx) {
		pr_err("fail to get valid input sw_ctx\n");
		return;
	}

	pr_debug("dcamint_vch2_path_done hw_ctx_id = %d\n", dcam_hw_ctx->hw_ctx_id);
	if (unlikely(dcam_hw_ctx->hw_ctx_id == DCAM_HW_CONTEXT_2))
		return;

	path = &sw_ctx->path[DCAM_PATH_VCH2];
	type = path->src_sel ? DCAM_CB_DATA_DONE : DCAM_CB_STATIS_DONE;
	if ((frame = dcamint_frame_prepare(dcam_hw_ctx, sw_ctx, DCAM_PATH_VCH2)))
		dcamint_frame_dispatch(dcam_hw_ctx, sw_ctx, DCAM_PATH_VCH2, frame, type);
}

/*
 * cycling frame through VCH3 path
 */
static void dcamint_vch3_path_done(void *param, struct dcam_sw_context *sw_ctx)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	struct camera_frame *frame = NULL;

	if (unlikely(dcam_hw_ctx->hw_ctx_id == DCAM_HW_CONTEXT_2))
		return;

	if ((frame = dcamint_frame_prepare(dcam_hw_ctx, sw_ctx, DCAM_PATH_VCH3)))
		dcamint_frame_dispatch(dcam_hw_ctx, sw_ctx, DCAM_PATH_VCH3, frame, DCAM_CB_STATIS_DONE);
}

/*
 * cycling frames through AFM path
 */
static void dcamint_afm_done(void *param, struct dcam_sw_context *sw_ctx)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	struct camera_frame *frame = NULL;

	if (unlikely(dcam_hw_ctx->hw_ctx_id == DCAM_HW_CONTEXT_2))
		return;

	if ((frame = dcamint_frame_prepare(dcam_hw_ctx, sw_ctx, DCAM_PATH_AFM)))
		dcamint_frame_dispatch(dcam_hw_ctx, sw_ctx, DCAM_PATH_AFM, frame, DCAM_CB_STATIS_DONE);
}

static void dcamint_afl_done(void *param, struct dcam_sw_context *sw_ctx)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	struct camera_frame *frame = NULL;

	if (!sw_ctx) {
		pr_err("fail to get valid input sw_ctx\n");
		return;
	}

	if (unlikely(dcam_hw_ctx->hw_ctx_id == DCAM_HW_CONTEXT_2))
		return;

	dcam_path_store_frm_set(sw_ctx, &sw_ctx->path[DCAM_PATH_AFL]);
	if ((frame = dcamint_frame_prepare(dcam_hw_ctx, sw_ctx, DCAM_PATH_AFL))) {
		frame->fid = sw_ctx->base_fid + sw_ctx->index_to_set - 1;
		dcamint_frame_dispatch(dcam_hw_ctx, sw_ctx, DCAM_PATH_AFL, frame, DCAM_CB_STATIS_DONE);
	}
}

/*
 * cycling frames through 3DNR path
 * DDR data is not used by now, while motion vector is used by ISP
 */
static void dcamint_nr3_done(void *param, struct dcam_sw_context *sw_ctx)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)param;
	uint32_t p = 0, out0 = 0, out1 = 0;
	int ret = 0;
	struct camera_frame *frame = NULL;
	struct dcam_path_desc *path = NULL;
	struct dcam_3dnrmv_ctrl *full_mv_state = NULL;
	struct dcam_3dnrmv_ctrl *bin_mv_state = NULL;

	if (!sw_ctx || !dcam_hw_ctx || dcam_hw_ctx->hw_ctx_id >= DCAM_HW_CONTEXT_MAX) {
		pr_err("fail to get valid input sw_ctx\n");
		return;
	}

	p = DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, NR3_FAST_ME_PARAM);
	out0 = DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, NR3_FAST_ME_OUT0);
	out1 = DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, NR3_FAST_ME_OUT1);

	sw_ctx->nr3_me.full_path_mv_ready++;
	sw_ctx->nr3_me.bin_path_mv_ready++;
	if (sw_ctx->nr3_me.full_path_mv_ready > 1)
		sw_ctx->nr3_me.full_path_mv_ready = 1;
	if (sw_ctx->nr3_me.bin_path_mv_ready > 1)
		sw_ctx->nr3_me.bin_path_mv_ready = 1;

	full_mv_state = cam_queue_empty_mv_state_get();
	if (full_mv_state) {
		full_mv_state->nr3_me.sub_me_bypass = (p >> 8) & 0x1;
		full_mv_state->nr3_me.project_mode = (p >> 4) & 0x1;
		/* currently ping-pong is disabled, mv will always be stored in ping */
		full_mv_state->nr3_me.mv_x = (out0 >> 8) & 0xff;
		full_mv_state->nr3_me.mv_y = out0 & 0xff;
		full_mv_state->nr3_me.src_width = sw_ctx->cap_info.cap_size.size_x;
		full_mv_state->nr3_me.src_height = sw_ctx->cap_info.cap_size.size_y;
		full_mv_state->nr3_me.valid = 1;
		pr_debug("dcam %d full mv_x %d,mv_y %d full_path_cnt %d bin_path_cnt %d",dcam_hw_ctx->hw_ctx_id,full_mv_state->nr3_me.mv_x,full_mv_state->nr3_me.mv_y,sw_ctx->nr3_me.full_path_cnt,sw_ctx->nr3_me.bin_path_cnt);

		ret = cam_queue_enqueue(&sw_ctx->fullpath_mv_queue, &full_mv_state->list);
		if (ret) {
			pr_debug("full path path mv state overflow\n");
			cam_queue_empty_mv_state_put(full_mv_state);
		}
	}

	bin_mv_state = cam_queue_empty_mv_state_get();
	if (bin_mv_state) {
		bin_mv_state->nr3_me.sub_me_bypass = (p >> 8) & 0x1;
		bin_mv_state->nr3_me.project_mode = (p >> 4) & 0x1;
		/* currently ping-pong is disabled, mv will always be stored in ping */
		bin_mv_state->nr3_me.mv_x = (out0 >> 8) & 0xff;
		bin_mv_state->nr3_me.mv_y = out0 & 0xff;
		bin_mv_state->nr3_me.src_width = sw_ctx->cap_info.cap_size.size_x;
		bin_mv_state->nr3_me.src_height = sw_ctx->cap_info.cap_size.size_y;
		bin_mv_state->nr3_me.valid = 1;
		pr_debug("dcam %d bin mv_x %d,mv_y %d full_path_cnt %d bin_path_cnt %d",dcam_hw_ctx->hw_ctx_id,bin_mv_state->nr3_me.mv_x,bin_mv_state->nr3_me.mv_y,sw_ctx->nr3_me.full_path_cnt,sw_ctx->nr3_me.bin_path_cnt);

		ret = cam_queue_enqueue(&sw_ctx->binpath_mv_queue, &bin_mv_state->list);
		if (ret) {
			pr_debug("bin path mv state overflow\n");
			cam_queue_empty_mv_state_put(bin_mv_state);
		}
	}

	if (sw_ctx->nr3_me.full_path_cnt) {
		full_mv_state = cam_queue_dequeue(&sw_ctx->fullpath_mv_queue, struct dcam_3dnrmv_ctrl, list);
		path = &sw_ctx->path[DCAM_PATH_FULL];
		frame = cam_queue_dequeue(&path->middle_queue, struct camera_frame, list);
		if (frame) {
			frame->nr3_me.mv_x = full_mv_state->nr3_me.mv_x;
			frame->nr3_me.mv_y = full_mv_state->nr3_me.mv_y;
			frame->nr3_me.project_mode = full_mv_state->nr3_me.project_mode;
			frame->nr3_me.sub_me_bypass = full_mv_state->nr3_me.sub_me_bypass;
			frame->nr3_me.src_height = full_mv_state->nr3_me.src_height;
			frame->nr3_me.src_width = full_mv_state->nr3_me.src_width;
			frame->nr3_me.valid = full_mv_state->nr3_me.valid;
			dcamint_frame_dispatch(dcam_hw_ctx, sw_ctx, DCAM_PATH_FULL, frame,
						DCAM_CB_DATA_DONE);
		}
		cam_queue_empty_mv_state_put(full_mv_state);
		sw_ctx->nr3_me.full_path_mv_ready = 0;
		sw_ctx->nr3_me.full_path_cnt = 0;
	}

	if (sw_ctx->nr3_me.bin_path_cnt) {
		bin_mv_state = cam_queue_dequeue(&sw_ctx->binpath_mv_queue, struct dcam_3dnrmv_ctrl, list);
		path = &sw_ctx->path[DCAM_PATH_BIN];
		frame = cam_queue_dequeue(&path->middle_queue, struct camera_frame, list);
		if (frame) {
			frame->nr3_me.mv_x = bin_mv_state->nr3_me.mv_x;
			frame->nr3_me.mv_y = bin_mv_state->nr3_me.mv_y;
			frame->nr3_me.project_mode = bin_mv_state->nr3_me.project_mode;
			frame->nr3_me.sub_me_bypass = bin_mv_state->nr3_me.sub_me_bypass;
			frame->nr3_me.src_height = bin_mv_state->nr3_me.src_height;
			frame->nr3_me.src_width = bin_mv_state->nr3_me.src_width;
			frame->nr3_me.valid = bin_mv_state->nr3_me.valid;
			dcamint_frame_dispatch(dcam_hw_ctx, sw_ctx, DCAM_PATH_BIN, frame,
						DCAM_CB_DATA_DONE);
		}
		cam_queue_empty_mv_state_put(bin_mv_state);
		sw_ctx->nr3_me.bin_path_mv_ready = 0;
		sw_ctx->nr3_me.bin_path_cnt = 0;
	}

	dcamint_frame_prepare(dcam_hw_ctx, sw_ctx, DCAM_PATH_3DNR);

}

/*
 * reset tracker
 */
void dcam_int_tracker_reset(uint32_t idx)
{
	if (is_dcam_id(idx))
		memset(dcam_int_tracker[idx], 0, sizeof(dcam_int_tracker[idx]));

#ifdef DCAM_INT_RECORD
	if (is_dcam_id(idx)) {
		memset(dcam_int_recorder[idx][0], 0, sizeof(dcam_int_recorder) / 3);
		memset(int_index[idx], 0, sizeof(int_index) / 3);
	}
#endif
}

/*
 * print int count
 */
void dcam_int_tracker_dump(uint32_t idx)
{
	int i = 0;

	if (!is_dcam_id(idx))
		return;

	for (i = 0; i < DCAM_IRQ_NUMBER; i++) {
		if (dcam_int_tracker[idx][i])
			pr_info("DCAM%u i=%d, int=%u\n", idx, i,
				 dcam_int_tracker[idx][i]);
	}

#ifdef DCAM_INT_RECORD
	{
		uint32_t cnt, j;
		for (cnt = 0; cnt < (uint32_t)dcam_int_tracker[idx][DCAM_SENSOR_EOF]; cnt += 4) {
			j = (cnt & (INT_RCD_SIZE - 1));
			pr_info("DCAM%u j=%d, %03d.%04d, %03d.%04d, %03d.%04d, %03d.%04d, %03d.%04d, %03d.%04d\n",
			idx, j, (uint32_t)dcam_int_recorder[idx][DCAM_SENSOR_EOF][j] >> 16,
			(uint32_t)dcam_int_recorder[idx][DCAM_SENSOR_EOF][j] & 0xffff,
			(uint32_t)dcam_int_recorder[idx][DCAM_CAP_SOF][j] >> 16,
			(uint32_t)dcam_int_recorder[idx][DCAM_CAP_SOF][j] & 0xffff,
			(uint32_t)dcam_int_recorder[idx][DCAM_FULL_PATH_TX_DONE][j] >> 16,
			(uint32_t)dcam_int_recorder[idx][DCAM_FULL_PATH_TX_DONE][j] & 0xffff,
			(uint32_t)dcam_int_recorder[idx][DCAM_PREV_PATH_TX_DONE][j] >> 16,
			(uint32_t)dcam_int_recorder[idx][DCAM_PREV_PATH_TX_DONE][j] & 0xffff,
			(uint32_t)dcam_int_recorder[idx][DCAM_AEM_TX_DONE][j] >> 16,
			(uint32_t)dcam_int_recorder[idx][DCAM_AEM_TX_DONE][j] & 0xffff,
			(uint32_t)dcam_int_recorder[idx][DCAM_AFM_INTREQ1][j] >> 16,
			(uint32_t)dcam_int_recorder[idx][DCAM_AFM_INTREQ1][j] & 0xffff);
		}
	}
#endif
}

/*
 * registered sub interrupt service routine
 */
typedef void (*dcam_isr_type)(void *param, struct dcam_sw_context *sw_ctx);
static const dcam_isr_type _DCAM_ISRS[DCAM_HW_CONTEXT_MAX][32] = {
	[0][DCAM_SENSOR_SOF] = dcamint_sensor_sof,
	[0][DCAM_SENSOR_EOF] = dcamint_sensor_eof,
	[0][DCAM_CAP_SOF] = dcamint_cap_sof,
	[0][DCAM_PREVIEW_SOF] = dcamint_preview_sof,
	[0][DCAM_FULL_PATH_TX_DONE] = dcamint_full_path_done,
	[0][DCAM_PREV_PATH_TX_DONE] = dcamint_bin_path_done,
	[0][DCAM_PDAF_PATH_TX_DONE] = dcamint_pdaf_path_done,
	[0][DCAM_VCH2_PATH_TX_DONE] = dcamint_vch2_path_done,
	[0][DCAM_VCH3_PATH_TX_DONE] = dcamint_vch3_path_done,
	[0][DCAM_AEM_TX_DONE] = dcamint_aem_done,
	[0][DCAM_AFL_TX_DONE] = dcamint_afl_done,
	[0][DCAM_AFM_INTREQ1] = dcamint_afm_done,
	[0][DCAM_NR3_TX_DONE] = dcamint_nr3_done,

	[1][DCAM_SENSOR_EOF] = dcamint_sensor_eof,
	[1][DCAM_CAP_SOF] = dcamint_cap_sof,
	[1][DCAM_PREVIEW_SOF] = dcamint_preview_sof,
	[1][DCAM_FULL_PATH_TX_DONE] = dcamint_full_path_done,
	[1][DCAM_PREV_PATH_TX_DONE] = dcamint_bin_path_done,
	[1][DCAM_PDAF_PATH_TX_DONE] = dcamint_pdaf_path_done,
	[1][DCAM_VCH2_PATH_TX_DONE] = dcamint_vch2_path_done,
	[1][DCAM_VCH3_PATH_TX_DONE] = dcamint_vch3_path_done,
	[1][DCAM_AEM_TX_DONE] = dcamint_aem_done,
	[1][DCAM_AFL_TX_DONE] = dcamint_afl_done,
	[1][DCAM_AFM_INTREQ1] = dcamint_afm_done,
	[1][DCAM_NR3_TX_DONE] = dcamint_nr3_done,

	[2][DCAM2_SENSOR_SOF] = dcamint_cap_sof,   /*only for dcam2*/
	[2][DCAM2_SENSOR_EOF] = dcamint_sensor_eof,
	[2][DCAM2_FULL_PATH_TX_DONE] = dcamint_full_path_done,
};

/*
 * interested interrupt bits in DCAM0
 */
static const int _DCAM0_SEQUENCE[] = {
	DCAM_CAP_SOF,/* must */
	DCAM_SENSOR_SOF,
	DCAM_PREVIEW_SOF,
	DCAM_SENSOR_EOF,/* TODO: why for flash */
	DCAM_NR3_TX_DONE,/* for 3dnr, before data path */
	DCAM_PREV_PATH_TX_DONE,/* for bin path */
	DCAM_FULL_PATH_TX_DONE,/* for full path */
	DCAM_AEM_TX_DONE,/* for aem statis */
	/* for afm statis, not sure 0 or 1 */
	DCAM_AFM_INTREQ1,/* TODO: which afm interrupt to use */
	DCAM_AFL_TX_DONE,/* for afl statis */
	DCAM_PDAF_PATH_TX_DONE,/* for pdaf data */
	DCAM_VCH2_PATH_TX_DONE,/* for vch2 data */
	DCAM_VCH3_PATH_TX_DONE,/* for vch3 data */
};

/*
 * interested interrupt bits in DCAM1
 */
static const int _DCAM1_SEQUENCE[] = {
	DCAM_CAP_SOF,/* must */
	DCAM_SENSOR_EOF,/* TODO: why for flash */
	DCAM_NR3_TX_DONE,/* for 3dnr, before data path */
	DCAM_PREV_PATH_TX_DONE,/* for bin path */
	DCAM_FULL_PATH_TX_DONE,/* for full path */
	DCAM_AEM_TX_DONE,/* for aem statis */
	/* for afm statis, not sure 0 or 1 */
	DCAM_AFM_INTREQ1,/* TODO: which afm interrupt to use */
	DCAM_AFL_TX_DONE,/* for afl statis */
	DCAM_PDAF_PATH_TX_DONE,/* for pdaf data */
	DCAM_VCH2_PATH_TX_DONE,/* for vch2 data */
	DCAM_VCH3_PATH_TX_DONE,/* for vch3 data */
};

/*
 * interested interrupt bits in DCAM2
 */
static const int _DCAM2_SEQUENCE[] = {
	DCAM2_SENSOR_SOF,/* must */
	DCAM2_SENSOR_EOF,/* TODO: why for flash */
	DCAM2_FULL_PATH_TX_DONE,/* for path data */
};

/*
 * interested interrupt bits
 */
static const struct {
	size_t count;
	const int *bits;
} DCAM_SEQUENCES[DCAM_HW_CONTEXT_MAX] = {
	{
		ARRAY_SIZE(_DCAM0_SEQUENCE),
		_DCAM0_SEQUENCE,
	},
	{
		ARRAY_SIZE(_DCAM1_SEQUENCE),
		_DCAM1_SEQUENCE,
	},
	{
		ARRAY_SIZE(_DCAM2_SEQUENCE),
		_DCAM2_SEQUENCE,
	},
};

/*
 * report error back to adaptive layer
 */

static void dcamint_dump_iommu_regs(struct dcam_hw_context *dcam_hw_ctx, struct dcam_sw_context *sw_ctx)
{
	uint32_t reg = 0;
	uint32_t val[4];

	if (!dcam_hw_ctx || !sw_ctx) {
		pr_err("fail to get valid input hw_ctx or sw_ctx\n");
		return;
	}

	if (sw_ctx->err_count) {
		for (reg = 0; reg <= MMU_STS; reg += 16) {
			val[0] = DCAM_MMU_RD(reg);
			val[1] = DCAM_MMU_RD(reg + 4);
			val[2] = DCAM_MMU_RD(reg + 8);
			val[3] = DCAM_MMU_RD(reg + 12);
			pr_err("fail to handle,offset=0x%04x: %08x %08x %08x %08x\n",
					reg, val[0], val[1], val[2], val[3]);
		}

		pr_err("fail to handle,full %08x bin0 %08x bin1 %08x bin2 %08x "
		"bin3 %08x\n", DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_FULL_BASE_WADDR),
				DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_BIN_BASE_WADDR0),
				DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_BIN_BASE_WADDR1),
				DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_BIN_BASE_WADDR2),
				DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_BIN_BASE_WADDR3));
		pr_err("fail to handle,pdaf %08x vch2 %08x vch3 %08x lsc %08x aem %08x\n",
				DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_PDAF_BASE_WADDR),
				DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_VCH2_BASE_WADDR),
				DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_VCH3_BASE_WADDR),
				DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_LENS_BASE_RADDR),
				DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_AEM_BASE_WADDR));
		pr_err("fail to handle,afl %08x %08x bpc %08x %08x afm %08x "
		"nr3 %08x\n", DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, ISP_AFL_GLB_WADDR),
				DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, ISP_AFL_REGION_WADDR),
				DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, ISP_BPC_MAP_ADDR),
				DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, ISP_BPC_OUT_ADDR),
				DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, ISP_AFM_BASE_WADDR),
				DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, ISP_NR3_WADDR));
		sw_ctx->err_count -= 1;
	}
}

static irqreturn_t dcamint_error_handler(struct dcam_hw_context *dcam_hw_ctx, struct dcam_sw_context *sw_ctx,
			uint32_t status)
{
	const char *tb_ovr[2] = {"", ", overflow"};
	const char *tb_lne[2] = {"", ", line error"};
	const char *tb_frm[2] = {"", ", frame error"};
	const char *tb_mmu[2] = {"", ", mmu"};

	if (!sw_ctx) {
		pr_err("fail to get valid sw_ctx\n");
		return IRQ_HANDLED;
	}

	pr_err("fail to get normal status DCAM%u 0x%x%s%s%s%s\n", dcam_hw_ctx->hw_ctx_id, status,
		tb_ovr[!!(status & BIT(DCAM_DCAM_OVF))],
		tb_lne[!!(status & BIT(DCAM_CAP_LINE_ERR))],
		tb_frm[!!(status & BIT(DCAM_CAP_FRM_ERR))],
		tb_mmu[!!(status & BIT(DCAM_MMU_INT))]);

	if (status & BIT(DCAM_MMU_INT)) {
		uint32_t val = DCAM_MMU_RD(MMU_STS);

		if (val != sw_ctx->iommu_status) {
			dcamint_dump_iommu_regs(dcam_hw_ctx, sw_ctx);
			sw_ctx->iommu_status = val;
		}
	}

	if ((status & DCAMINT_FATAL_ERROR)
		&& (atomic_read(&sw_ctx->state) != STATE_ERROR)) {
		atomic_set(&sw_ctx->state, STATE_ERROR);
		sw_ctx->dcam_cb_func(DCAM_CB_DEV_ERR, sw_ctx, sw_ctx->cb_priv_data);
	}

	return IRQ_HANDLED;
}

/*
 * interrupt handler
 */
static irqreturn_t dcamint_isr_root(int irq, void *priv)
{
	struct dcam_hw_context *dcam_hw_ctx = (struct dcam_hw_context *)priv;
	uint32_t status = 0;
	uint32_t line_mask;
	unsigned int i = 0;
	struct dcam_sw_context *dcam_sw_ctx = dcam_hw_ctx->sw_ctx;

	if (unlikely(irq != dcam_hw_ctx->irq)) {
		pr_err("fail to get irq,DCAM%u irq %d mismatch %d\n",
			dcam_hw_ctx->hw_ctx_id, irq, dcam_hw_ctx->irq);
		DCAM_REG_WR(dcam_hw_ctx->hw_ctx_id, DCAM_INT_CLR, 0xFFFFFFFF);
		return IRQ_NONE;
	}
	if (!dcam_sw_ctx) {
		status = DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_INT_MASK);
		DCAM_REG_WR(dcam_hw_ctx->hw_ctx_id, DCAM_INT_CLR, status);
		return IRQ_HANDLED;
	}
	if (atomic_read(&dcam_sw_ctx->state) != STATE_RUNNING) {
		/* clear int */
		pr_warn_ratelimited("warning: DCAM%u ignore irq in NON-running, 0x%x\n",
			dcam_hw_ctx->hw_ctx_id, DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_INT_MASK));
		DCAM_REG_WR(dcam_hw_ctx->hw_ctx_id, DCAM_INT_CLR, 0xFFFFFFFF);
		return IRQ_NONE;
	}

	if (dcam_hw_ctx->hw_ctx_id != DCAM_HW_CONTEXT_2)
		line_mask = DCAMINT_IRQ_LINE_MASK;
	else
		line_mask = DCAM2INT_IRQ_LINE_MASK;

	status = DCAM_REG_RD(dcam_hw_ctx->hw_ctx_id, DCAM_INT_MASK);
	status = status & line_mask;
	if (unlikely(!status))
		return IRQ_NONE;

	DCAM_REG_WR(dcam_hw_ctx->hw_ctx_id, DCAM_INT_CLR, status);

	dcam_sw_ctx->in_irq_handler = 1;

	if (!dcam_sw_ctx->slowmotion_count)
		dcamint_dcam_int_record(dcam_hw_ctx->hw_ctx_id, status);

	if (unlikely(DCAMINT_ALL_ERROR & status)) {
		dcamint_error_handler(dcam_hw_ctx, dcam_sw_ctx, status);
		status &= (~DCAMINT_ALL_ERROR);
	}

	pr_debug("DCAM%u status=0x%x\n", dcam_hw_ctx->hw_ctx_id, status);
	for (i = 0; i < DCAM_SEQUENCES[dcam_hw_ctx->hw_ctx_id].count; i++) {
		int cur_int = DCAM_SEQUENCES[dcam_hw_ctx->hw_ctx_id].bits[i];

		if (status & BIT(cur_int)) {
			if (_DCAM_ISRS[dcam_hw_ctx->hw_ctx_id][cur_int]) {
				_DCAM_ISRS[dcam_hw_ctx->hw_ctx_id][cur_int](dcam_hw_ctx, dcam_sw_ctx);
				status &= ~dcam_hw_ctx->handled_bits;
				dcam_hw_ctx->handled_bits = 0;
			} else {
				pr_warn("warning: DCAM%u missing handler for int %d\n",
					dcam_hw_ctx->hw_ctx_id, cur_int);
			}
			status &= ~BIT(cur_int);
			if (!status)
				break;
		}
	}

	/* TODO ignore DCAM_AFM_INTREQ0 now */
	status &= ~BIT(DCAM_AFM_INTREQ0);
	if (unlikely(status))
		pr_warn("warning: DCAM%u unhandled int 0x%x\n", dcam_hw_ctx->hw_ctx_id, status);
	dcam_sw_ctx->in_irq_handler = 0;

	return IRQ_HANDLED;
}

/*
 * request irq each time we open a camera
 */
int dcam_int_irq_request(struct device *pdev, int irq, void *param)
{
	struct dcam_hw_context *dcam_hw_ctx = NULL;
	int ret = 0;

	if (unlikely(!pdev || !param || irq < 0)) {
		pr_err("fail to get valid param pdev %p, param %p, irq %d\n",
			pdev, param, irq);
		return -EINVAL;
	}

	dcam_hw_ctx = (struct dcam_hw_context *)param;
	dcam_hw_ctx->irq = irq;

	ret = devm_request_irq(pdev, dcam_hw_ctx->irq, dcamint_isr_root,
			IRQF_SHARED, dcam_dev_name[dcam_hw_ctx->hw_ctx_id], dcam_hw_ctx);
	if (ret < 0) {
		pr_err("fail to get irq,DCAM%u fail to install irq %d\n",
			dcam_hw_ctx->hw_ctx_id, dcam_hw_ctx->irq);
		return -EFAULT;
	}

	dcam_int_tracker_reset(dcam_hw_ctx->hw_ctx_id);

	return ret;
}

/*
 * free irq each time we close a camera
 */
void dcam_int_irq_free(struct device *pdev, void *param)
{
	struct dcam_hw_context *dcam_hw_ctx = NULL;

	if (unlikely(!pdev || !param)) {
		pr_err("fail to get valid param. pdev = %p, param =%p\n",
			pdev, param);
		return;
	}

	dcam_hw_ctx = (struct dcam_hw_context *)param;
	devm_free_irq(pdev, dcam_hw_ctx->irq, dcam_hw_ctx);
}

