// SPDX-License-Identifier: GPL-2.0
/*
 *
 * MMC software queue support based on command queue interfaces
 *
 * Copyright (C) 2021 Spreadtrum, Inc.
 * Author: Zhongwu Zhu <zhongwu.zhu@unisoc.com>
 */

#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/core.h>
#include <linux/module.h>
#include <uapi/linux/sched/types.h>
#include <linux/kthread.h>
#include "mmc_swcq.h"
#include <linux/sort.h>
#include <linux/proc_fs.h>
#include <linux/proc_ns.h>
#include <linux/fs.h>
#include <linux/seq_file.h>

#define SCHED_WORK(x) queue_work(system_unbound_wq, x)
#define SCHED_PUMP_WORK(x, t) queue_delayed_work(system_unbound_wq, x, t)


#define SWCQ_NUM_SLOTS	64
#define SWCQ_INVALID_TAG	SWCQ_NUM_SLOTS
/* Sleep when polling cmd13' for over 1ms */
#define CMD13_TMO_NS (1000 * 1000)

struct mmc_swcq *g_swcq;

/**************debug log*******************/

static inline void swcq_dump_host_regs(struct mmc_host *mmc)
{
	struct sdhci_host *host = (struct sdhci_host *)mmc->private;

	pr_notice("[CQ] %s.\n", __func__);
	if (host->ops->dump_vendor_regs)
		host->ops->dump_vendor_regs(host);
}

inline void __dbg_add_host_log(struct mmc_host *mmc, int type,
			int cmd, int arg, int cpu, unsigned long reserved, struct mmc_request *mrq)
{
	struct mmc_swcq *swcq = mmc->cqe_private;
	unsigned long long t, tn;
	unsigned long long nanosec_rem;
	static int last_cmd, last_arg, skip, last_type;
	int l_skip = 0;
	struct dbg_run_host_log  *dbg_run_host_log_dat;
	unsigned long flags;

	if (!swcq)
		return;
	/* only log emmc */
	if (!HOST_IS_EMMC_TYPE(mmc))
		return;

	spin_lock_irqsave(&swcq->log_lock, flags);
	dbg_run_host_log_dat = &swcq->cmd_history[0];
	t = sched_clock();

	switch (type) {
	case 0: /* normal - cmd */
		tn = t;
		nanosec_rem = do_div(t, 1000000000)/1000;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].time_sec = t;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].time_usec = nanosec_rem;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].type = type;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].cmd = cmd;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].arg = arg;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].blocks = mrq ?
		(mrq->data ? mrq->data->blocks : 0) : 0;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].skip = l_skip;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].mrq = mrq;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].tag = mrq ? mrq->tag : -1;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].pid = current->pid;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].qcnt =
			atomic_read(&swcq->qcnt);
		dbg_run_host_log_dat[swcq->dbg_host_cnt].cmdq_cnt =
			atomic_read(&swcq->cmdq_cnt);
		/*
		* flags =
		* work_on<<24 | hsq_running<<16 | enabled <<8 |pump_busy<<4
		* |busy<<2|timer_running<<1 | cmdq_mode
		*/
		dbg_run_host_log_dat[swcq->dbg_host_cnt].flags =
		atomic_read(&swcq->work_on) << 24 | swcq->hsq_running << 16
		| swcq->enabled << 8 | swcq->pump_busy << 4
		| atomic_read(&swcq->busy) << 2 | swcq->timer_running << 1
		| swcq->cmdq_mode;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].task_id_index =
		swcq->task_id_index;
		swcq->dbg_host_cnt++;
		if (swcq->dbg_host_cnt >= dbg_max_cnt)
			swcq->dbg_host_cnt = 0;
		break;
	case 1: /* normal -rsp */
		nanosec_rem = do_div(t, 1000000000)/1000;
		/*skip log if last cmd rsp are the same*/
		if (last_cmd == cmd &&
			last_arg == arg && cmd == 13) {
			skip++;
			if (swcq->dbg_host_cnt == 0)
				swcq->dbg_host_cnt = dbg_max_cnt;
			/*remove type = 0, command*/
			swcq->dbg_host_cnt--;
			break;
		}
		last_cmd = cmd;
		last_arg = arg;
		l_skip = skip;
		skip = 0;

		dbg_run_host_log_dat[swcq->dbg_host_cnt].time_sec = t;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].time_usec = nanosec_rem;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].type = type;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].cmd = cmd;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].arg = arg;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].blocks = mrq ?
		(mrq->data ? mrq->data->blocks : 0) : 0;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].skip = l_skip;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].mrq = mrq;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].tag = mrq ? mrq->tag : -1;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].pid = current->pid;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].qcnt =
			atomic_read(&swcq->qcnt);
		dbg_run_host_log_dat[swcq->dbg_host_cnt].cmdq_cnt =
			atomic_read(&swcq->cmdq_cnt);
		dbg_run_host_log_dat[swcq->dbg_host_cnt].flags =
		atomic_read(&swcq->work_on) << 24 | swcq->hsq_running << 16
		| swcq->enabled << 8 | swcq->pump_busy << 4
		| atomic_read(&swcq->busy) << 2 | swcq->timer_running << 1
		| swcq->cmdq_mode;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].task_id_index =
		swcq->task_id_index;
		swcq->dbg_host_cnt++;
		if (swcq->dbg_host_cnt >= dbg_max_cnt)
			swcq->dbg_host_cnt = 0;
		break;
	case 2:
	case 3:
	case 4:
	case 33:
	case 44:
	case 58:
	case 59:
	case 60:
		tn = t;
		nanosec_rem = do_div(t, 1000000000)/1000;
		/*skip log if last cmd rsp are the same*/
		if (last_type == type &&
			type == 58) {
			skip++;
			if (swcq->dbg_host_cnt == 0)
				swcq->dbg_host_cnt = dbg_max_cnt;
			/*remove type = 0, command*/
			swcq->dbg_host_cnt--;
			break;
		}
		last_type = type;
		l_skip = skip;
		skip = 0;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].time_sec = t;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].time_usec = nanosec_rem;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].type = type;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].cmd = cmd;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].arg = arg;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].blocks = mrq ?
		(mrq->data ? mrq->data->blocks : 0) : 0;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].skip = l_skip;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].mrq = mrq;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].tag = mrq ? mrq->tag : -1;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].pid = current->pid;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].qcnt =
			atomic_read(&swcq->qcnt);
		dbg_run_host_log_dat[swcq->dbg_host_cnt].cmdq_cnt =
			atomic_read(&swcq->cmdq_cnt);
		dbg_run_host_log_dat[swcq->dbg_host_cnt].flags =
		atomic_read(&swcq->work_on) << 24 | swcq->hsq_running << 16
		| swcq->enabled << 8 | swcq->pump_busy << 4
		| atomic_read(&swcq->busy) << 2 | swcq->timer_running << 1
		| swcq->cmdq_mode;
		dbg_run_host_log_dat[swcq->dbg_host_cnt].task_id_index =
		swcq->task_id_index;
		swcq->dbg_host_cnt++;
		if (swcq->dbg_host_cnt >= dbg_max_cnt)
			swcq->dbg_host_cnt = 0;
		break;

	default:
		break;
	}
	spin_unlock_irqrestore(&swcq->log_lock, flags);
}

/* all cases which except softirq of IO */
void dbg_add_host_log(struct mmc_host *mmc, int type,
		int cmd, int arg, struct mmc_request *mrq)
{
	__dbg_add_host_log(mmc, type, cmd, arg, -1, 0, mrq);
}
EXPORT_SYMBOL(dbg_add_host_log);

void dump_cmd_history(struct mmc_swcq *swcq)
{
	int i, j;

	if (!swcq)
		return;
	pr_err("==========dump cmd history[max:%d entries]==========\n", dbg_max_cnt);
	for (i = 0, j = swcq->dbg_host_cnt; i < dbg_max_cnt; i++, j++) {
		if (j >= dbg_max_cnt)
			j = 0;
		if (swcq->cmd_history[j].time_sec == 0)
			continue;

			pr_info("[%d] time_sec:%lld time_usec:%lld type:%d CMD%d arg:0x%x blk:%d"
			"skip:%d pid:%d mrq:0x%p qcnt:%d cmdq_cnt:%d flags:0x%x tsk_idx:0x%x",
			i, swcq->cmd_history[j].time_sec, swcq->cmd_history[j].time_usec,
			swcq->cmd_history[j].type, swcq->cmd_history[j].cmd,
			swcq->cmd_history[j].arg, swcq->cmd_history[j].blocks,
			swcq->cmd_history[j].skip, swcq->cmd_history[j].pid,
			swcq->cmd_history[j].mrq, swcq->cmd_history[j].qcnt,
			swcq->cmd_history[j].cmdq_cnt, swcq->cmd_history[j].flags,
			swcq->cmd_history[j].task_id_index);

	}
	pr_err("==========dump cmd history end==========:\n");

}

/**************debug log end******************/

static void mmc_swcq_retry_handler(struct work_struct *work)
{
	struct mmc_swcq *swcq = container_of(work, struct mmc_swcq, retry_work);
	struct mmc_host *mmc = swcq->mmc;

	mmc->ops->request(mmc, swcq->mrq);
}
/* add for emmc reset when error happen */
int emmc_resetting_when_cmdq;
static int mmc_reset_for_cmdq(struct mmc_swcq *swcq)
{
	struct mmc_host *mmc = swcq->mmc;
	int err, ret;
	unsigned long flags;

	spin_lock_irqsave(&swcq->lock, flags);
	emmc_resetting_when_cmdq = 1;
	spin_unlock_irqrestore(&swcq->lock, flags);
	err = mmc_hw_reset(mmc);
	/* Ensure we switch back to the correct partition */
	if (err != -EOPNOTSUPP && !swcq->hsq_running) {
		u8 part_config = mmc->card->ext_csd.part_config;

		part_config &= ~EXT_CSD_PART_CONFIG_ACC_MASK;
		/*  only enable cq at user */
		part_config |= 0;

		ret = mmc_switch(mmc->card, EXT_CSD_CMD_SET_NORMAL,
				EXT_CSD_PART_CONFIG, part_config,
				mmc->card->ext_csd.part_time);
		if (ret)
			return ret;

		/* enable cmdq at all partition */
		ret = mmc_cmdq_enable(mmc->card);
		if (ret)
			return ret;

		mmc->card->ext_csd.part_config = part_config;

	}
	spin_lock_irqsave(&swcq->lock, flags);
	emmc_resetting_when_cmdq = 0;
	spin_unlock_irqrestore(&swcq->lock, flags);
	return err;
}

static inline struct swcq_node *alloc_swcq_cmd_node(struct mmc_swcq *swcq)
{
	int i;
	unsigned long flags;

	spin_lock_irqsave(&swcq->cmd_node_lock, flags);
	for (i = 0; i < SWCQ_NUM_SLOTS; i++) {
		if (!atomic_read(&swcq->cmd_node_array[i].used)) {
			atomic_set(&swcq->cmd_node_array[i].used, 1);
			swcq->cmd_node_array[i].mrq = NULL;
			break;
		}
	}
	spin_unlock_irqrestore(&swcq->cmd_node_lock, flags);
	BUG_ON(i == SWCQ_NUM_SLOTS);

	return &swcq->cmd_node_array[i];
}

