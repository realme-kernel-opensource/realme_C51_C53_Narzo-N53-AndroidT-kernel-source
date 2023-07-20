// SPDX-License-Identifier: GPL-2.0-only
/*
 * Spreadtrum watchdog driver
 * Copyright (C) 2017 Spreadtrum - http://www.spreadtrum.com
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>

#define SPRD_WDT_LOAD_LOW		0x0
#define SPRD_WDT_LOAD_HIGH		0x4
#define SPRD_WDT_CTRL			0x8
#define SPRD_WDT_INT_CLR		0xc
#define SPRD_WDT_INT_RAW		0x10
#define SPRD_WDT_INT_MSK		0x14
#define SPRD_WDT_CNT_LOW		0x18
#define SPRD_WDT_CNT_HIGH		0x1c
#define SPRD_WDT_LOCK			0x20
#define SPRD_WDT_IRQ_LOAD_LOW		0x2c
#define SPRD_WDT_IRQ_LOAD_HIGH		0x30

/* WDT_CTRL */
#define SPRD_WDT_INT_EN_BIT		BIT(0)
#define SPRD_WDT_CNT_EN_BIT		BIT(1)
#define SPRD_WDT_NEW_VER_EN		BIT(2)
#define SPRD_WDT_RST_EN_BIT		BIT(3)

/* WDT_INT_CLR */
#define SPRD_WDT_INT_CLEAR_BIT		BIT(0)
#define SPRD_WDT_RST_CLEAR_BIT		BIT(3)

/* WDT_INT_RAW */
#define SPRD_WDT_INT_RAW_BIT		BIT(0)
#define SPRD_WDT_RST_RAW_BIT		BIT(3)
#define SPRD_WDT_LD_BUSY_BIT		BIT(4)

/* 1s equal to 32768 counter steps */
#define SPRD_WDT_CNT_STEP		32768

#define SPRD_WDT_UNLOCK_KEY		0xe551
#define SPRD_WDT_MIN_TIMEOUT		3
#define SPRD_WDT_MAX_TIMEOUT		60

#define SPRD_WDT_CNT_HIGH_SHIFT		16
#define SPRD_WDT_LOW_VALUE_MASK		GENMASK(15, 0)
#define SPRD_WDT_LOAD_TIMEOUT		2000
#define SPRD_WDTEN_MAGIC "e551"
#define SPRD_WDTEN_MAGIC_LEN_MAX  10

#define SPRD_DSWDTEN_MAGIC "enabled"
#define SPRD_DSWDTEN_MAGIC_LEN_MAX  10

#define SPRD_WDT_SLEEP_KICKTIME		540
#define SPRD_WDT_SLEEP_PRETIMEOUT	(600-570)
#define SPRD_WDT_SLEEP_TIMEOUT		600

struct sprd_wdt {
	void __iomem *base;
	struct watchdog_device wdd;
	struct clk *enable;
	struct clk *rtc_enable;
	struct alarm sleep_tmr;
	bool reset_en;
	bool sleep_en;
	int irq;
};

static bool sprd_dswdt_fiq_en(void)
{
	struct device_node *cmdline_node;
	const char *cmd_line, *dswdten_name_p;
	char dswdten_value[SPRD_DSWDTEN_MAGIC_LEN_MAX] = "NULL";
	int ret;

	cmdline_node = of_find_node_by_path("/chosen");
	ret = of_property_read_string(cmdline_node, "bootargs", &cmd_line);

	if (ret) {
		pr_err("can't not parse bootargs property\n");
		return false;
	}

	dswdten_name_p = strstr(cmd_line, "androidboot.dswdten=");
	if (!dswdten_name_p) {
		pr_err("can't find androidboot.dswdten\n");
		return false;
	}

	sscanf(dswdten_name_p, "androidboot.dswdten=%8s", dswdten_value);
	if (strncmp(dswdten_value, SPRD_DSWDTEN_MAGIC, strlen(SPRD_DSWDTEN_MAGIC)))
		return false;

	return true;
}

