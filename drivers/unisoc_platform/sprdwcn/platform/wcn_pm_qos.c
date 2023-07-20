// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Spreadtrum Communications Inc.
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#include <linux/cpufreq.h>
#include <misc/wcn_bus.h>
#include "wcn_glb.h"
#include "wcn_gnss.h"
#include "wcn_procfs.h"
#include "wcn_pm_qos.h"

/* #define WCN_PM_QOS_TEST */

#ifdef WCN_PM_QOS_TEST
bool pm_qos_test = true;
#else
bool pm_qos_test;
#endif

static struct wcn_pm_qos *wpq;

struct wcn_pm_qos *wcn_pm_qos_get(void)
{
	return wpq;
}

static void wcn_pm_qos_cpu_pd_forbid(struct pm_qos_request *pm_qos)
{
	pm_qos_update_request(pm_qos, CPU_DMA_FORBID_POWER_DOWN_LATENCY);
}

static void wcn_pm_qos_cpu_pd_allow(struct pm_qos_request *pm_qos)
{
	pm_qos_update_request(pm_qos, PM_QOS_DEFAULT_VALUE);
}

static bool wcn_freq_pm_qos_swicth(unsigned long constraint, bool *cpu_pd_set)
{
	if (constraint != 0)
		*cpu_pd_set = true;

	if (test_bit(WIFI_TX_HIGH_THROUGHPUT, &constraint))
		return true;

	if (test_bit(WIFI_RX_HIGH_THROUGHPUT, &constraint))
		return true;

	if (test_bit(WIFI_AP, &constraint) && test_bit(BT_OPP, &constraint) &&
		test_bit(BT_A2DP, &constraint))
		return true;

	return false;
}

static int  wcn_pm_qos_request(unsigned long constraint_pending)
{
	struct wcn_pm_qos *pqos = wcn_pm_qos_get();
	bool freq_set = false, cpu_pd_set = false;
	int ret = 0;

	if (IS_ERR_OR_NULL(pqos))
		return -EINVAL;

	WCN_INFO("%s constraint=0x%lx, cond=%d,%d,%d, freq_set_flag=%d, cpu_pd_set_flag=%d\n",
		__func__, constraint_pending, pqos->cond.blank_cond, pqos->cond.cpu_pd_forbid,
		pqos->cond.cpu_freq_change, pqos->freq_set_flag, pqos->cpu_pd_set_flag);

	freq_set = wcn_freq_pm_qos_swicth(constraint_pending, &cpu_pd_set);
	WCN_DBG("%s freq_set=%d, freq_set_flag=%d\n", __func__, freq_set, pqos->freq_set_flag);

	if (pqos->cond.cpu_freq_change) {
		if (freq_set && pqos->freq_set_flag == false) {
			ret = freq_qos_update_request(&pqos->big_core_min_freq, pqos->max_freq);
			if (ret >= 0)
				pqos->freq_set_flag = true;
		} else if (!freq_set && pqos->freq_set_flag == true) {
			ret = freq_qos_update_request(&pqos->big_core_min_freq, pqos->min_freq);
			if (ret >= 0)
				pqos->freq_set_flag = false;
		} else
			WCN_DBG("Ignore set CPU frequency request\n");
	}

	if (pqos->cond.cpu_pd_forbid) {
		if (cpu_pd_set && pqos->cpu_pd_set_flag == false) {
			wcn_pm_qos_cpu_pd_forbid(&pqos->pm_qos_req);
			pqos->cpu_pd_set_flag = true;
		} else if (!cpu_pd_set && pqos->cpu_pd_set_flag == true) {
			wcn_pm_qos_cpu_pd_allow(&pqos->pm_qos_req);
			pqos->cpu_pd_set_flag = false;
		}
	}

	return ret;
}