static inline void free_swcq_cmd_node(struct mmc_swcq *swcq, struct swcq_node *node)
{
	unsigned long flags;

	BUG_ON(!atomic_read(&node->used));

	spin_lock_irqsave(&swcq->cmd_node_lock, flags);
	atomic_set(&node->used, 0);
	spin_unlock_irqrestore(&swcq->cmd_node_lock, flags);

}

static inline struct swcq_node *alloc_swcq_data_node(struct mmc_swcq *swcq)
{
	int i;
	unsigned long flags;

	spin_lock_irqsave(&swcq->data_node_lock, flags);
	for (i = 0; i < EMMC_MAX_QUEUE_DEPTH; i++) {
		if (!atomic_read(&swcq->data_node_array[i].used)) {
			atomic_set(&swcq->data_node_array[i].used, 1);
			swcq->data_node_array[i].mrq = NULL;
			break;
		}
	}
	spin_unlock_irqrestore(&swcq->data_node_lock, flags);
	BUG_ON(i == EMMC_MAX_QUEUE_DEPTH);
	return &swcq->data_node_array[i];
}

static inline void free_swcq_data_node(struct mmc_swcq *swcq, struct swcq_node *node)
{
	unsigned long flags;

	BUG_ON(!atomic_read(&node->used));
	spin_lock_irqsave(&swcq->data_node_lock, flags);
	atomic_set(&node->used, 0);
	spin_unlock_irqrestore(&swcq->data_node_lock, flags);

}

static void mmc_enqueue_queue(struct mmc_swcq *swcq, struct mmc_request *mrq)
{
	unsigned long flags;
	struct swcq_node *node;

	if (mrq->cmd->opcode == MMC_EXECUTE_READ_TASK ||
		mrq->cmd->opcode == MMC_EXECUTE_WRITE_TASK) {
		node = alloc_swcq_data_node(swcq);
		node->mrq = mrq;
		//spin_lock_irqsave(&swcq->data_que_lock, flags);
		list_add_tail(&node->link, &swcq->data_que);
		//spin_unlock_irqrestore(&swcq->data_que_lock, flags);
	} else {
		node = alloc_swcq_cmd_node(swcq);
		node->mrq = mrq;
		spin_lock_irqsave(&swcq->cmd_que_lock, flags);
		//if (mrq->flags)
		//	list_add(&mrq->link, &swcq->cmd_que);
		//else
			list_add_tail(&node->link, &swcq->cmd_que);
		spin_unlock_irqrestore(&swcq->cmd_que_lock, flags);

	}
}

static void mmc_dequeue_queue(struct mmc_swcq *swcq, struct mmc_request *mrq)
{
	//unsigned long flags;

	if (mrq->cmd->opcode == MMC_EXECUTE_READ_TASK ||
		mrq->cmd->opcode == MMC_EXECUTE_WRITE_TASK) {
		//spin_lock_irqsave(&swcq->data_que_lock, flags);
		//list_del_init(&mrq->link);
		//spin_unlock_irqrestore(&swcq->data_que_lock, flags);
	}
}

static struct mmc_request *mmc_get_cmd_que(struct mmc_swcq *swcq)
{
	struct mmc_request *mrq = NULL;
	unsigned long flags;
	struct swcq_node *node;

	spin_lock_irqsave(&swcq->cmd_que_lock, flags);
	if (!list_empty(&swcq->cmd_que)) {
		node = list_first_entry(&swcq->cmd_que,
			struct swcq_node, link);
		list_del_init(&node->link);
		mrq = node->mrq;
		free_swcq_cmd_node(swcq, node);
	}
	spin_unlock_irqrestore(&swcq->cmd_que_lock, flags);

	return mrq;
}

static void mmc_clear_data_mrq_que_flag(struct mmc_swcq *swcq)
{
	unsigned int i;

	for (i = 0; i < swcq->cmdq_depth; i++)
		swcq->data_mrq_queued[i] = false;
}

static void mmc_clear_data_list(struct mmc_swcq *swcq)
{

	unsigned long flags;
	struct swcq_node *node = NULL;
	struct swcq_node *node_next = NULL;

	spin_lock_irqsave(&swcq->data_que_lock, flags);
	list_for_each_entry_safe(node, node_next, &swcq->data_que, link) {
		list_del_init(&node->link);
		free_swcq_data_node(swcq, node);
	}
	spin_unlock_irqrestore(&swcq->data_que_lock, flags);

	mmc_clear_data_mrq_que_flag(swcq);
}

static void mmc_restore_tasks(struct mmc_host *host)
{
	struct mmc_swcq *swcq = host->cqe_private;
	unsigned int task_id;
	unsigned int tasks;

	tasks = swcq->task_id_index;
	for (task_id = 0; task_id < swcq->cmdq_depth; task_id++) {
		if (tasks & 0x1) {
			mmc_enqueue_queue(swcq, swcq->cmdq_slot[task_id].ext_mrq);
			clear_bit(task_id, &swcq->task_id_index);
		}
		tasks >>= 1;
	}

}

static struct mmc_request *mmc_get_data_que(struct mmc_swcq *swcq)
{
	struct mmc_request *mrq = NULL;
	struct swcq_node *node = NULL;

	if (!list_empty(&swcq->data_que)) {
		node = list_first_entry(&swcq->data_que,
			struct swcq_node, link);
		mrq = node->mrq;
		list_del_init(&node->link);
		free_swcq_data_node(swcq, node);
	}

	return mrq;
}

static int mmc_blk_status_check(struct mmc_card *card, unsigned int *status)
{
	struct mmc_command cmd = {0};
	int err, retries = 3;

	cmd.opcode = MMC_SEND_STATUS;
	cmd.arg = card->rca << 16;
	cmd.flags = MMC_RSP_SPI_R2 | MMC_RSP_R1 | MMC_CMD_AC;
	err = mmc_wait_for_cmd(card->host, &cmd, retries);
	if (err == 0)
		*status = cmd.resp[0];
	else
		pr_err("%s: err %d\n", __func__, err);

	return err;
}

void mmc_do_check(struct mmc_host *host)
{
	struct mmc_swcq *swcq = host->cqe_private;

	memset(&swcq->que_cmd, 0, sizeof(struct mmc_command));
	memset(&swcq->que_mrq, 0, sizeof(struct mmc_request));
	swcq->que_cmd.opcode = MMC_SEND_STATUS;
	swcq->que_cmd.arg = swcq->mmc->card->rca << 16 | 1 << 15;
	swcq->que_cmd.flags = MMC_RSP_SPI_R2 | MMC_RSP_R1 | MMC_CMD_AC;
	swcq->que_cmd.data = NULL;
	swcq->que_mrq.cmd = &swcq->que_cmd;

	swcq->que_mrq.done = mmc_wait_cmdq_done;
	swcq->que_mrq.host = swcq->mmc;
	swcq->que_mrq.cmd->retries = 3;
	swcq->que_mrq.cmd->error = 0;
	swcq->que_mrq.cmd->mrq = &swcq->que_mrq;

	while (1) {
		host->ops->request_atomic(host, &swcq->que_mrq);

		/* add for emmc reset when error happen */
		if (swcq->que_mrq.cmd->error && !swcq->que_mrq.cmd->retries) {
	/* wait data irq handle done otherwice timing issue will happen  */
			msleep(2000);
			if (mmc_reset_for_cmdq(swcq)) {
				pr_notice("[CQ] reinit fail\n");
				WARN_ON(1);
			}
			mmc_clear_data_list(swcq);
			mmc_restore_tasks(host);
			atomic_set(&swcq->cq_wait_rdy, 0);
			atomic_set(&swcq->cq_rdy_cnt, 0);
		}

		if (!swcq->que_mrq.cmd->error ||
			!swcq->que_mrq.cmd->retries)
			break;

		pr_err("%s: req failed (CMD%u): %d, retrying...\n",
			 __func__,
			 swcq->que_mrq.cmd->opcode,
			 swcq->que_mrq.cmd->error);

		swcq->que_mrq.cmd->retries--;
		swcq->que_mrq.cmd->error = 0;
	};
}

static void mmc_prep_data_mrq(struct mmc_swcq *swcq,
	struct mmc_request *mrq)
{
	mrq->done = mmc_wait_cmdq_done;
	mrq->host = swcq->mmc;
	mrq->cmd->error = 0;
	mrq->cmd->mrq = mrq;
	mrq->cmd->data = mrq->data;
	mrq->data->error = 0;
	mrq->data->mrq = mrq;
	if (mrq->stop) {
		mrq->data->stop = mrq->stop;
		mrq->stop->error = 0;
		mrq->stop->mrq = mrq;
	} else {
		mrq->data->stop = NULL;
	}
}

#define CMD_ERRORS_EXCL_OOR						\
	(R1_ADDRESS_ERROR |	/* Misaligned address */		\
	 R1_BLOCK_LEN_ERROR |	/* Transferred block length incorrect */\
	 R1_WP_VIOLATION |	/* Tried to write to protected block */	\
	 R1_CARD_ECC_FAILED |	/* Card ECC failed */			\
	 R1_CC_ERROR |		/* Card controller error */		\
	 R1_ERROR)		/* General/unknown error */

#define CMD_ERRORS							\
	(CMD_ERRORS_EXCL_OOR |						\
	 R1_OUT_OF_RANGE)	/* Command argument out of range */	\

static void swcq_mmc_blk_eval_resp_error(struct mmc_blk_request *brq)
{
	u32 val;

	/*
	 * Per the SD specification(physical layer version 4.10)[1],
	 * section 4.3.3, it explicitly states that "When the last
	 * block of user area is read using CMD18, the host should
	 * ignore OUT_OF_RANGE error that may occur even the sequence
	 * is correct". And JESD84-B51 for eMMC also has a similar
	 * statement on section 6.8.3.
	 *
	 * Multiple block read/write could be done by either predefined
	 * method, namely CMD23, or open-ending mode. For open-ending mode,
	 * we should ignore the OUT_OF_RANGE error as it's normal behaviour.
	 *
	 * However the spec[1] doesn't tell us whether we should also
	 * ignore that for predefined method. But per the spec[1], section
	 * 4.15 Set Block Count Command, it says"If illegal block count
	 * is set, out of range error will be indicated during read/write
	 * operation (For example, data transfer is stopped at user area
	 * boundary)." In another word, we could expect a out of range error
	 * in the response for the following CMD18/25. And if argument of
	 * CMD23 + the argument of CMD18/25 exceed the max number of blocks,
	 * we could also expect to get a -ETIMEDOUT or any error number from
	 * the host drivers due to missing data response(for write)/data(for
	 * read), as the cards will stop the data transfer by itself per the
	 * spec. So we only need to check R1_OUT_OF_RANGE for open-ending mode.
	 */

	if (!brq->stop.error) {
		bool oor_with_open_end;
		/* If there is no error yet, check R1 response */

		val = brq->stop.resp[0] & CMD_ERRORS;
		oor_with_open_end = val & R1_OUT_OF_RANGE && !brq->mrq.sbc;

		if (val && !oor_with_open_end)
			brq->stop.error = -EIO;
	}
}