static bool sprd_wdt_en(void)
{
	struct device_node *cmdline_node;
	const char *cmd_line, *wdten_name_p;
	char wdten_value[SPRD_WDTEN_MAGIC_LEN_MAX] = "NULL";
	int ret;

	cmdline_node = of_find_node_by_path("/chosen");
	ret = of_property_read_string(cmdline_node, "bootargs", &cmd_line);

	if (ret) {
		pr_err("can't not parse bootargs property\n");
		return false;
	}

	wdten_name_p = strstr(cmd_line, "androidboot.wdten=");
	if (!wdten_name_p) {
		pr_err("can't find androidboot.wdten\n");
		return false;
	}

	sscanf(wdten_name_p, "androidboot.wdten=%8s", wdten_value);
	if (strncmp(wdten_value, SPRD_WDTEN_MAGIC, strlen(SPRD_WDTEN_MAGIC)))
		return false;

	return true;
}

static inline struct sprd_wdt *to_sprd_wdt(struct watchdog_device *wdd)
{
	return container_of(wdd, struct sprd_wdt, wdd);
}

static inline void sprd_wdt_lock(void __iomem *addr)
{
	writel_relaxed(0x0, addr + SPRD_WDT_LOCK);
}

static inline void sprd_wdt_unlock(void __iomem *addr)
{
	writel_relaxed(SPRD_WDT_UNLOCK_KEY, addr + SPRD_WDT_LOCK);
}

static irqreturn_t sprd_wdt_isr(int irq, void *dev_id)
{
	struct sprd_wdt *wdt = (struct sprd_wdt *)dev_id;

	sprd_wdt_unlock(wdt->base);
	writel_relaxed(SPRD_WDT_INT_CLEAR_BIT, wdt->base + SPRD_WDT_INT_CLR);
	sprd_wdt_lock(wdt->base);
	watchdog_notify_pretimeout(&wdt->wdd);
	return IRQ_HANDLED;
}

static u32 sprd_wdt_get_cnt_value(struct sprd_wdt *wdt)
{
	u32 val;

	val = readl_relaxed(wdt->base + SPRD_WDT_CNT_HIGH) <<
		SPRD_WDT_CNT_HIGH_SHIFT;
	val |= readl_relaxed(wdt->base + SPRD_WDT_CNT_LOW) &
		SPRD_WDT_LOW_VALUE_MASK;

	return val;
}

static int sprd_wdt_load_value(struct sprd_wdt *wdt, u32 timeout,
			       u32 pretimeout)
{
	u32 val, delay_cnt = 0;
	u32 tmr_step = timeout * SPRD_WDT_CNT_STEP;
	u32 prtmr_step = pretimeout * SPRD_WDT_CNT_STEP;

	/*
	 * Waiting the load value operation done,
	 * it needs two or three RTC clock cycles.
	 */
	do {
		val = readl_relaxed(wdt->base + SPRD_WDT_INT_RAW);
		if (!(val & SPRD_WDT_LD_BUSY_BIT))
			break;

		cpu_relax();
	} while (delay_cnt++ < SPRD_WDT_LOAD_TIMEOUT);

	if (delay_cnt >= SPRD_WDT_LOAD_TIMEOUT)
		return -EBUSY;

	sprd_wdt_unlock(wdt->base);
	writel_relaxed((tmr_step >> SPRD_WDT_CNT_HIGH_SHIFT) &
		       SPRD_WDT_LOW_VALUE_MASK, wdt->base + SPRD_WDT_LOAD_HIGH);
	writel_relaxed((tmr_step & SPRD_WDT_LOW_VALUE_MASK),
		       wdt->base + SPRD_WDT_LOAD_LOW);
	writel_relaxed((prtmr_step >> SPRD_WDT_CNT_HIGH_SHIFT) &
		       SPRD_WDT_LOW_VALUE_MASK, wdt->base + SPRD_WDT_IRQ_LOAD_HIGH);
	writel_relaxed(prtmr_step & SPRD_WDT_LOW_VALUE_MASK,
		       wdt->base + SPRD_WDT_IRQ_LOAD_LOW);
	sprd_wdt_lock(wdt->base);

	return 0;
}

