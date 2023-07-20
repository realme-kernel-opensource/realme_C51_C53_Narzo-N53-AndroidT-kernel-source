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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/notifier.h>

#include "sprd_camsys_domain.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "sprd_campd: %d %d %s : "\
	fmt, current->pid, __LINE__, __func__

struct camsys_power_info *pw_info;

static BLOCKING_NOTIFIER_HEAD(mmsys_chain);

int sprd_mm_pw_notify_register(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&mmsys_chain, nb);
}
EXPORT_SYMBOL(sprd_mm_pw_notify_register);

int sprd_mm_pw_notify_unregister(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&mmsys_chain, nb);
}
EXPORT_SYMBOL(sprd_mm_pw_notify_unregister);

static int mmsys_notifier_call_chain(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&mmsys_chain, val, v);
}

static int check_drv_init(struct camsys_power_info *pw_info)
{
	int ret = 0;

	if (!pw_info)
		ret = -1;
	if (atomic_read(&pw_info->inited) == 0)
		ret = -2;

	return ret;
}

int sprd_glb_mm_pw_on_cfg(void)
{
	int ret = 0;

	ret = check_drv_init(pw_info);
	if (ret) {
		pr_err("fail to check drv init. cb: %p, ret %d\n",
			__builtin_return_address(0), ret);
		return -ENODEV;
	}

	pr_info("power on state %d, cb %p\n", atomic_read(&pw_info->users_pw),
		__builtin_return_address(0));

	mutex_lock(&pw_info->mlock);
	if (atomic_inc_return(&pw_info->users_pw) == 1) {
		ret = pw_info->ops->sprd_cam_pw_on(pw_info);
		if (ret) {
			atomic_dec_return(&pw_info->users_pw);
			mutex_unlock(&pw_info->mlock);
			return ret;
		}

		ret = pw_info->ops->sprd_cam_domain_eb(pw_info);
		if (ret) {
			atomic_dec_return(&pw_info->users_pw);
			mutex_unlock(&pw_info->mlock);
			return ret;
		}

		mmsys_notifier_call_chain(_E_PW_ON, NULL);
	}
	mutex_unlock(&pw_info->mlock);

	return ret;
}
EXPORT_SYMBOL(sprd_glb_mm_pw_on_cfg);

int sprd_glb_mm_pw_off_cfg(void)
{
	int ret = 0;

	ret = check_drv_init(pw_info);
	if (ret) {
		pr_err("fail to check drv init. cb: %p, ret %d\n",
			__builtin_return_address(0), ret);
		return -ENODEV;
	}

	pr_info("power off state %d, cb %p\n", atomic_read(&pw_info->users_pw),
		__builtin_return_address(0));

	mutex_lock(&pw_info->mlock);
	if (atomic_dec_return(&pw_info->users_pw) == 0) {
		mmsys_notifier_call_chain(_E_PW_OFF, NULL);
		ret = pw_info->ops->sprd_cam_domain_disable(pw_info);
		if (ret) {
			mutex_unlock(&pw_info->mlock);
			return ret;
		}

		ret = pw_info->ops->sprd_cam_pw_off(pw_info);
		if (ret) {
			mutex_unlock(&pw_info->mlock);
			return ret;
		}
	}
	mutex_unlock(&pw_info->mlock);

	return ret;
}
EXPORT_SYMBOL(sprd_glb_mm_pw_off_cfg);

static int sprd_cam_pw_on(struct generic_pm_domain *domain)
{
	return 0;
}

static int sprd_cam_pw_off(struct generic_pm_domain *domain)
{
	return 0;
}

static const struct of_device_id sprd_campw_match_table[] = {
	{ .compatible = "sprd,pike2-camsys-domain",
	   .data = (void *)(&camsys_power_ops_pike2)},

	{ .compatible = "sprd,sharkl3-camsys-domain",
	   .data = (void *)(&camsys_power_ops_l3)},

	{ .compatible = "sprd,sharkl5pro-camsys-domain",
	   .data = (void *)(&camsys_power_ops_l5pro)},

	{ .compatible = "sprd,sharkle-camsys-domain",
	   .data = (void *)(&camsys_power_ops_le)},

	{ .compatible = "sprd,qogirl6-camsys-domain",
	   .data = (void *)(&camsys_power_ops_qogirl6)},

	{ .compatible = "sprd,qogirn6pro-camsys-domain",
	   .data = (void *)(&camsys_power_ops_qogirn6pro)},

	{ .compatible = "sprd,sharkl5-camsys-domain",
	   .data = (void *)(&camsys_power_ops_l5)},

	{ .compatible = "sprd,qogirl6l-camsys-domain",
	   .data = (void *)(&camsys_power_ops_qogirn6l)},

	{},
};

static int sprd_campw_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct camsys_power_ops *ops = NULL;

	pw_info = devm_kzalloc(&pdev->dev, sizeof(*pw_info), GFP_KERNEL);
	if (IS_ERR_OR_NULL(pw_info)) {
		pr_err("fail to alloc pw_info\n");
		return -ENOMEM;
	}

	ops = (struct camsys_power_ops *)
		((of_match_node(sprd_campw_match_table, np))->data);
	if (IS_ERR_OR_NULL(ops)) {
		pr_err("fail to parse sprd_campw_match_table item\n");
		return -ENOENT;
	}

	pw_info->ops = ops;
	pw_info->ops->sprd_campw_init(pdev, pw_info);
	pw_info->pd.name = kstrdup(np->name, GFP_KERNEL);
	pw_info->pd.power_off = sprd_cam_pw_off;
	pw_info->pd.power_on = sprd_cam_pw_on;
	pdev->dev.platform_data = (void *)pw_info;
	pm_genpd_init(&pw_info->pd, NULL, true);
	of_genpd_add_provider_simple(np, &pw_info->pd);
	mutex_init(&pw_info->mlock);
	atomic_set(&pw_info->inited, 1);

	return 0;
}

static int sprd_campw_remove(struct platform_device *pdev)
{
	struct camsys_power_info *pw_info;
	int ret = 0;

	pw_info = pdev->dev.platform_data;
	ret = check_drv_init(pw_info);
	if (ret) {
		pr_err("fail to check drv init: cb: %p, ret %d\n",
			__builtin_return_address(0), ret);
		return -ENODEV;
	}

	pm_genpd_remove(&pw_info->pd);
	devm_kfree(&pdev->dev, pw_info);

	return 0;
}

static struct platform_driver sprd_campw_driver = {
	.probe = sprd_campw_probe,
	.remove = sprd_campw_remove,
	.driver = {
		.name = "sprd_campd",
		.of_match_table = sprd_campw_match_table,
	},
};
module_platform_driver(sprd_campw_driver);

MODULE_DESCRIPTION("Camsys Power Domain Driver");
MODULE_AUTHOR("hongjian.wang@unisoc.com");
MODULE_LICENSE("GPL v2");