static inline bool swcq_mmc_blk_rq_error(struct mmc_blk_request *brq)
{
	swcq_mmc_blk_eval_resp_error(brq);

	return brq->sbc.error || brq->cmd.error || brq->stop.error ||
		brq->data.error || brq->cmd.resp[0] & CMD_ERRORS;
}

static bool swcq_mmc_blk_urgent_bkops_needed(struct mmc_queue *mq,
					struct mmc_queue_req *mqrq)
{
	return mmc_card_mmc(mq->card) && !mmc_host_is_spi(mq->card->host) &&
	       (mqrq->brq.cmd.resp[0] & R1_EXCEPTION_EVENT ||
		mqrq->brq.stop.resp[0] & R1_EXCEPTION_EVENT);
}

static inline bool swcq_mmc_cqe_dcmd_busy(struct mmc_queue *mq)
{
	/* Allow only 1 DCMD at a time */
	return mq->in_flight[MMC_ISSUE_DCMD];
}

static void swcq_mmc_cqe_check_busy(struct mmc_queue *mq)
{
	if ((mq->cqe_busy & MMC_CQE_DCMD_BUSY) && !swcq_mmc_cqe_dcmd_busy(mq))
		mq->cqe_busy &= ~MMC_CQE_DCMD_BUSY;

	mq->cqe_busy &= ~MMC_CQE_QUEUE_FULL;
}

static inline bool swcq_mmc_cqe_can_dcmd(struct mmc_host *host)
{
	return host->caps2 & MMC_CAP2_CQE_DCMD;
}

static enum mmc_issue_type swcq_mmc_cqe_issue_type(struct mmc_host *host,
					      struct request *req)
{
	switch (req_op(req)) {
	case REQ_OP_DRV_IN:
	case REQ_OP_DRV_OUT:
	case REQ_OP_DISCARD:
	case REQ_OP_SECURE_ERASE:
		return MMC_ISSUE_SYNC;
	case REQ_OP_FLUSH:
		return swcq_mmc_cqe_can_dcmd(host) ? MMC_ISSUE_DCMD : MMC_ISSUE_SYNC;
	default:
		return MMC_ISSUE_ASYNC;
	}
}

static enum mmc_issue_type swcq_mmc_issue_type(struct mmc_queue *mq, struct request *req)
{
	struct mmc_host *host = mq->card->host;

	if (mq->use_cqe && !host->hsq_enabled)
		return swcq_mmc_cqe_issue_type(host, req);

	if (req_op(req) == REQ_OP_READ || req_op(req) == REQ_OP_WRITE)
		return MMC_ISSUE_ASYNC;

	return MMC_ISSUE_SYNC;
}

static void swcq_mmc_blk_cqe_complete_rq(struct mmc_queue *mq, struct request *req)
{
	struct mmc_queue_req *mqrq = req_to_mmc_queue_req(req);
	struct mmc_request *mrq = &mqrq->brq.mrq;
	struct request_queue *q = req->q;
	struct mmc_host *host = mq->card->host;
	enum mmc_issue_type issue_type = swcq_mmc_issue_type(mq, req);
	unsigned long flags;
	bool put_card;
	int err;

	mmc_cqe_post_req(host, mrq);

	if (mrq->cmd && mrq->cmd->error)
		err = mrq->cmd->error;
	else if (mrq->data && mrq->data->error)
		err = mrq->data->error;
	else
		err = 0;

	if (err) {
		if (mqrq->retries++ < MMC_CQE_RETRIES)
			blk_mq_requeue_request(req, true);
		else
			blk_mq_end_request(req, BLK_STS_IOERR);
	} else if (mrq->data) {
		if (blk_update_request(req, BLK_STS_OK, mrq->data->bytes_xfered))
			blk_mq_requeue_request(req, true);
		else
			__blk_mq_end_request(req, BLK_STS_OK);
	} else {
		blk_mq_end_request(req, BLK_STS_OK);
	}

	spin_lock_irqsave(&mq->lock, flags);

	mq->in_flight[issue_type] -= 1;

	put_card = (mmc_tot_in_flight(mq) == 0);

	swcq_mmc_cqe_check_busy(mq);

	spin_unlock_irqrestore(&mq->lock, flags);

	if (!mq->cqe_busy)
		blk_mq_run_hw_queues(q, true);

	if (put_card)
		mmc_put_card(mq->card, &mq->ctx);
}

static inline int mmc_swcq_switch_cmdq(struct mmc_swcq *swcq, bool enable)
{
	int ret;
	struct mmc_host *mmc = swcq->mmc;
	struct mmc_card *card = mmc->card;

	if (enable)
		ret = mmc_cmdq_enable(card);
	else
		ret = mmc_cmdq_disable(card);

	return ret;
}

static inline bool check_need_cmdq(struct mmc_swcq *swcq)
{
	return swcq->cmdq_mode;
}

static void mmc_swcq_update_next_tag(struct mmc_swcq *swcq, int remains)
{
	struct swcq_slot *slot;
	int tag, found = SWCQ_INVALID_TAG;
	unsigned long long min = -1;

	/*
	 * If there are no remain requests in software queue, then set a invalid
	 * tag.
	 */
	if (!remains) {
		swcq->next_tag = SWCQ_INVALID_TAG;
		return;
	}

	/* Othersie we should iterate all slots to find a available tag. */
	for (tag = 0; tag < SWCQ_NUM_SLOTS; tag++) {
		slot = &swcq->slot[tag];
		if (slot->mrq) {
			if (slot->time < min) {
				min = slot->time;
				found = tag;
			}
		}
	}

	swcq->next_tag = found;
}

static struct mmc_request *prepare_cmdq_extmrq(struct mmc_swcq *swcq,
								struct mmc_request *mrq, int tag)
{
	struct mmc_request *mrq_ext;
	struct mmc_queue_req *mqrq = container_of(mrq, struct mmc_queue_req,
											brq.mrq);
	struct mmc_blk_request *brq = &mqrq->brq;
	struct request *req = mmc_queue_req_to_req(mqrq);
	struct mmc_host *mmc = swcq->mmc;
	struct mmc_card *card = mmc->card;
	struct mmc_blk_request *brq_new;
	int rt = RT_TYPE(req) ? 0 : IS_RT_CLASS_REQ(req);
	bool do_rel_wr, do_data_tag;
	u32 readcmd, writecmd;
	u32 req_flags = mrq->data->flags;

	do_rel_wr = req_flags & MMC_DATA_REL_WR;
	do_data_tag = req_flags & MMC_DATA_DAT_TAG;
	//swcq->cmdq_slot[tag].flags = rt;

	readcmd = MMC_EXECUTE_READ_TASK; //cmd46
	writecmd = MMC_EXECUTE_WRITE_TASK; //cmd47
	brq_new = &swcq->mqrq[tag].brq;

	brq_new->sbc.opcode = MMC_QUE_TASK_PARAMS;//cmd44
	brq_new->sbc.arg = brq->data.blocks |
		(do_rel_wr ? (1 << 31) : 0) |
		((rq_data_dir(req) == WRITE) ? 0 : (1 << 30)) |
		(do_data_tag ? (1 << 29) : 0) |
		(rt << 23) | (tag << 16);
	brq_new->sbc.flags = MMC_RSP_R1 | MMC_CMD_AC;
	brq_new->mrq.sbc = &brq_new->sbc;

	brq_new->cmd.opcode = MMC_QUE_TASK_ADDR;//cmd45
	brq_new->cmd.arg = blk_rq_pos(req);
	if (!mmc_card_blockaddr(card))
		brq_new->cmd.arg <<= 9;
	brq_new->cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;
	brq_new->mrq.host = mmc;
	brq_new->mrq.cmd = &brq_new->cmd;
	brq_new->mrq.data = NULL;
	brq_new->mrq.done = mmc_wait_cmdq_done;
	brq_new->mrq.cmd->error = 0;
	brq_new->mrq.cmd->mrq = &brq_new->mrq;
	brq_new->mrq.cmd->data = NULL;
	brq_new->mrq.sbc->error = 0;
	brq_new->mrq.sbc->mrq = &brq_new->mrq;
	mrq_ext = &brq_new->mrq;

	mrq->cmd->opcode = rq_data_dir(req) == READ ? readcmd : writecmd;
	mrq->cmd->arg = tag << 16;
	mrq->sbc = NULL;
	mrq->stop = NULL;

	BUG_ON(swcq->cmdq_slot[tag].mrq != NULL);
	WARN_ON(swcq->cmdq_slot[tag].ext_mrq != NULL);

	swcq->cmdq_slot[tag].mrq = mrq;
	swcq->cmdq_slot[tag].ext_mrq = mrq_ext;
	atomic_set(&swcq->cmdq_slot[tag].used, true);

	return mrq_ext;
}

static int mmc_get_cmdq_index(struct mmc_swcq *swcq)
{
	int i;

	for (i = 0; i < swcq->cmdq_depth; i++) {
		if (!atomic_read(&swcq->cmdq_slot[i].used))
			break;
	}
	return i;
}

static bool cmdq_is_idle(struct mmc_swcq *swcq)
{
	bool is_idle;
	unsigned long flags;

	spin_lock_irqsave(&swcq->lock, flags);
	is_idle = !atomic_read(&swcq->work_on)
		&& !atomic_read(&swcq->cmdq_cnt);
	swcq->waiting_for_cmdq_idle = !is_idle;
	spin_unlock_irqrestore(&swcq->lock, flags);

	return is_idle;
}

static bool hsq_is_idle(struct mmc_swcq *swcq)
{
	bool is_idle;
	unsigned long flags;

	spin_lock_irqsave(&swcq->lock, flags);
	is_idle = !swcq->mrq;
	swcq->waiting_for_hsq_idle = !is_idle;
	spin_unlock_irqrestore(&swcq->lock, flags);

	return is_idle;
}