static int sprd_wdt_enable(struct sprd_wdt *wdt)
{
	u32 val;
	int ret;

	ret = clk_prepare_enable(wdt->enable);
	if (ret)
		return ret;
	ret = clk_prepare_enable(wdt->rtc_enable);
	if (ret) {
		clk_disable_unprepare(wdt->enable);
		return ret;
	}

	sprd_wdt_unlock(wdt->base);
	val = readl_relaxed(wdt->base + SPRD_WDT_CTRL);
	val |= SPRD_WDT_NEW_VER_EN;
	writel_relaxed(val, wdt->base + SPRD_WDT_CTRL);
	sprd_wdt_lock(wdt->base);
	return 0;
}

static void sprd_wdt_disable(void *_data)
{
	struct sprd_wdt *wdt = _data;

	sprd_wdt_unlock(wdt->base);
	writel_relaxed(0x0, wdt->base + SPRD_WDT_CTRL);
	sprd_wdt_lock(wdt->base);

	clk_disable_unprepare(wdt->rtc_enable);
	clk_disable_unprepare(wdt->enable);
}

static int sprd_wdt_start(struct watchdog_device *wdd)
{
	struct sprd_wdt *wdt = to_sprd_wdt(wdd);
	u32 val;
	int ret;

	pr_err("ap watchdog sprd_wdt start: timeout = %d, pretimeout = %d\n",
	       wdd->timeout, wdd->pretimeout);
	ret = sprd_wdt_load_value(wdt, wdd->timeout, wdd->pretimeout);
	if (ret)
		return ret;

	sprd_wdt_unlock(wdt->base);
	val = readl_relaxed(wdt->base + SPRD_WDT_CTRL);
	if (wdt->reset_en)
		val |= SPRD_WDT_CNT_EN_BIT | SPRD_WDT_INT_EN_BIT |
			SPRD_WDT_RST_EN_BIT;
	else
		val |= SPRD_WDT_CNT_EN_BIT | SPRD_WDT_INT_EN_BIT;
	writel_relaxed(val, wdt->base + SPRD_WDT_CTRL);
	sprd_wdt_lock(wdt->base);
	set_bit(WDOG_HW_RUNNING, &wdd->status);

	return 0;
}

static int sprd_wdt_stop(struct watchdog_device *wdd)
{
	struct sprd_wdt *wdt = to_sprd_wdt(wdd);
	u32 val;

	sprd_wdt_unlock(wdt->base);
	val = readl_relaxed(wdt->base + SPRD_WDT_CTRL);
	val &= ~(SPRD_WDT_CNT_EN_BIT | SPRD_WDT_RST_EN_BIT |
		SPRD_WDT_INT_EN_BIT);
	writel_relaxed(val, wdt->base + SPRD_WDT_CTRL);
	sprd_wdt_lock(wdt->base);
	return 0;
}

static int sprd_wdt_set_timeout(struct watchdog_device *wdd,
				u32 timeout)
{
	struct sprd_wdt *wdt = to_sprd_wdt(wdd);

	if (timeout == wdd->timeout)
		return 0;

	wdd->timeout = timeout;

	return sprd_wdt_load_value(wdt, timeout, wdd->pretimeout);
}

static int sprd_wdt_set_pretimeout(struct watchdog_device *wdd,
				   u32 new_pretimeout)
{
	struct sprd_wdt *wdt = to_sprd_wdt(wdd);

	if (new_pretimeout < wdd->min_timeout)
		return -EINVAL;

	wdd->pretimeout = new_pretimeout;

	return sprd_wdt_load_value(wdt, wdd->timeout, new_pretimeout);
}