void wcn_pm_qos_enable(void)
{
	struct wcn_pm_qos *pqos = wcn_pm_qos_get();
	int ret = 0;

	if (IS_ERR_OR_NULL(pqos) || !pqos->cond.blank_cond)
		return;

	if (!pqos->pm_qos_disable) {
		WCN_INFO("%s Already\n", __func__);
		return;
	}

	mutex_lock(&pqos->mutex);

	pqos->pm_qos_disable = false;

	if (pqos->active_modules_pending != pqos->active_modules) {
		WCN_INFO("%s: set pm_qos (%lx to %lx)\n", __func__,
				pqos->active_modules, pqos->active_modules_pending);
		ret = wcn_pm_qos_request(pqos->active_modules_pending);
	}

	if (ret >= 0)
		pqos->active_modules = pqos->active_modules_pending;
	else
		WCN_INFO("%s: failed to set pm_qos\n", __func__);

	mutex_unlock(&pqos->mutex);
}
EXPORT_SYMBOL_GPL(wcn_pm_qos_enable);

void wcn_pm_qos_disable(void)
{
	struct wcn_pm_qos *pqos = wcn_pm_qos_get();
	int ret = 0;

	if (IS_ERR_OR_NULL(pqos) || !pqos->cond.blank_cond)
		return;

	if (pqos->pm_qos_disable) {
		WCN_INFO("%s Already\n", __func__);
		return;
	}
	mutex_lock(&pqos->mutex);
	pqos->pm_qos_disable = true;

	/* Restore Default */
	ret = wcn_pm_qos_request(0);
	if (ret >= 0)
		pqos->active_modules = 0;
	else
		WCN_INFO("%s: failed to set pm_qos\n", __func__);

	mutex_unlock(&pqos->mutex);
}
EXPORT_SYMBOL_GPL(wcn_pm_qos_disable);

int wcn_pm_qos_condition_config(struct wcn_pm_qos_condition *cond)
{
	struct wcn_pm_qos *pqos = wcn_pm_qos_get();

	if (IS_ERR_OR_NULL(pqos))
		return -EINVAL;

	memcpy(&pqos->cond, cond, sizeof(struct wcn_pm_qos_condition));
	pqos->cond_set = true;
	if (cond->blank_cond) {
		WCN_INFO("%s default on screen, disable pm_qos\n", __func__);
		wcn_pm_qos_disable();
	}

	return 0;
}

int wcn_pm_qos_config_common(unsigned int mode, bool set)
{
	struct wcn_pm_qos *pqos = wcn_pm_qos_get();
	int ret = 0;

	if (IS_ERR_OR_NULL(pqos) || mode >= BT_MAX)
		return -EINVAL;

	mutex_lock(&pqos->mutex);

	if (set)
		set_bit(mode, &pqos->active_modules_pending);
	else
		clear_bit(mode, &pqos->active_modules_pending);

	if (pqos->cond.blank_cond && pqos->pm_qos_disable) {
		WCN_INFO("%s: conditions are not met, set later(0x%lx)\n", __func__,
				pqos->active_modules_pending);
		goto unlock;
	}

	if (pqos->active_modules_pending != pqos->active_modules)
		ret = wcn_pm_qos_request(pqos->active_modules_pending);

	if (ret >= 0)
		pqos->active_modules = pqos->active_modules_pending;
	else
		WCN_INFO("%s: failed to set pm_qos\n", __func__);
unlock:
	mutex_unlock(&pqos->mutex);

	return ret;
}

void wcn_pm_qos_reset(void)
{
	struct wcn_pm_qos *pqos = wcn_pm_qos_get();
	int ret = 0;

	if (IS_ERR_OR_NULL(pqos))
		return;

	mutex_lock(&pqos->mutex);

	pqos->active_modules = 0;
	pqos->active_modules_pending = 0;
	ret = wcn_pm_qos_request(0);
	WARN((ret < 0), "CPU freq cannot be recovered");

	mutex_unlock(&pqos->mutex);
}
EXPORT_SYMBOL_GPL(wcn_pm_qos_reset);