static void mmc_swcq_pump_requests(struct mmc_swcq *swcq)
{
	struct mmc_host *mmc = swcq->mmc;
	struct swcq_slot *slot;
	unsigned long flags;
	int ret = 0, index = 0, remains = 0;
	struct mmc_request *mrq = NULL, *cmd_mrq = NULL;

	spin_lock_irqsave(&swcq->lock, flags);
	/* Make sure we are not already running a request now */
	if (swcq->mrq || swcq->pump_busy
		|| swcq->recovery_halt
		|| (emmc_resetting_when_cmdq == 1)) {
		if (swcq->pump_busy)
			dbg_add_host_log(swcq->mmc, 58, 0, swcq->pump_busy, 0);
		spin_unlock_irqrestore(&swcq->lock, flags);
		return;
	}

	swcq->pump_busy = true;
	/* Make sure there are remain requests need to pump */
	if (!atomic_read(&swcq->qcnt) || !swcq->enabled) {
		dbg_add_host_log(swcq->mmc, 59, 0, atomic_read(&swcq->qcnt)<<16 | swcq->enabled, 0);
		swcq->pump_busy = false;
		spin_unlock_irqrestore(&swcq->lock, flags);
		return;
	}

	if (!swcq->timer_running && swcq->cmdq_support) {
		swcq->timer_running = true;
		mod_timer(&swcq->check_timer, jiffies + msecs_to_jiffies(swcq->timeout));
	}

	if (check_need_cmdq(swcq)) {
		/*come from hsq interrupt handler context*/
		if (in_irq()) {
			SCHED_PUMP_WORK(&swcq->delayed_pump_work, 0);
			swcq->pump_busy = false;
			spin_unlock_irqrestore(&swcq->lock, flags);
			return;
		}

		if (!swcq->mmc->card->ext_csd.cmdq_en) {
			atomic_set(&swcq->busy, true);
			swcq->mode_need_change = false;
			spin_unlock_irqrestore(&swcq->lock, flags);
			wait_event(swcq->wait_hsq_idle,
			hsq_is_idle(swcq));
			ret = mmc_swcq_switch_cmdq(swcq, true);
			if (ret) {
				atomic_set(&swcq->busy, false);
				goto reset;
			}
			spin_lock_irqsave(&swcq->lock, flags);
			swcq->mode_need_change = true;
			atomic_set(&swcq->busy, false);
		}
		if (swcq->hsq_running)
			swcq->hsq_running = false;
again:
		index = mmc_get_cmdq_index(swcq);
		if (index == swcq->cmdq_depth) {
			pr_info("mmc0: %s %d ", __func__, __LINE__);
			swcq->pump_busy = false;
			spin_unlock_irqrestore(&swcq->lock, flags);
			return;
		}

		slot = &swcq->slot[swcq->next_tag];
		mrq = slot->mrq;

		dbg_add_host_log(swcq->mmc, 44, 0, 0, mrq);

		if (!mrq) {
			WARN_ON(1);
			swcq->pump_busy = false;
			spin_unlock_irqrestore(&swcq->lock, flags);
			return;
		}

		if ((mrq->data->flags & MMC_DATA_WRITE) || (mrq->data->flags & MMC_DATA_READ)) {
			if (mrq->data->blocks == 8)
				atomic_inc(&swcq->blocks_cnt8);
			else
				atomic_inc(&swcq->changes_cnt);
		}

		atomic_inc(&swcq->cmdq_cnt);
		atomic_dec(&swcq->qcnt);

		cmd_mrq = prepare_cmdq_extmrq(swcq, mrq, index);
		mmc_enqueue_queue(swcq, cmd_mrq);
		SCHED_WORK(&swcq->cmdq_work);
		remains = atomic_read(&swcq->qcnt);
		swcq->slot[swcq->next_tag].mrq = NULL;
		mmc_swcq_update_next_tag(swcq, remains);

		if (remains > 0)
			goto again;

		swcq->pump_busy = false;
		spin_unlock_irqrestore(&swcq->lock, flags);
		return;
	}
	/*no cmdq mode*/
	if (swcq->mmc->card->ext_csd.cmdq_en) {
		swcq->mode_need_change = false;
		spin_unlock_irqrestore(&swcq->lock, flags);
		wait_event(swcq->wait_cmdq_idle,
			cmdq_is_idle(swcq));
		ret = mmc_swcq_switch_cmdq(swcq, false);
		if (ret)
			goto reset;

		spin_lock_irqsave(&swcq->lock, flags);
		swcq->mode_need_change = true;
	}

	slot = &swcq->slot[swcq->next_tag];
	swcq->mrq = slot->mrq;

	dbg_add_host_log(swcq->mmc, 33, 0, 0, swcq->mrq);

	if (!swcq->hsq_running)
		swcq->hsq_running = true;

	if (!swcq->mrq) {
		WARN_ON(1);
		swcq->pump_busy = false;
		spin_unlock_irqrestore(&swcq->lock, flags);
		return;
	}
	atomic_dec(&swcq->qcnt);
	swcq->pump_busy = false;
	spin_unlock_irqrestore(&swcq->lock, flags);

	if (mmc->ops->request_atomic)
		ret = mmc->ops->request_atomic(mmc, swcq->mrq);
	else
		mmc->ops->request(mmc, swcq->mrq);

	/*
	 * If returning BUSY from request_atomic(), which means the card
	 * may be busy now, and we should change to non-atomic context to
	 * try again for this unusual case, to avoid time-consuming operations
	 * in the atomic context.
	 *
	 * Note: we just give a warning for other error cases, since the host
	 * driver will handle them.
	 */
	if (ret == -EBUSY)
		schedule_work(&swcq->retry_work);
	else
		WARN_ON_ONCE(ret);

	return;

reset:
	if (swcq->timer_running) {
		swcq->timer_running = false;
		del_timer(&swcq->check_timer);
	}

	swcq_dump_host_regs(mmc);
	pr_notice("[CQ] switch_cmdq err! do reset.\n");

	if (mmc_reset_for_cmdq(swcq)) {
		pr_notice("[CQ] reinit fail after switch_cmdq\n");
		WARN_ON(1);
	}
	spin_lock_irqsave(&swcq->lock, flags);
	if (atomic_read(&swcq->qcnt) > 0)
		SCHED_PUMP_WORK(&swcq->delayed_pump_work, 1);

	swcq->mode_need_change = true;
	swcq->pump_busy = false;
	spin_unlock_irqrestore(&swcq->lock, flags);

}

static inline void mmc_cmdq_post_request(struct mmc_swcq *swcq, int task_id)
{
	unsigned long flags;

	spin_lock_irqsave(&swcq->lock, flags);
	atomic_dec(&swcq->cmdq_cnt);
	dbg_add_host_log(swcq->mmc, 4, 0, 0, swcq->cmdq_slot[task_id].mrq);
	swcq->cmdq_slot[task_id].mrq = NULL;
	swcq->cmdq_slot[task_id].ext_mrq = NULL;
	atomic_set(&swcq->cmdq_slot[task_id].used, false);
	spin_unlock_irqrestore(&swcq->lock, flags);

}

static void mmc_swcq_post_request(struct mmc_swcq *swcq)
{
	unsigned long flags;
	int remains;

	spin_lock_irqsave(&swcq->lock, flags);
	remains = atomic_read(&swcq->qcnt);

	dbg_add_host_log(swcq->mmc, 3, 0, 0, swcq->mrq);
	swcq->mrq = NULL;

	if (swcq->waiting_for_hsq_idle) {
		swcq->waiting_for_hsq_idle = false;
		wake_up(&swcq->wait_hsq_idle);
	}

	/* Update the next available tag to be queued. */
	mmc_swcq_update_next_tag(swcq, remains);

	if (swcq->waiting_for_idle && !remains) {
		swcq->waiting_for_idle = false;
		wake_up(&swcq->wait_queue);
	}

	/* Do not pump new request in recovery mode. */
	if (swcq->recovery_halt) {
		spin_unlock_irqrestore(&swcq->lock, flags);
		return;
	}
	if (remains == 0)
		swcq->hsq_running = false;

	spin_unlock_irqrestore(&swcq->lock, flags);

	 /*
	  * Try to pump new request to host controller as fast as possible,
	  * after completing previous request.
	  */

	if (remains > 0)
		mmc_swcq_pump_requests(swcq);
	else {
		if (swcq->timer_running) {
			swcq->timer_running = false;
			del_timer(&swcq->check_timer);
		}
	}
}

static inline void swcq_mmc_blk_reset_success(struct mmc_blk_data *md, int type)
{
	md->reset_done &= ~type;
}

static inline void swcq_mmc_blk_rw_reset_success(struct mmc_queue *mq,
					    struct request *req)
{
	int type = rq_data_dir(req) == READ ? MMC_BLK_READ : MMC_BLK_WRITE;

	swcq_mmc_blk_reset_success(mq->blkdata, type);
}

static void mmc_blk_cmdq_end_request(struct mmc_request *mrq, int task_id)
{
	struct mmc_queue_req *mqrq =
	container_of(mrq, struct mmc_queue_req, brq.mrq);
	struct request *req = mmc_queue_req_to_req(mqrq);
	struct request_queue *q = req->q;
	struct mmc_queue *mq = q->queuedata;
	struct mmc_host *host = mq->card->host;
	struct mmc_swcq *swcq = host->cqe_private;

	if (swcq_mmc_blk_rq_error(&mqrq->brq) ||
	    swcq_mmc_blk_urgent_bkops_needed(mq, mqrq)) {
		swcq->recovery_cnt++;
	}

	mmc_cmdq_post_request(swcq, task_id);
	swcq_mmc_blk_rw_reset_success(mq, req);

	/*
	 * Block layer timeouts race with completions which means the normal
	 * completion path cannot be used during recovery.
	 */
	if (mq->in_recovery)
		swcq_mmc_blk_cqe_complete_rq(mq, req);
	else
		blk_mq_complete_request(req);
}

/*
 *	send stop command
 */
void mmc_do_stop(struct mmc_host *host)
{
	unsigned int timeout;
	struct mmc_swcq *swcq = host->cqe_private;

	memset(&swcq->que_cmd, 0, sizeof(struct mmc_command));
	memset(&swcq->que_mrq, 0, sizeof(struct mmc_request));
	swcq->que_cmd.opcode = MMC_STOP_TRANSMISSION;
	swcq->que_cmd.flags = MMC_RSP_SPI_R1B | MMC_RSP_R1B | MMC_CMD_AC;
	swcq->que_mrq.cmd = &swcq->que_cmd;

	swcq->que_mrq.done = mmc_wait_cmdq_done;
	swcq->que_mrq.host = host;
	swcq->que_mrq.cmd->retries = 3;
	swcq->que_mrq.cmd->error = 0;
	swcq->que_mrq.cmd->mrq = &swcq->que_mrq;

	while (1) {
		host->ops->request(host, &swcq->que_mrq);

		if (!swcq->need_polling) {
			timeout = wait_event_interruptible_timeout(
			swcq->cmdq_que, swcq->done_mrq, 1 * HZ);
			if (!timeout)
				pr_info("[CQ] cmd13 time out occurred!\n");
		}

		if (!swcq->que_mrq.cmd->error ||
			!swcq->que_mrq.cmd->retries)
			break;

		pr_err("%s: req failed (CMD%u): %d, retrying...\n",
			 __func__,
			 swcq->que_mrq.cmd->opcode,
			 swcq->que_mrq.cmd->error);

		swcq->que_mrq.cmd->retries--;
		swcq->que_mrq.cmd->error = 0;
	};
}