static u32 sprd_wdt_get_timeleft(struct watchdog_device *wdd)
{
	struct sprd_wdt *wdt = to_sprd_wdt(wdd);
	u32 val;

	val = sprd_wdt_get_cnt_value(wdt);
	return val / SPRD_WDT_CNT_STEP;
}

static int __maybe_unused sprd_wdt_alarm_prepare(struct device *dev)
{
	struct sprd_wdt *wdt = dev_get_drvdata(dev);
	ktime_t now, add;

	if (wdt->sleep_en) {
		if (watchdog_active(&wdt->wdd)) {
			now = ktime_get_boottime();
			add = ktime_set(SPRD_WDT_SLEEP_KICKTIME, 0);
			alarm_start(&wdt->sleep_tmr, ktime_add(now, add));
			pr_info("sprd_wdt:alarm start\n");
		}
	}
	return 0;
}

static void __maybe_unused sprd_wdt_alarm_complete(struct device *dev)
{
	struct sprd_wdt *wdt = dev_get_drvdata(dev);

	if (wdt->sleep_en) {
		if (watchdog_active(&wdt->wdd)) {
			alarm_cancel(&wdt->sleep_tmr);
			pr_info("sprd_wdt:alarm_cancel\n");
		}
	}
}

static const struct watchdog_ops sprd_wdt_ops = {
	.owner = THIS_MODULE,
	.start = sprd_wdt_start,
	.stop = sprd_wdt_stop,
	.set_timeout = sprd_wdt_set_timeout,
	.set_pretimeout = sprd_wdt_set_pretimeout,
	.get_timeleft = sprd_wdt_get_timeleft,
};

static const struct watchdog_info sprd_wdt_info = {
	.options = WDIOF_SETTIMEOUT |
		   WDIOF_PRETIMEOUT |
		   WDIOF_MAGICCLOSE |
		   WDIOF_KEEPALIVEPING,
	.identity = "Spreadtrum Watchdog Timer",
};

static enum alarmtimer_restart sprd_wdt_sleep_callback(struct alarm *p,
						       ktime_t t)
{
	pr_err("sprd_wdt: sprd wdt sleep callback\n");
	return ALARMTIMER_NORESTART;
}

static int sprd_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sprd_wdt *wdt;
	int ret;

	wdt = devm_kzalloc(dev, sizeof(*wdt), GFP_KERNEL);
	if (!wdt)
		return -ENOMEM;

	wdt->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(wdt->base))
		return PTR_ERR(wdt->base);

	wdt->enable = devm_clk_get(dev, "enable");
	if (IS_ERR(wdt->enable)) {
		dev_err(dev, "can't get the enable clock\n");
		return PTR_ERR(wdt->enable);
	}

	wdt->rtc_enable = devm_clk_get(dev, "rtc_enable");
	if (IS_ERR(wdt->rtc_enable)) {
		dev_err(dev, "can't get the rtc enable clock\n");
		return PTR_ERR(wdt->rtc_enable);
	}

	wdt->irq = platform_get_irq(pdev, 0);
	if (wdt->irq < 0)
		return wdt->irq;

	ret = devm_request_irq(dev, wdt->irq, sprd_wdt_isr, IRQF_NO_SUSPEND,
			       "sprd-wdt", (void *)wdt);
	if (ret) {
		dev_err(dev, "failed to register irq\n");
		return ret;
	}

	wdt->wdd.info = &sprd_wdt_info;
	wdt->wdd.ops = &sprd_wdt_ops;
	wdt->wdd.parent = dev;
	wdt->wdd.min_timeout = SPRD_WDT_MIN_TIMEOUT;
	wdt->wdd.max_timeout = SPRD_WDT_MAX_TIMEOUT;
	wdt->wdd.timeout = SPRD_WDT_MAX_TIMEOUT;

	wdt->sleep_en = sprd_dswdt_fiq_en();
	wdt->reset_en = sprd_wdt_en();
	ret = sprd_wdt_enable(wdt);
	if (ret) {
		dev_err(dev, "failed to enable wdt\n");
		return ret;
	}
	ret = devm_add_action_or_reset(dev, sprd_wdt_disable, wdt);
	if (ret) {
		sprd_wdt_disable(wdt);
		dev_err(dev, "Failed to add wdt disable action\n");
		return ret;
	}

	watchdog_set_nowayout(&wdt->wdd, WATCHDOG_NOWAYOUT);
	watchdog_init_timeout(&wdt->wdd, 0, dev);

	ret = devm_watchdog_register_device(dev, &wdt->wdd);
	if (ret) {
		sprd_wdt_disable(wdt);
		return ret;
	}

	if (wdt->sleep_en) {
		alarm_init(&wdt->sleep_tmr, ALARM_BOOTTIME,
			   sprd_wdt_sleep_callback);
	}

	platform_set_drvdata(pdev, wdt);

	return 0;
}