static int wcn_freq_pm_qos_init(void)
{
	struct wcn_pm_qos *pqos = wcn_pm_qos_get();
	int ret = 0;

	if (IS_ERR_OR_NULL(pqos))
		return -ENOMEM;

	pqos->policy = cpufreq_cpu_get(CPU_FREQ_BIG_CORE_INDEX);
	pqos->min_freq = pqos->policy->cpuinfo.min_freq;
	pqos->max_freq = pqos->policy->cpuinfo.max_freq;

	WCN_INFO("%s: CPU min_freq=%lu,max_freq=%lu\n", __func__,
		pqos->policy->cpuinfo.min_freq, pqos->policy->cpuinfo.max_freq);

	ret = freq_qos_add_request(&pqos->policy->constraints, &pqos->big_core_min_freq,
		FREQ_QOS_MIN, pqos->policy->cpuinfo.min_freq);

	cpufreq_cpu_put(pqos->policy);
	if (ret < 0) {
		WCN_INFO("freq_qos_add_request failed %d\n", ret);
		goto fail;
	}

	pqos->dev = get_cpu_device(CPU_FREQ_BIG_CORE_INDEX);
	if (unlikely(!pqos->dev)) {
		WCN_ERR("%s: No cpu device for cpu0\n", __func__);
		ret = -ENODEV;
		goto fail;
	}

	return 0;
fail:
	freq_qos_remove_request(&pqos->big_core_min_freq);
	return ret;
}

static void wcn_freq_pm_qos_exit(void)
{
	struct wcn_pm_qos *pqos = wcn_pm_qos_get();
	int ret = 0;

	if (IS_ERR_OR_NULL(pqos))
		return;

	ret = freq_qos_remove_request(&pqos->big_core_min_freq);
	if (ret < 0)
		WCN_INFO("freq_qos_remove_request failed\n");
}

static int wcn_pm_qos_test(void *arg)
{
	int i = 0;

	WCN_INFO("pm_qos_test kthread run\n");
	while (!kthread_should_stop()) {
		wcn_pm_qos_disable();
		sprdwcn_bus_pm_qos_set(WIFI_AP, true);
		sprdwcn_bus_pm_qos_set(BT_A2DP, true);
		sprdwcn_bus_pm_qos_set(BT_OPP, true);
		WCN_INFO("CPU%d freq: screen on, later set(down)\n", CPU_FREQ_BIG_CORE_INDEX);
		msleep(5000);
		WCN_INFO("CPU%d freq: screen off, now set(up)\n", CPU_FREQ_BIG_CORE_INDEX);
		wcn_pm_qos_enable();
		msleep(5000);
		WCN_INFO("CPU%d freq: screen on, restore default(down)\n", CPU_FREQ_BIG_CORE_INDEX);
		wcn_pm_qos_disable();
		msleep(5000);
		WCN_INFO("CPU%d freq: screen off, restore up(up)", CPU_FREQ_BIG_CORE_INDEX);
		wcn_pm_qos_enable();
		msleep(10000);

		WCN_INFO("CPU%d freq: screen off, restore up(down)\n", CPU_FREQ_BIG_CORE_INDEX);
		sprdwcn_bus_pm_qos_set(WIFI_AP, false);
		msleep(5000);
		WCN_INFO("CPU%d freq: screen on, restore default(down)\n", CPU_FREQ_BIG_CORE_INDEX);
		wcn_pm_qos_disable();
		msleep(5000);
		WCN_INFO("CPU%d freq: screen on, later set(down)\n", CPU_FREQ_BIG_CORE_INDEX);
		sprdwcn_bus_pm_qos_set(WIFI_TX_HIGH_THROUGHPUT, true);
		msleep(5000);
		WCN_INFO("CPU%d freq: screen off, now set(up)\n", CPU_FREQ_BIG_CORE_INDEX);
		wcn_pm_qos_enable();
		msleep(5000);
		WCN_INFO("CPU%d freq: screen on, restore default(down)\n", CPU_FREQ_BIG_CORE_INDEX);
		wcn_pm_qos_disable();
		msleep(5000);
		WCN_INFO("CPU%d freq: screen on, later set(down)\n", CPU_FREQ_BIG_CORE_INDEX);
		sprdwcn_bus_pm_qos_set(WIFI_TX_HIGH_THROUGHPUT, false);
		msleep(5000);
		WCN_INFO("CPU%d freq: screen off, now set(down)\n", CPU_FREQ_BIG_CORE_INDEX);
		wcn_pm_qos_enable();
		msleep(10000);

		WCN_INFO("CPU0 freq (down)\n");
		sprdwcn_bus_pm_qos_set(WIFI_TX_HIGH_THROUGHPUT, false);
		msleep(10000);
		WCN_INFO("CPU0 freq (up)\n");
		sprdwcn_bus_pm_qos_set(WIFI_RX_HIGH_THROUGHPUT, true);
		msleep(10000);
		sprdwcn_bus_pm_qos_set(WIFI_RX_HIGH_THROUGHPUT, false);
		sprdwcn_bus_pm_qos_set(BT_A2DP, false);
		sprdwcn_bus_pm_qos_set(BT_OPP, false);
		WCN_INFO("CPU0 freq (down)\n");
		msleep(10000);
		WCN_INFO("pm_qos test %d\n", i++);
	}

	return 0;
}