static int mmc_wait_transfer(struct mmc_host *host)
{
	u32 status;
	int err;
	unsigned long timeout;

	timeout = jiffies + msecs_to_jiffies(10 * 1000);
	do {
		err = mmc_blk_status_check(host->card, &status);
		if (err) {
			pr_notice("[CQ] check card status error = %d\n", err);
			return 1;
		}

		if ((R1_CURRENT_STATE(status) == R1_STATE_DATA) ||
			(R1_CURRENT_STATE(status) == R1_STATE_RCV))
			mmc_do_stop(host);

		if (time_after(jiffies, timeout)) {
			pr_err("%s: Card stuck in %d state! %s\n",
				mmc_hostname(host),
				R1_CURRENT_STATE(status), __func__);
			return 1;
		}
	} while (R1_CURRENT_STATE(status) != R1_STATE_TRAN);

	return 0;
}
/*
 *	check write
 */
static int mmc_check_write(struct mmc_host *host, struct mmc_request *mrq)
{
	int ret = 0;
	u32 status = 0;
	struct mmc_swcq *swcq = host->cqe_private;

	if (mrq->cmd->opcode == MMC_EXECUTE_WRITE_TASK) {
		//ret = mmc_blk_status_check(host->card, &status);
		if ((status & R1_WP_VIOLATION) || swcq->wp_error) {

			pr_notice("[%s]: err\n", __func__);
		} else if (R1_CURRENT_STATE(status) != R1_STATE_TRAN) {
			mmc_wait_transfer(host);
			mrq->data->error = 0;
			swcq->wp_error = 0;
			atomic_set(&swcq->cq_w, false);

		}

	}

	return ret;
}


static int mmc_discard_cmdq(struct mmc_swcq *swcq)
{
	struct mmc_host *host = swcq->mmc;

	memset(&swcq->deq_cmd, 0, sizeof(struct mmc_command));
	memset(&swcq->deq_mrq, 0, sizeof(struct mmc_request));

	swcq->deq_cmd.opcode = MMC_CMDQ_TASK_MGMT;
	swcq->deq_cmd.arg = 1;
	swcq->deq_cmd.flags = MMC_RSP_SPI_R2 | MMC_RSP_R1B | MMC_CMD_AC;
	swcq->deq_mrq.data = NULL;
	swcq->deq_mrq.cmd = &swcq->deq_cmd;

	swcq->deq_mrq.done = mmc_wait_cmdq_done;
	swcq->deq_mrq.host = host;
	swcq->deq_mrq.cmd->retries = 3;
	swcq->deq_mrq.cmd->error = 0;
	swcq->deq_mrq.cmd->mrq = &swcq->deq_mrq;

	while (1) {
		host->ops->request_atomic(host, &swcq->deq_mrq);

		if (!swcq->deq_mrq.cmd->error ||
			!swcq->deq_mrq.cmd->retries)
			break;

		pr_err("%s: req failed (CMD%u): %d, retrying...\n",
			 __func__,
			 swcq->deq_mrq.cmd->opcode,
			 swcq->deq_mrq.cmd->error);

		swcq->deq_mrq.cmd->retries--;
		swcq->deq_mrq.cmd->error = 0;
	};

	pr_notice("%s: CMDQ send distard (CMD48)\n", __func__);
	if (!swcq->deq_mrq.cmd->retries &&
		swcq->deq_mrq.cmd->error)
		return 1;
	else
		return 0;
}

static void mmc_pump_work(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct mmc_swcq *swcq = container_of(delayed_work,
						struct mmc_swcq, delayed_pump_work);


	mmc_swcq_pump_requests(swcq);

}

static void mmc_cmdq_work(struct work_struct *work)
{
	struct mmc_swcq *swcq = container_of(work, struct mmc_swcq, cmdq_work);
	struct mmc_host *host = swcq->mmc;
	struct mmc_request *cmd_mrq = NULL;
	struct mmc_request *data_mrq = NULL;
	struct mmc_request *done_mrq = NULL;
	unsigned int task_id, cmdq_cnt_chk, timeout;
	bool is_done = false;
	int ret;
	u64 chk_time = 0;
	unsigned long flags;

	//pr_notice("[CQ]mmc0 start cmdq work\n");
	spin_lock_irqsave(&swcq->lock, flags);
	if (atomic_read(&swcq->cmdq_cnt) == 0 || atomic_read(&swcq->work_on)) {
		spin_unlock_irqrestore(&swcq->lock, flags);
		return;
	}
	atomic_set(&swcq->work_on, true);
	swcq->worker_pid = current->pid;
	spin_unlock_irqrestore(&swcq->lock, flags);

	while (1) {
		/* End request stage 1/2 */
		if (atomic_read(&swcq->cq_rw)
		|| (atomic_read(&swcq->cmdq_cnt) <= 1)) {
			if (swcq->done_mrq) {
				done_mrq = swcq->done_mrq;
				swcq->done_mrq = NULL;
			}
		}

		if (done_mrq) {
			if (done_mrq->data->error || done_mrq->cmd->error) {
				emmc_resetting_when_cmdq = 1;
				swcq_dump_host_regs(host);
				if (mmc_wait_transfer(host)
				    || mmc_discard_cmdq(swcq)
				    || mmc_wait_transfer(host))
					goto reset_card;
				/*
				if (host->ops->execute_tuning) {
					err = host->ops->execute_tuning(host,
				MMC_SEND_TUNING_BLOCK_HS200);
					if (err) {
						pr_notice("[CQ] reinit fail\n");
						WARN_ON(1);
						goto reset_card;
					} else {
						pr_notice("[CQ] tuning pass\n");
						goto restore_task;
					}
				}*/
reset_card:
				if (mmc_reset_for_cmdq(swcq)) {
					pr_err("cmq reset err, trigger exception!\n");
					WARN_ON(1);
				}

				mmc_clear_data_list(swcq);
				atomic_set(&swcq->cq_rdy_cnt, 0);
				swcq->cur_rw_task = CQ_TASK_IDLE;
				task_id = (done_mrq->cmd->arg >> 16) & 0x1f;

				ret = host->ops->request_atomic(host,
					swcq->cmdq_slot[task_id].ext_mrq);

				atomic_set(&swcq->cq_wait_rdy, 1);

				done_mrq = NULL;
			}
			atomic_set(&swcq->cq_rw, false);

			if (done_mrq && !done_mrq->data->error
			&& !done_mrq->cmd->error && !atomic_read(&swcq->busy)) {
				task_id = (done_mrq->cmd->arg >> 16) & 0x1f;
				mmc_check_write(host, done_mrq);
				swcq->cur_rw_task = CQ_TASK_IDLE;
				is_done = true;

				if (atomic_read(&swcq->cq_tuning_now) == 1) {
					pr_notice("mmc0: mmc_restore_tasks\n");
					mmc_restore_tasks(host);
					atomic_set(&swcq->cq_tuning_now, 0);
				}
			}
		}
		/* Send Command 46/47 (DMA) */
		if (!atomic_read(&swcq->cq_rw) && !atomic_read(&swcq->busy)) {
			data_mrq = mmc_get_data_que(swcq);
			if (data_mrq) {
				WARN_ON(
			data_mrq->cmd->opcode != MMC_EXECUTE_WRITE_TASK
			&& data_mrq->cmd->opcode != MMC_EXECUTE_READ_TASK);

				if (data_mrq->cmd->opcode
				== MMC_EXECUTE_WRITE_TASK)
					atomic_set(&swcq->cq_w, true);

				atomic_set(&swcq->cq_rw, true);
				task_id = ((data_mrq->cmd->arg >> 16) & 0x1f);
				swcq->cur_rw_task = task_id;
				ret = host->ops->request_atomic(host, data_mrq);
				if (swcq->need_intr) {
					timeout = wait_event_interruptible_timeout(
					swcq->cmdq_que, swcq->done_mrq, 10 * HZ);
					if (!timeout)
						pr_info("[CQ] cmd46/47 time out occurred!\n");
				}
				atomic_dec(&swcq->cq_rdy_cnt);
				data_mrq = NULL;
			}
		}
		/* End request stage 2/2 */
		if (is_done) {
			task_id = (done_mrq->cmd->arg >> 16) & 0x1f;
			mmc_post_req(host, done_mrq, 0);
			mmc_blk_cmdq_end_request(done_mrq, task_id);
			done_mrq = NULL;
			is_done = false;
		}
		/* Send Command 44/45 */
		if (atomic_read(&swcq->cq_tuning_now) == 0
			&& atomic_read(&swcq->cq_rdy_cnt) == 0
			&& !atomic_read(&swcq->busy)) {

			cmd_mrq = mmc_get_cmd_que(swcq);
			while (cmd_mrq) {
				task_id = ((cmd_mrq->sbc->arg >> 16) & 0x1f);
				if (swcq->task_id_index & (1 << task_id)) {
					pr_err(
"[%s] BUG!!! task_id %d used, task_id_index 0x%08lx, cmdq_cnt = %d, cq_wait_rdy = %d\n",
					__func__, task_id, swcq->task_id_index,
					atomic_read(&swcq->cmdq_cnt),
					atomic_read(&swcq->cq_wait_rdy));
					BUG_ON(1);
				}

				set_bit(task_id, &swcq->task_id_index);
				ret = host->ops->request_atomic(host, cmd_mrq);

				/* add for emmc reset when error happen */
				if ((cmd_mrq->sbc && cmd_mrq->sbc->error)
				|| cmd_mrq->cmd->error) {
					swcq_dump_host_regs(host);
		/* wait data irq handle done otherwise timing issue happen*/
					msleep(2000);
					if (mmc_reset_for_cmdq(swcq)) {
						pr_notice("[CQ] reinit fail\n");
						WARN_ON(1);
					}
					mmc_clear_data_list(swcq);
					mmc_restore_tasks(host);
					atomic_set(&swcq->cq_wait_rdy, 0);
					atomic_set(&swcq->cq_rdy_cnt, 0);
				} else
					atomic_inc(&swcq->cq_wait_rdy);

				cmd_mrq = mmc_get_cmd_que(swcq);
			}
		}

		if (atomic_read(&swcq->cq_rw)) {
			/* wait for event to wakeup */
			/* wake up when new request arrived and dma done */
			cmdq_cnt_chk = atomic_read(&swcq->cmdq_cnt);
			timeout = wait_event_interruptible_timeout(swcq->cmdq_que,
				swcq->done_mrq ||
				(atomic_read(&swcq->cmdq_cnt) > cmdq_cnt_chk),
				10 * HZ);
			if (!timeout) {
				pr_info("%s:timeout,mrq(%p),chk(%d),cnt(%d)\n",
					__func__,
					swcq->done_mrq,
					cmdq_cnt_chk,
					atomic_read(&swcq->cmdq_cnt));
				pr_info("%s:timeout,rw(%d),wait(%d),rdy(%d)\n",
					__func__,
					atomic_read(&swcq->cq_rw),
					atomic_read(&swcq->cq_wait_rdy),
					atomic_read(&swcq->cq_rdy_cnt));
			}
			/* DMA time should not count in polling time */
			chk_time = 0;
		}

		/* Send Command 13' */
		if (atomic_read(&swcq->cq_wait_rdy) > 0
			&& atomic_read(&swcq->cq_rdy_cnt) == 0
			&& !atomic_read(&swcq->busy)) {
			if (!chk_time)
				/* set check time */
				chk_time = sched_clock();
			/* send cmd13' */
			mmc_do_check(host);
			if (atomic_read(&swcq->cq_rdy_cnt))
				/* clear when got ready task */
				chk_time = 0;
			else if (sched_clock() - chk_time > CMD13_TMO_NS)
				/* sleep when timeout */
				usleep_range(2000, 5000);
		}
		/* Sleep when nothing to do */
		spin_lock_irqsave(&swcq->lock, flags);
		if (atomic_read(&swcq->cmdq_cnt) == 0) {
			atomic_set(&swcq->work_on, false);
			if (swcq->waiting_for_cmdq_idle) {
				swcq->waiting_for_cmdq_idle = false;
				wake_up(&swcq->wait_cmdq_idle);
			}
			if (!atomic_read(&swcq->qcnt)
				&& !atomic_read(&swcq->cmdq_cnt)
				&& swcq->waiting_for_idle) {
				swcq->waiting_for_idle = false;
				wake_up(&swcq->wait_queue);
			}
			if (atomic_read(&swcq->qcnt) == 0 && swcq->timer_running &&
				atomic_read(&swcq->changes_cnt) == 0) {
				swcq->timer_running = false;
				del_timer(&swcq->check_timer);
			}
			if (atomic_read(&swcq->qcnt) > 0) {
				SCHED_PUMP_WORK(&swcq->delayed_pump_work, 0);
			}
			dbg_add_host_log(swcq->mmc, 60, 0, atomic_read(&swcq->qcnt), 0);
			spin_unlock_irqrestore(&swcq->lock, flags);
			break;

		}
		spin_unlock_irqrestore(&swcq->lock, flags);
	}
}