static int __maybe_unused sprd_wdt_pm_suspend(struct device *dev)
{
	struct sprd_wdt *wdt = dev_get_drvdata(dev);

	if (!wdt) {
		pr_info("sprd_wdt:wdt is null\n");
		return -ENODEV;
	}

	if (wdt->sleep_en) {
		if (watchdog_active(&wdt->wdd))
			sprd_wdt_load_value(wdt, SPRD_WDT_SLEEP_TIMEOUT, SPRD_WDT_SLEEP_PRETIMEOUT);
		else
			sprd_wdt_disable(wdt);
	} else {
		if (watchdog_active(&wdt->wdd))
			sprd_wdt_stop(&wdt->wdd);

		sprd_wdt_disable(wdt);
	}

	return 0;
}

static int __maybe_unused sprd_wdt_pm_resume(struct device *dev)
{
	struct sprd_wdt *wdt = dev_get_drvdata(dev);
	int ret = -ENODEV;

	if (!wdt) {
		pr_info("sprd_wdt:wdt is null\n");
		return -ENODEV;
	}

	if (wdt->sleep_en) {
		if (!watchdog_active(&wdt->wdd)) {
			ret = sprd_wdt_enable(wdt);
			if (ret) {
				pr_info("sprd_wdt:sprd_wdt_enable failed\n");
				return ret;
			}
		}
	} else {
		ret = sprd_wdt_enable(wdt);
		if (ret) {
			pr_info("sprd_wdt:sprd_wdt_enable failed\n");
			return ret;
		}
	}

	if (watchdog_active(&wdt->wdd)) {
		ret = sprd_wdt_start(&wdt->wdd);
		if (ret) {
			pr_info("sprd_wdt:sprd_wdt_start failed\n");
			return ret;
		}
	}

	return ret;
}

static const struct dev_pm_ops sprd_wdt_pm_ops = {
	.prepare = sprd_wdt_alarm_prepare,
	.complete = sprd_wdt_alarm_complete,
	SET_SYSTEM_SLEEP_PM_OPS(sprd_wdt_pm_suspend,
				sprd_wdt_pm_resume)
};

static const struct of_device_id sprd_wdt_match_table[] = {
	{ .compatible = "sprd,sp9860-wdt", },
	{ .compatible = "sprd,sharkl3-wdt", },
	{ .compatible = "sprd,pike2-wdt", },
	{},
};
MODULE_DEVICE_TABLE(of, sprd_wdt_match_table);

static struct platform_driver sprd_watchdog_driver = {
	.probe	= sprd_wdt_probe,
	.driver	= {
		.name = "sprd-wdt",
		.of_match_table = sprd_wdt_match_table,
		.pm = &sprd_wdt_pm_ops,
	},
};
module_platform_driver(sprd_watchdog_driver);

MODULE_AUTHOR("Eric Long <eric.long@spreadtrum.com>");
MODULE_DESCRIPTION("Spreadtrum Watchdog Timer Controller Driver");
MODULE_LICENSE("GPL v2");