struct wcn_pm_qos_condition pm_qos_cond[] = {
	[0] = {false, true, false}, /* HW_TYPE_SDIO */
	[1] = {false, false, false}, /* HW_TYPE_PCIE */
	[2] = {true, false, true}, /* HW_TYPE_SIPC */
};

int wcn_pm_qos_init(void)
{
	struct wcn_pm_qos *pqos = NULL;
	int ret = 0;

	pqos = kvzalloc(sizeof(*pqos), GFP_KERNEL);
	if (pqos == NULL) {
		WCN_INFO("%s fail to malloc\n", __func__);
		return -ENOMEM;
	}
	wpq = pqos;
	mutex_init(&pqos->mutex);

	ret = sprdwcn_bus_get_hwintf_type();
	if (ret != HW_TYPE_INVALIED)
		wcn_pm_qos_condition_config(&pm_qos_cond[sprdwcn_bus_get_hwintf_type()]);

	pm_qos_add_request(&pqos->pm_qos_req, PM_QOS_CPU_DMA_LATENCY,
		PM_QOS_DEFAULT_VALUE);

	ret = wcn_freq_pm_qos_init();
	if (ret < 0)
		goto failed;
	if (pm_qos_test) {
		pqos->test_task = kthread_run(wcn_pm_qos_test, pqos, "WCN_PM_QOS_TEST");
		if (IS_ERR_OR_NULL(pqos->test_task)) {
			WCN_INFO("fail to kthread_run\n");
			goto freq_pm_qos_exit;
		}
	}

	return 0;

freq_pm_qos_exit:
	wcn_freq_pm_qos_exit();
failed:
	pm_qos_remove_request(&pqos->pm_qos_req);
	wpq = NULL;
	kzfree(pqos);
	return ret;
}

void wcn_pm_qos_exit(void)
{
	struct wcn_pm_qos *pqos = wcn_pm_qos_get();

	if (IS_ERR_OR_NULL(pqos))
		return;

	WCN_INFO("%s enter\n", __func__);
	if (!IS_ERR_OR_NULL(pqos->test_task))
		kthread_stop(pqos->test_task);

	wcn_freq_pm_qos_exit();

	pm_qos_remove_request(&pqos->pm_qos_req);
	mutex_destroy(&pqos->mutex);

	wpq = NULL;
	kzfree(pqos);
}