unsigned long not_ready_time;
void mmc_wait_cmdq_done(struct mmc_request *mrq)
{
	struct mmc_host *host = mrq->host;
	struct mmc_swcq *swcq = host->cqe_private;
	struct mmc_command *cmd = mrq->cmd;
	int done = 0, task_id;

	pr_debug("%s %s: cmd%d \n",
			__func__, mmc_hostname(host),
			cmd->opcode);

	if (cmd->opcode == MMC_SEND_STATUS ||
		cmd->opcode == MMC_STOP_TRANSMISSION ||
		cmd->opcode == MMC_CMDQ_TASK_MGMT) {
		/* do nothing */
	} else
		mmc_dequeue_queue(swcq, mrq);

	/* error - request done */
	if (cmd->error) {
		pr_info("%s: cmd%d arg:%x error:%d\n",
			mmc_hostname(host),
			cmd->opcode, cmd->arg,
			cmd->error);
		if ((cmd->opcode == MMC_EXECUTE_READ_TASK) ||
			(cmd->opcode == MMC_EXECUTE_WRITE_TASK)) {
			atomic_set(&swcq->cq_tuning_now, 1);
			goto clear_end;
		}
		goto request_end;
	}
	/* data error */
	if (mrq->data && mrq->data->error) {
		pr_info("%s: cmd%d arg:%x data error:%d\n",
			mmc_hostname(host),
			cmd->opcode, cmd->arg,
			mrq->data->error);
		atomic_set(&swcq->cq_tuning_now, 1);
		goto clear_end;
	}
	/* cmd13' - check queue ready & enqueue 46/47 */
	if ((cmd->opcode == MMC_SEND_STATUS) && (cmd->arg & (1 << 15))) {
		int i = 0;
		unsigned int resp = cmd->resp[0];

		pr_debug("CMD%d, resp = %d\n", cmd->opcode, resp);

		if (resp == 0) {
			/* if task not ready over 5s, reinit emmc */
			if (!not_ready_time)
				not_ready_time = jiffies;
			else if (time_after(jiffies, not_ready_time
			+ msecs_to_jiffies(5 * 1000))) {
				pr_info("mmc0: error: task not ready over 5s\n");
				//msleep(2000);
				if (mmc_reset_for_cmdq(swcq)) {
					pr_notice("[CQ] reinit fail\n");
					WARN_ON(1);
				}
				mmc_clear_data_list(swcq);
				mmc_restore_tasks(host);
				atomic_set(&swcq->cq_wait_rdy, 0);
				atomic_set(&swcq->cq_rdy_cnt, 0);
				not_ready_time = 0;

			}
			goto request_end;
		}
		not_ready_time = 0;
		do {
			if ((resp & 1) && (!swcq->data_mrq_queued[i])) {
				if (swcq->cur_rw_task == i) {
					resp >>= 1;
					i++;
					continue;
				}
				if (!swcq->cmdq_slot[i].mrq) {
					pr_info("%s: task %d not exist!,QSR:%x\n",
				mmc_hostname(host), i, cmd->resp[0]);
					pr_info("%s: task_idx:%08lx\n",
						mmc_hostname(host),
						swcq->task_id_index);
					pr_info("%s: cnt:%d,wait:%d,rdy:%d\n",
						mmc_hostname(host),
						atomic_read(&swcq->cmdq_cnt),
						atomic_read(&swcq->cq_wait_rdy),
						atomic_read(&swcq->cq_rdy_cnt));
					/* reset eMMC flow */
					cmd->error = (unsigned int)-ETIMEDOUT;
					cmd->retries = 0;
					goto request_end;
				}
				atomic_dec(&swcq->cq_wait_rdy);
				atomic_inc(&swcq->cq_rdy_cnt);
				mmc_prep_data_mrq(swcq, swcq->cmdq_slot[i].mrq);
				mmc_enqueue_queue(swcq, swcq->cmdq_slot[i].mrq);

				swcq->data_mrq_queued[i] = true;
			}
			resp >>= 1;
			i++;
		} while (resp && (i < swcq->cmdq_depth));
	}
	/* cmd46 - request done */
	if (cmd->opcode == MMC_EXECUTE_READ_TASK
		|| cmd->opcode == MMC_EXECUTE_WRITE_TASK)
		goto clear_end;

	goto request_end;

clear_end:
	task_id = ((cmd->arg >> 16) & 0x1f);
	clear_bit(task_id, &swcq->task_id_index);
	swcq->data_mrq_queued[task_id] = false;
	done = 1;
request_end:
	/* request done when next data transfer */
	if (done) {
		WARN_ON(cmd->opcode != 46 && cmd->opcode != 47);
		WARN_ON(swcq->done_mrq);
		swcq->done_mrq = mrq;
		wake_up_interruptible(&swcq->cmdq_que);
	}
}

/**
 * mmc_swcq_finalize_request - finalize one request if the request is done
 * @mmc: the host controller
 * @mrq: the request need to be finalized
 *
 * Return true if we finalized the corresponding request in software queue,
 * otherwise return false.
 */
bool mmc_swcq_finalize_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct mmc_swcq *swcq = mmc->cqe_private;
	unsigned long flags;

	spin_lock_irqsave(&swcq->lock, flags);
	if (swcq->enabled && (atomic_read(&swcq->work_on) ||
	atomic_read(&swcq->cmdq_cnt) || atomic_read(&swcq->busy))) {
		spin_unlock_irqrestore(&swcq->lock, flags);
		mmc_cqe_request_done(mmc, mrq);
		return true;
	}

	if (!swcq->enabled || !swcq->mrq || swcq->mrq != mrq) {
		spin_unlock_irqrestore(&swcq->lock, flags);
		return false;
	}

	/*
	 * Clear current completed slot request to make a room for new request.
	 */
	swcq->slot[swcq->next_tag].mrq = NULL;

	spin_unlock_irqrestore(&swcq->lock, flags);

	mmc_cqe_request_done(mmc, mrq);

	mmc_swcq_post_request(swcq);

	return true;
}
EXPORT_SYMBOL_GPL(mmc_swcq_finalize_request);

static bool mmc_swcq_is_busy(struct mmc_host *mmc)
{
	struct mmc_swcq *swcq = mmc->cqe_private;
	struct mmc_queue *mq = swcq->mq;
	bool busy;

	if (!swcq->cmdq_support || !swcq->cmdq_mode)
		busy = mq ? (mq->in_flight[MMC_ISSUE_ASYNC] > 3) : false;
	else
		busy = mq ? (mq->in_flight[MMC_ISSUE_ASYNC] >
			(swcq->cmdq_depth - 4)) : false;

	return busy;
}

#ifdef CONFIG_SPRD_DEBUG
static u32 recovery_print_time;
#endif
static void mmc_swcq_recovery_start(struct mmc_host *mmc)
{
	struct mmc_swcq *swcq = mmc->cqe_private;
	unsigned long flags;

	spin_lock_irqsave(&swcq->lock, flags);
	swcq->recovery_halt = true;
	swcq->mode_need_change = false;
	swcq->recovery_cnt++;
	spin_unlock_irqrestore(&swcq->lock, flags);
#ifdef CONFIG_SPRD_DEBUG
	if ((ktime_to_ms(ktime_get()) - recovery_print_time) > (10000ULL)) {
		pr_info("%s : recovery_cnt = %d\n", mmc_hostname(mmc), swcq->recovery_cnt);
		recovery_print_time = ktime_to_ms(ktime_get());
	}
#endif
}

static void mmc_swcq_recovery_finish(struct mmc_host *mmc)
{
	struct mmc_swcq *swcq = mmc->cqe_private;
	int remains;
	unsigned long flags;

	spin_lock_irqsave(&swcq->lock, flags);
	swcq->recovery_halt = false;
	swcq->mode_need_change = true;
	remains = atomic_read(&swcq->qcnt);
	spin_unlock_irqrestore(&swcq->lock, flags);

	/*
	 * Try to pump new request if there are request pending in software
	 * queue after finishing recovery.
	 */
	if (remains > 0)
		mmc_swcq_pump_requests(swcq);
}

static bool queue_flag;
static int mmc_swcq_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct mmc_queue_req *mqrq = container_of(mrq, struct mmc_queue_req,
											brq.mrq);
	struct request *req = mmc_queue_req_to_req(mqrq);
	struct request_queue *q = req->q;
	struct mmc_queue *mq = q->queuedata;
	struct mmc_swcq *swcq = mmc->cqe_private;
	int tag = mrq->tag;
	unsigned long flags;

	spin_lock_irqsave(&swcq->lock, flags);
	if (!swcq->enabled) {
		spin_unlock_irqrestore(&swcq->lock, flags);
		return -ESHUTDOWN;
	}

	if (!swcq->mq)
		swcq->mq = mq;

	if (!queue_flag) {
		blk_queue_flag_set(QUEUE_FLAG_SAME_FORCE, q);
		queue_flag = true;
	}

	/* Do not queue any new requests in recovery mode. */
	if (swcq->recovery_halt) {
		spin_unlock_irqrestore(&swcq->lock, flags);
		return -EBUSY;
	}

	swcq->slot[tag].mrq = mrq;
	swcq->slot[tag].time = sched_clock();

	/*
	 * Set the next tag as current request tag if no available
	 * next tag.
	 */

	if (swcq->next_tag == SWCQ_INVALID_TAG)
		swcq->next_tag = tag;

	atomic_inc(&swcq->qcnt);

	dbg_add_host_log(mmc, 2, 0, 0, mrq);

	spin_unlock_irqrestore(&swcq->lock, flags);
	mmc_swcq_pump_requests(swcq);

	return 0;
}

static void mmc_swcq_post_req(struct mmc_host *mmc, struct mmc_request *mrq)
{
	if (mmc->ops->post_req)
		mmc->ops->post_req(mmc, mrq, 0);
}

static bool mmc_swcq_queue_is_idle(struct mmc_swcq *swcq, int *ret)
{
	bool is_idle;
	unsigned long flags;

	spin_lock_irqsave(&swcq->lock, flags);
	is_idle = (!swcq->mrq && !atomic_read(&swcq->qcnt) && !atomic_read(&swcq->cmdq_cnt)
		  && !atomic_read(&swcq->work_on)) || swcq->recovery_halt;

	*ret = swcq->recovery_halt ? -EBUSY : 0;
	swcq->waiting_for_idle = !is_idle;
	spin_unlock_irqrestore(&swcq->lock, flags);

	return is_idle;
}

static int mmc_swcq_wait_for_idle(struct mmc_host *mmc)
{
	struct mmc_swcq *swcq = mmc->cqe_private;
	int ret;

	wait_event(swcq->wait_queue,
		   mmc_swcq_queue_is_idle(swcq, &ret));

	return ret;
}

static void mmc_swcq_disable(struct mmc_host *mmc)
{
	struct mmc_swcq *swcq = mmc->cqe_private;
	u32 timeout = 500;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&swcq->lock, flags);
	if (!swcq->enabled) {
		spin_unlock_irqrestore(&swcq->lock, flags);
		return;
	}

	spin_unlock_irqrestore(&swcq->lock, flags);

	ret = wait_event_timeout(swcq->wait_queue,
				 mmc_swcq_queue_is_idle(swcq, &ret),
				 msecs_to_jiffies(timeout));
	if (ret == 0) {
		pr_info("could not stop mmc software queue\n");
		return;
	}

	spin_lock_irqsave(&swcq->lock, flags);
	swcq->enabled = false;
	spin_unlock_irqrestore(&swcq->lock, flags);
}
/*mmc-card-init, cqe_enable, add card, mmc_init_queue */
static int mmc_swcq_enable(struct mmc_host *mmc, struct mmc_card *card)
{
	struct mmc_swcq *swcq = mmc->cqe_private;
	unsigned long flags;

	spin_lock_irqsave(&swcq->lock, flags);

	if (!swcq->initialized && card) {
		swcq->initialized = true;
		swcq->cmdq_depth = card->ext_csd.cmdq_depth;
		swcq->cmdq_support = card->ext_csd.cmdq_support;
		if (!swcq->cmdq_support)
			pr_info("%s : emmc not support CMDQ!\n", mmc_hostname(mmc));
		card->reenable_cmdq = false;
	}

	if (swcq->enabled) {
		spin_unlock_irqrestore(&swcq->lock, flags);
		return -EBUSY;
	}

	swcq->enabled = true;
	spin_unlock_irqrestore(&swcq->lock, flags);

	return 0;
}

static bool mmc_swcq_timeout(struct mmc_host *mmc, struct mmc_request *mrq,
								bool *recovery_needed)
{
	struct mmc_swcq *swcq = mmc->cqe_private;
	int tag = mrq->tag;
	unsigned long flags;
	bool timed_out = true;
	//struct sdhci_host *host = mmc_priv(mmc);

	spin_lock_irqsave(&swcq->lock, flags);
	//timed_out = slot->mrq == mrq;
	if (timed_out) {
		//slot->flags |= SWCQ_EXTERNAL_TIMEOUT;
		//swcq_recovery_needed(mmc, mrq, false);
		//*recovery_needed = swcq->recovery_halt;
		/*ignore cqe recovery*/
		*recovery_needed = false;
	}
	spin_unlock_irqrestore(&swcq->lock, flags);

	if (timed_out) {
		pr_err("%s: swcq: timeout for tag %d, mrq:0x%p\n",
		       mmc_hostname(mmc), tag, mrq);
		//sdhci_dumpregs(host);
		dump_cmd_history(swcq);
	}

	return timed_out;
}
static int my_cmp(const void *a, const void *b)
{
	struct swcq_check *slot1 = (struct swcq_check *)a;
	struct swcq_check *slot2 = (struct swcq_check *)b;

	if (slot1->blk_addr > slot2->blk_addr)
		return 1;
	else if (slot1->blk_addr < slot2->blk_addr)
		return -1;
	else
		return 0;
}

static bool pre_result;
static void check_cmdq_timer(struct timer_list *t)
{
	struct mmc_swcq *swcq;
	struct mmc_host *mmc;
	struct swcq_slot *slot;
	struct swcq_check *check_slot, *next_check_slot;
	unsigned long flags;
	struct mmc_request *mrq = NULL;
	int i, j;
	u32 expect_blk_addr, real_blk_addr;
	bool result = false;
	int reason = 0;
	int blocks_cnt8, pre_qcnt, pre_cmdqcnt, pre_mode;
	static u64 pre_checksum, cur_checksum;

	swcq = from_timer(swcq, t, check_timer);
	mmc = swcq->mmc;

	blocks_cnt8 = 0;
	pre_qcnt = atomic_read(&swcq->qcnt);
	pre_cmdqcnt = atomic_read(&swcq->cmdq_cnt);
	pre_mode = swcq->cmdq_mode;

	if (atomic_read(&swcq->qcnt) < 2 && !swcq->cmdq_mode) {
		reason = -1;
		goto out;
	}

	if (swcq->cmdq_mode) {
		/*from cmdq to hsq checking*/
		if (atomic_read(&swcq->changes_cnt) != 0) {
			reason = 1;
			result = false;
		} else {
			reason = 2;
			result = true;
		}
	} else {
		/*from hsq to cmdq checking*/
		memset(swcq->check_slot, 0, swcq->num_slots*sizeof(struct swcq_check));
		spin_lock_irqsave(&swcq->lock, flags);
		cur_checksum = 0;
		for (i = 0, j = 0; i < swcq->num_slots; i++) {
			slot = &swcq->slot[i];
			mrq = slot->mrq;
			if (mrq && mrq->data && (mrq->data->blocks == 8)) {
					blocks_cnt8++;
					swcq->check_slot[j].blk_addr = mrq->data->blk_addr;
					swcq->check_slot[j].blocks = mrq->data->blocks;
					cur_checksum += swcq->check_slot[j].blk_addr;
					j++;
			} else if (mrq) {
				result = false;
				reason = 6;
				spin_unlock_irqrestore(&swcq->lock, flags);
				goto out;
			}
		}
		spin_unlock_irqrestore(&swcq->lock, flags);

		if (blocks_cnt8 < 3) {
			result = false;
			reason = 3;
			goto out;
		}
		/* use checksum to judge if solt is unpumped.
		 * if yes, kee in hsq mode.
		 */
		if (pre_checksum == cur_checksum && cur_checksum != 0) {
			result = false;
			reason = 5;
		}

		pre_checksum = cur_checksum;
		if (reason == 5)
			goto out;

		sort(swcq->check_slot, blocks_cnt8,
				sizeof(struct swcq_check), my_cmp, NULL);

		for (i = 0; i < blocks_cnt8; i++) {
			check_slot = &swcq->check_slot[i];
			expect_blk_addr = check_slot->blk_addr + check_slot->blocks;
			if ((i + 1) < blocks_cnt8) {
				next_check_slot = &swcq->check_slot[i+1];
				real_blk_addr = next_check_slot->blk_addr;
				pr_debug("mmc0: blk_addr:%d blocks:%d, expect_next_addr:%d,"
				"real_next_addr:%d\n",
				check_slot->blk_addr, check_slot->blocks,
				expect_blk_addr, real_blk_addr);
				if (expect_blk_addr == real_blk_addr) {
					result = false;
					reason = 7;
					break;
				}
			} else {
				result = true;
				reason = 4;
				break;
			}
		}
	}

out:
	if (result && pre_result && swcq->mode_need_change)
		swcq->cmdq_mode = true;
	if (!result && !pre_result && swcq->mode_need_change)
		swcq->cmdq_mode = false;

	pre_result = result;

	if (pre_mode != swcq->cmdq_mode) {
		pr_info("mmc0 qcnt:%d cmdq_cnt:%d swcq->cmdq_mode: %s",
		pre_qcnt,
		pre_cmdqcnt,
		swcq->cmdq_mode ? "True" : "False");

		pr_info("mmc0 blocks_cnt8:%d changes_cnt: %d reason:%d",
			atomic_read(&swcq->blocks_cnt8), atomic_read(&swcq->changes_cnt), reason);
		swcq->debug1++;
	}

	atomic_set(&swcq->blocks_cnt8, 0);
	atomic_set(&swcq->changes_cnt, 0);
	mod_timer(&swcq->check_timer, jiffies + msecs_to_jiffies(swcq->timeout));

}

static int sprd_swcq_cmd_show(struct seq_file *m, void *v)
{
	int i, j;
	struct mmc_swcq *swcq = g_swcq;

	for (i = 0, j = swcq->dbg_host_cnt; i < dbg_max_cnt; i++, j++) {
		if (j >= dbg_max_cnt)
			j = 0;
		if (swcq->cmd_history[j].time_sec == 0)
			continue;
		if (swcq->cmd_history[j].type == 0) {
			seq_printf(m, "[%d] time_sec:%lld, time_usec:%lld CMD%d=> arg:0x%x, "
			"skip:%d pid:%d blocks:%d mrq:0x%p qcnt:%d cmdq_cnt:%d flags:0x%x"
			" task_id_index:0x%x\n",
			i, swcq->cmd_history[j].time_sec, swcq->cmd_history[j].time_usec,
			swcq->cmd_history[j].cmd, swcq->cmd_history[j].arg,
			swcq->cmd_history[j].skip, swcq->cmd_history[j].pid,
			swcq->cmd_history[j].blocks, swcq->cmd_history[j].mrq,
			swcq->cmd_history[j].qcnt, swcq->cmd_history[j].cmdq_cnt,
			swcq->cmd_history[j].flags, swcq->cmd_history[j].task_id_index);
		} else if (swcq->cmd_history[j].type == 1) {
			seq_printf(m, "[%d] time_sec:%lld time_usec:%lld CMD%d resp:0x%x, "
			"skip:%d pid:%d blocks:%d mrq:0x%p qcnt:%d cmdq_cnt:%d flags:0x%x"
			" task_id_index:0x%x\n",
			i, swcq->cmd_history[j].time_sec, swcq->cmd_history[j].time_usec,
			swcq->cmd_history[j].cmd, swcq->cmd_history[j].arg,
			swcq->cmd_history[j].skip, swcq->cmd_history[j].pid,
			swcq->cmd_history[j].blocks, swcq->cmd_history[j].mrq,
			swcq->cmd_history[j].qcnt, swcq->cmd_history[j].cmdq_cnt,
			swcq->cmd_history[j].flags, swcq->cmd_history[j].task_id_index);

		} else if (swcq->cmd_history[j].type == 2) {
			seq_printf(m, "[%d] time_sec:%lld, time_usec:%lld issue next:%d, "
			"skip:%d pid:%d mrq:0x%p blocks:%d addr=%d qcnt:%d cmdq_cnt:%d flags:0x%x"
			" task_id_index:0x%x\n",
			i, swcq->cmd_history[j].time_sec, swcq->cmd_history[j].time_usec,
			swcq->cmd_history[j].arg, swcq->cmd_history[j].skip,
			swcq->cmd_history[j].pid, swcq->cmd_history[j].mrq,
			swcq->cmd_history[j].blocks, swcq->cmd_history[j].blocks ?
			swcq->cmd_history[j].mrq->data->blk_addr : 0,
			swcq->cmd_history[j].qcnt, swcq->cmd_history[j].cmdq_cnt,
			swcq->cmd_history[j].flags, swcq->cmd_history[j].task_id_index);

		} else if (swcq->cmd_history[j].type == 3) {
			seq_printf(m, "[%d] time_sec:%lld time_usec:%lld finish next:%d, "
			"skip:%d pid:%d blocks:%d mrq:0x%p qcnt:%d cmdq_cnt:%d flags:0x%x"
			" task_id_index:0x%x\n",
			i, swcq->cmd_history[j].time_sec, swcq->cmd_history[j].time_usec,
			swcq->cmd_history[j].arg,
			swcq->cmd_history[j].skip, swcq->cmd_history[j].pid,
			swcq->cmd_history[j].blocks, swcq->cmd_history[j].mrq,
			swcq->cmd_history[j].qcnt, swcq->cmd_history[j].cmdq_cnt,
			swcq->cmd_history[j].flags, swcq->cmd_history[j].task_id_index);

		} else {
			seq_printf(m, "[%d] time_sec:%lld time_usec:%lld type:%d args:0x%x, "
			"skip:%d pid:%d blocks:%d mrq:0x%p qcnt:%d cmdq_cnt:%d flags:0x%x"
			" task_id_index:0x%x\n",
			i, swcq->cmd_history[j].time_sec, swcq->cmd_history[j].time_usec,
			swcq->cmd_history[j].type, swcq->cmd_history[j].arg,
			swcq->cmd_history[j].skip, swcq->cmd_history[j].pid,
			swcq->cmd_history[j].blocks, swcq->cmd_history[j].mrq,
			swcq->cmd_history[j].qcnt, swcq->cmd_history[j].cmdq_cnt,
			swcq->cmd_history[j].flags, swcq->cmd_history[j].task_id_index);

		}

	}

	return 0;
}

static int sprd_swcq_cmd_open(struct inode *inode, struct file *file)
{
	return single_open(file, sprd_swcq_cmd_show, inode->i_private);
}

static const struct file_operations swcq_cmd_fops = {
	.open = sprd_swcq_cmd_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int sprd_swcq_cmdqmode_show(struct seq_file *m, void *v)
{
	struct mmc_swcq *swcq = g_swcq;

	seq_printf(m, "cmdq_mode: %d\n", swcq->cmdq_mode);

	return 0;
}

static ssize_t sprd_swcq_cmdqmode_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *pos)
{
	struct mmc_swcq *swcq = g_swcq;
	char val;

	if (count > 0) {
		if (get_user(val, buffer))
			return -EFAULT;

		swcq->cmdq_mode = (val == '1') ? true : false;
	}

	return count;
}

static int sprd_swcq_cmdqmode_open(struct inode *inode, struct file *file)
{
	return single_open(file, sprd_swcq_cmdqmode_show, inode->i_private);
}

static const struct file_operations swcq_cmdqmode_fops = {
	.open = sprd_swcq_cmdqmode_open,
	.read = seq_read,
	.write = sprd_swcq_cmdqmode_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations *proc_fops_list[] = {
	&swcq_cmd_fops,
	&swcq_cmdqmode_fops,
};

static char * const sprd_emmc_node_info[] = {
	"cmd_history",
	"cmdq_mode"
};

int sprd_create_swcq_proc_init(void)
{
	#define PROC_MODE 0440
	struct proc_dir_entry *swcq_procdir;
	struct proc_dir_entry *prEntry;
	int i, node;

	swcq_procdir = proc_mkdir("emmc_debug", NULL);
	if (!swcq_procdir) {
		pr_err("%s: failed to create /proc/emmc_cmd_history\n",
			__func__);
		return -1;
	}

	node = ARRAY_SIZE(sprd_emmc_node_info);
	for (i = 0; i < node; i++) {
		prEntry = proc_create(sprd_emmc_node_info[i], PROC_MODE,
				      swcq_procdir, proc_fops_list[i]);
		if (!prEntry) {
			pr_err("%s,failed to create node: /proc/emmc_debug/%s\n",
				__func__, sprd_emmc_node_info[i]);
			return -1;
		}
	}

	return 0;
}

static struct mmc_cqe_ops mmc_swcq_ops = {
	.cqe_enable = mmc_swcq_enable,
	.cqe_disable = mmc_swcq_disable,
	.cqe_request = mmc_swcq_request,
	.cqe_post_req = mmc_swcq_post_req,
	.cqe_wait_for_idle = mmc_swcq_wait_for_idle,
	.cqe_recovery_start = mmc_swcq_recovery_start,
	.cqe_recovery_finish = mmc_swcq_recovery_finish,
	.cqe_timeout = mmc_swcq_timeout,
};

int mmc_swcq_init(struct mmc_swcq *swcq, struct mmc_host *mmc)
{
	int i;

	g_swcq = swcq;
	swcq->mmc = mmc;
	swcq->mmc->cqe_private = swcq;
	mmc_swcq_ops.android_kabi_reserved1 = (u64)mmc_swcq_is_busy;
	mmc->cqe_ops = &mmc_swcq_ops;
	queue_flag = false;

	swcq->num_slots = SWCQ_NUM_SLOTS;
	swcq->next_tag = SWCQ_INVALID_TAG;

	swcq->cmd_node_array = kzalloc(sizeof(struct swcq_node) *
	SWCQ_NUM_SLOTS, GFP_KERNEL);

	if (!swcq->cmd_node_array)
		return -ENOMEM;

	swcq->data_node_array = kzalloc(sizeof(struct swcq_node) *
	EMMC_MAX_QUEUE_DEPTH, GFP_KERNEL);

	if (!swcq->data_node_array)
		goto nomem_err1;

	swcq->slot = devm_kcalloc(mmc_dev(mmc), swcq->num_slots,
				 sizeof(struct swcq_slot), GFP_KERNEL);
	if (!swcq->slot)
		goto nomem_err2;

	swcq->check_slot = devm_kcalloc(mmc_dev(mmc), swcq->num_slots,
				 sizeof(struct swcq_check), GFP_KERNEL);
	if (!swcq->check_slot)
		goto nomem_err3;

	for (i = 0; i < EMMC_MAX_QUEUE_DEPTH; i++) {
		swcq->cmdq_slot[i].mrq = NULL;
		swcq->cmdq_slot[i].ext_mrq = NULL;
		atomic_set(&swcq->cmdq_slot[i].used, false);
	}

	INIT_WORK(&swcq->retry_work, mmc_swcq_retry_handler);
	INIT_WORK(&swcq->cmdq_work, mmc_cmdq_work);
	INIT_DELAYED_WORK(&swcq->delayed_pump_work, mmc_pump_work);

	init_waitqueue_head(&swcq->wait_queue);
	init_waitqueue_head(&swcq->wait_cmdq_idle);
	init_waitqueue_head(&swcq->wait_hsq_idle);
	init_waitqueue_head(&swcq->cmdq_que);
	atomic_set(&swcq->qcnt, 0);
	atomic_set(&swcq->blocks_cnt8, 0);
	atomic_set(&swcq->changes_cnt, 0);

	INIT_LIST_HEAD(&swcq->cmd_que);
	INIT_LIST_HEAD(&swcq->data_que);
	INIT_LIST_HEAD(&swcq->rq_list);
	spin_lock_init(&swcq->cmd_que_lock);
	spin_lock_init(&swcq->rqlist_lock);
	spin_lock_init(&swcq->data_que_lock);
	spin_lock_init(&swcq->lock);
	spin_lock_init(&swcq->log_lock);
	spin_lock_init(&swcq->cmd_node_lock);
	spin_lock_init(&swcq->data_node_lock);

	atomic_set(&swcq->cq_rw, false);
	atomic_set(&swcq->cq_w, false);
	atomic_set(&swcq->cq_wait_rdy, 0);
	swcq->task_id_index = 0;
	atomic_set(&swcq->is_data_dma, 0);
	swcq->cur_rw_task = CQ_TASK_IDLE;
	atomic_set(&swcq->cq_tuning_now, 0);
	atomic_set(&swcq->work_on, false);
	timer_setup(&swcq->check_timer, check_cmdq_timer, 0);

	for (i = 0; i < EMMC_MAX_QUEUE_DEPTH; i++)
		swcq->data_mrq_queued[i] = false;

	swcq->done_mrq = NULL;
	swcq->need_polling = false;
	swcq->need_intr = false;
	swcq->dbg_host_cnt = 0;
	swcq->num_slots = SWCQ_NUM_SLOTS;
	swcq->initialized = false;
	swcq->cmdq_mode = false;
	swcq->debug1 = 0;
	atomic_set(&swcq->cmdq_cnt, 0);
	swcq->timeout = 10;
	swcq->timer_running = false;
	swcq->mode_need_change = true;
	swcq->pump_busy = false;
	swcq->recovery_cnt = 0;
	sprd_create_swcq_proc_init();
	pr_notice("[notice] swcq init finish.\n");

	return 0;
nomem_err3:
	devm_kfree(mmc_dev(mmc), swcq->slot);
nomem_err2:
	kfree(swcq->data_node_array);
nomem_err1:
	kfree(swcq->cmd_node_array);
	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(mmc_swcq_init);

void mmc_swcq_suspend(struct mmc_host *mmc)
{
	struct mmc_swcq *swcq = mmc->cqe_private;

	if (swcq->timer_running) {
		swcq->timer_running = false;
		del_timer(&swcq->check_timer);
	}
	mmc_swcq_disable(mmc);
}
EXPORT_SYMBOL_GPL(mmc_swcq_suspend);

int mmc_swcq_resume(struct mmc_host *mmc)
{
	return mmc_swcq_enable(mmc, NULL);
}
EXPORT_SYMBOL_GPL(mmc_swcq_resume);

MODULE_DESCRIPTION("MMC Host Software Queue support");
MODULE_LICENSE("GPL v2");
