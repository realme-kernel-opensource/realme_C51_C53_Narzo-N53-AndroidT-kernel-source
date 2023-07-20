/*
 * Driver for the FAIRCHILD fan54015 charger.
 * Author: Mark A. Greer <mgreer@animalcreek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/alarmtimer.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeup.h>
#include <linux/power_supply.h>
#include <linux/power/charger-manager.h>
#include <linux/power/sprd_battery_info.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/slab.h>

#define FAN54015_REG_0					0x0
#define FAN54015_REG_1					0x1
#define FAN54015_REG_2					0x2
#define FAN54015_REG_3					0x3
#define FAN54015_REG_4					0x4
#define FAN54015_REG_5					0x5
#define FAN54015_REG_6					0x6
#define FAN54015_REG_10					0x10

#define FAN54015_BATTERY_NAME				"sc27xx-fgu"
#define BIT_DP_DM_BC_ENB				BIT(0)
#define FAN54015_OTG_VALID_MS				500
#define FAN54015_FEED_WATCHDOG_VALID_MS			50
#define FAN54015_WDG_TIMER_S				15

#define FAN54015_REG_FAULT_MASK				0x7
#define FAN54015_OTG_TIMER_FAULT			0x6
#define FAN54015_REG_HZ_MODE_MASK			GENMASK(1, 1)
#define FAN54015_REG_OPA_MODE_MASK			GENMASK(0, 0)

#define FAN54015_REG_SAFETY_VOL_MASK			GENMASK(3, 0)
#define FAN54015_REG_SAFETY_CUR_MASK			GENMASK(6, 4)

#define FAN54015_REG_RESET_MASK				GENMASK(7, 7)
#define FAN54015_REG_RESET				BIT(7)

#define FAN54015_REG_WEAK_VOL_THRESHOLD_MASK		GENMASK(5, 4)

#define FAN54015_REG_IO_LEVEL_MASK			GENMASK(5, 5)

#define FAN54015_REG_VSP_MASK				GENMASK(2, 0)
#define FAN54015_REG_VSP				(BIT(2) | BIT(0))

#define FAN54015_REG_TERMINAL_CURRENT_MASK		GENMASK(3, 3)
#define FAN54015_REG_TERMINAL_VOLTAGE_MASK		GENMASK(7, 2)
#define FAN54015_REG_TERMINAL_VOLTAGE_SHIFT		2

#define FAN54015_REG_CHARGE_CONTROL_MASK		GENMASK(2, 2)
#define FAN54015_REG_CHARGE_DISABLE			BIT(2)
#define FAN54015_REG_CHARGE_ENABLE			0

#define FAN54015_REG_CURRENT_MASK			GENMASK(6, 4)
#define FAN54015_REG_CURRENT_MASK_SHIFT			4

#define FAN54015_REG_LIMIT_CURRENT_MASK			GENMASK(7, 6)
#define FAN54015_REG_LIMIT_CURRENT_SHIFT		6

#define FAN54015_DISABLE_PIN_MASK_2730			BIT(0)
#define FAN54015_DISABLE_PIN_MASK_2721			BIT(15)
#define FAN54015_DISABLE_PIN_MASK_2720			BIT(0)

struct fan54015_charge_current {
	int sdp_limit;
	int sdp_cur;
	int dcp_limit;
	int dcp_cur;
	int cdp_limit;
	int cdp_cur;
	int unknown_limit;
	int unknown_cur;
};

struct fan54015_charger_info {
	struct i2c_client *client;
	struct device *dev;
	struct power_supply *psy_usb;
	struct fan54015_charge_current cur;
	struct mutex lock;
	bool charging;
	struct delayed_work otg_work;
	struct delayed_work wdt_work;
	struct regmap *pmic;
	u32 charger_detect;
	u32 charger_pd;
	u32 charger_pd_mask;
	struct gpio_desc *gpiod;
	struct extcon_dev *edev;
	bool otg_enable;
	struct alarm wdg_timer;
	bool is_charger_online;
};

static int
fan54015_charger_set_limit_current(struct fan54015_charger_info *info, u32 limit_cur);

static int fan54015_read(struct fan54015_charger_info *info, u8 reg, u8 *data)
{
	int ret;

	ret = i2c_smbus_read_byte_data(info->client, reg);
	if (ret < 0)
		return ret;

	*data = ret;
	return 0;
}

static int fan54015_write(struct fan54015_charger_info *info, u8 reg, u8 data)
{
	return i2c_smbus_write_byte_data(info->client, reg, data);
}

static int
fan54015_update_bits(struct fan54015_charger_info *info, u8 reg, u8 mask, u8 data)
{
	u8 v;
	int ret;

	ret = fan54015_read(info, reg, &v);
	if (ret < 0)
		return ret;

	v &= ~mask;
	v |= (data & mask);

	return fan54015_write(info, reg, v);
}

static int fan54015_charger_set_safety_vol(struct fan54015_charger_info *info, u32 vol)
{
	u8 reg_val;

	if (vol < 4200)
		vol = 4200;
	if (vol > 4440)
		vol = 4440;
	reg_val = (vol - 4200) / 20 + 1;

	return fan54015_update_bits(info, FAN54015_REG_6,
				    FAN54015_REG_SAFETY_VOL_MASK, reg_val);
}

static int fan54015_charger_set_termina_vol(struct fan54015_charger_info *info, u32 vol)
{
	u8 reg_val;

	if (vol < 3500)
		reg_val = 0x0;
	else if (vol >= 4440)
		reg_val = 0x2f;
	else
		reg_val = (vol - 3499) / 20;

	return fan54015_update_bits(info, FAN54015_REG_2,
				    FAN54015_REG_TERMINAL_VOLTAGE_MASK,
				    reg_val << FAN54015_REG_TERMINAL_VOLTAGE_SHIFT);
}

static int fan54015_charger_set_safety_cur(struct fan54015_charger_info *info, u32 cur)
{
	u8 reg_val;

	if (cur < 650000)
		reg_val = 0x0;
	else if (cur >= 650000 && cur < 750000)
		reg_val = 0x1;
	else if (cur >= 750000 && cur < 850000)
		reg_val = 0x2;
	else if (cur >= 850000 && cur < 1050000)
		reg_val = 0x3;
	else if (cur >= 1050000 && cur < 1150000)
		reg_val = 0x4;
	else if (cur >= 1150000 && cur < 1350000)
		reg_val = 0x5;
	else if (cur >= 1350000 && cur < 1450000)
		reg_val = 0x6;
	else
		reg_val = 0x7;

	return fan54015_update_bits(info, FAN54015_REG_6,
				    FAN54015_REG_SAFETY_CUR_MASK,
				    reg_val << FAN54015_REG_CURRENT_MASK_SHIFT);
}

static int fan54015_charger_hw_init(struct fan54015_charger_info *info)
{
	struct sprd_battery_info bat_info = {};
	int voltage_max_microvolt;
	int ret;

	ret = sprd_battery_get_battery_info(info->psy_usb, &bat_info);
	if (ret) {
		dev_warn(info->dev, "no battery information is supplied\n");

		info->cur.sdp_limit = 500000;
		info->cur.sdp_cur = 500000;
		info->cur.dcp_limit = 1500000;
		info->cur.dcp_cur = 1500000;
		info->cur.cdp_limit = 1000000;
		info->cur.cdp_cur = 1000000;
		info->cur.unknown_limit = 1000000;
		info->cur.unknown_cur = 1000000;

		/*
		 * If no battery information is supplied, we should set
		 * default charge termination current to 120 mA, and default
		 * charge termination voltage to 4.44V.
		 */
		voltage_max_microvolt = 4440;
	} else {
		info->cur.sdp_limit = bat_info.cur.sdp_limit;
		info->cur.sdp_cur = bat_info.cur.sdp_cur;
		info->cur.dcp_limit = bat_info.cur.dcp_limit;
		info->cur.dcp_cur = bat_info.cur.dcp_cur;
		info->cur.cdp_limit = bat_info.cur.cdp_limit;
		info->cur.cdp_cur = bat_info.cur.cdp_cur;
		info->cur.unknown_limit = bat_info.cur.unknown_limit;
		info->cur.unknown_cur = bat_info.cur.unknown_cur;

		voltage_max_microvolt = bat_info.constant_charge_voltage_max_uv / 1000;
		sprd_battery_put_battery_info(info->psy_usb, &bat_info);
	}

	if (of_device_is_compatible(info->dev->of_node, "fairchild,fan54015_chg")) {
		ret = fan54015_charger_set_safety_vol(info, voltage_max_microvolt);
		if (ret) {
			dev_err(info->dev, "set fan54015 safety vol failed\n");
			return ret;
		}

		ret = fan54015_charger_set_safety_cur(info, info->cur.dcp_cur);
		if (ret) {
			dev_err(info->dev, "set fan54015 safety cur failed\n");
			return ret;
		}
	}

	ret = fan54015_update_bits(info, FAN54015_REG_4,
				   FAN54015_REG_RESET_MASK,
				   FAN54015_REG_RESET);
	if (ret) {
		dev_err(info->dev, "reset fan54015 failed\n");
		return ret;
	}

	if (of_device_is_compatible(info->dev->of_node, "prisemi,psc5415z_chg")) {
		ret = fan54015_charger_set_safety_vol(info, voltage_max_microvolt);
		if (ret) {
			dev_err(info->dev, "set psc5415z safety vol failed\n");
			return ret;
		}

		ret = fan54015_charger_set_safety_cur(info, info->cur.dcp_cur);
		if (ret) {
			dev_err(info->dev, "set psc5415z safety cur failed\n");
			return ret;
		}
	}

	ret = fan54015_update_bits(info, FAN54015_REG_1,
				   FAN54015_REG_WEAK_VOL_THRESHOLD_MASK, 0);
	if (ret) {
		dev_err(info->dev, "set fan54015 weak voltage threshold failed\n");
		return ret;
	}
	ret = fan54015_update_bits(info, FAN54015_REG_5, FAN54015_REG_IO_LEVEL_MASK, 0);
	if (ret) {
		dev_err(info->dev, "set fan54015 io level failed\n");
		return ret;
	}

	ret = fan54015_update_bits(info, FAN54015_REG_5,
				   FAN54015_REG_VSP_MASK,
				   FAN54015_REG_VSP);
	if (ret) {
		dev_err(info->dev, "set fan54015 vsp failed\n");
		return ret;
	}

	ret = fan54015_update_bits(info, FAN54015_REG_1,
				   FAN54015_REG_TERMINAL_CURRENT_MASK, 0);
	if (ret) {
		dev_err(info->dev, "set fan54015 terminal cur failed\n");
		return ret;
	}

	ret = fan54015_update_bits(info,
				   FAN54015_REG_0,
				   FAN54015_REG_RESET_MASK,
				   FAN54015_REG_RESET);
	if (ret) {
		dev_err(info->dev, "feed fan54015 watchdog failed\n");
		return ret;
	}
	ret = fan54015_charger_set_termina_vol(info, voltage_max_microvolt);
	if (ret) {
		dev_err(info->dev, "set fan54015 terminal vol failed\n");
		return ret;
	}

	ret = fan54015_charger_set_limit_current(info, info->cur.unknown_cur);
	if (ret)
		dev_err(info->dev, "set fan54015 limit current failed\n");

	return ret;
}

static void fan54015_charger_dump_register(struct fan54015_charger_info *info)
{
	int ret, len, idx = 0;
	u8 reg_val, addr;
	char buf[256];

	memset(buf, '\0', sizeof(buf));
	for (addr = FAN54015_REG_0; addr < FAN54015_REG_6; addr++) {
		ret = fan54015_read(info, addr, &reg_val);
		if (ret == 0) {
			len = snprintf(buf + idx, sizeof(buf) - idx,
				       "[REG_0x%.2x]=0x%.2x  ", addr, reg_val);
			idx += len;
		}
	}

	addr = FAN54015_REG_10;
	ret = fan54015_read(info, addr, &reg_val);
	if (ret == 0) {
		len = snprintf(buf + idx, sizeof(buf) - idx,
			       "[REG_0x%.2x]=0x%.2x  ", addr, reg_val);
		idx += len;
	}

	dev_info(info->dev, "%s: %s", __func__, buf);
}

static int fan54015_charger_start_charge(struct fan54015_charger_info *info)
{
	int ret;

	ret = regmap_update_bits(info->pmic, info->charger_pd, info->charger_pd_mask, 0);
	if (ret)
		dev_err(info->dev, "enable fan54015 charge failed\n");

	return ret;
}

static void fan54015_charger_stop_charge(struct fan54015_charger_info *info)
{
	int ret;

	ret = regmap_update_bits(info->pmic, info->charger_pd,
				 info->charger_pd_mask,
				 info->charger_pd_mask);
	if (ret)
		dev_err(info->dev, "disable fan54015 charge failed\n");
}

static int fan54015_charger_set_current(struct fan54015_charger_info *info, u32 cur)
{
	u8 reg_val;

	if (cur < 650000)
		reg_val = 0x0;
	else if (cur >= 650000 && cur < 750000)
		reg_val = 0x1;
	else if (cur >= 750000 && cur < 850000)
		reg_val = 0x2;
	else if (cur >= 850000 && cur < 1050000)
		reg_val = 0x3;
	else if (cur >= 1050000 && cur < 1150000)
		reg_val = 0x4;
	else if (cur >= 1150000 && cur < 1350000)
		reg_val = 0x5;
	else if (cur >= 1350000 && cur < 1450000)
		reg_val = 0x6;
	else
		reg_val = 0x7;

	return fan54015_update_bits(info, FAN54015_REG_4,
				    FAN54015_REG_CURRENT_MASK | FAN54015_REG_RESET_MASK,
				    reg_val << FAN54015_REG_CURRENT_MASK_SHIFT);
}

static int fan54015_charger_get_current(struct fan54015_charger_info *info, u32 *cur)
{
	u8 reg_val;
	int ret;

	ret = fan54015_read(info, FAN54015_REG_4, &reg_val);
	if (ret < 0)
		return ret;

	reg_val &= FAN54015_REG_CURRENT_MASK;
	reg_val = reg_val >> FAN54015_REG_CURRENT_MASK_SHIFT;

	switch (reg_val) {
	case 0:
		*cur = 550000;
		break;
	case 1:
		*cur = 650000;
		break;
	case 2:
		*cur = 750000;
		break;
	case 3:
		*cur = 850000;
		break;
	case 4:
		*cur = 1050000;
		break;
	case 5:
		*cur = 1150000;
		break;
	case 6:
		*cur = 1350000;
		break;
	case 7:
		*cur = 1450000;
		break;
	default:
		*cur = 550000;
	}
	return 0;
}

static int
fan54015_charger_set_limit_current(struct fan54015_charger_info *info, u32 limit_cur)
{
	u8 reg_val;
	int ret;

	if (limit_cur <= 100000)
		reg_val = 0x0;
	else if (limit_cur > 100000 && limit_cur <= 500000)
		reg_val = 0x1;
	else if (limit_cur > 500000 && limit_cur <= 800000)
		reg_val = 0x2;
	else
		reg_val = 0x3;

	ret = fan54015_update_bits(info, FAN54015_REG_1,
				   FAN54015_REG_LIMIT_CURRENT_MASK,
				   reg_val << FAN54015_REG_LIMIT_CURRENT_SHIFT);
	if (ret)
		dev_err(info->dev, "set fan54015 limit cur failed\n");

	return ret;
}

static u32
fan54015_charger_get_limit_current(struct fan54015_charger_info *info, u32 *limit_cur)
{
	u8 reg_val;
	int ret;

	ret = fan54015_read(info, FAN54015_REG_1, &reg_val);
	if (ret < 0)
		return ret;

	reg_val &= FAN54015_REG_LIMIT_CURRENT_MASK;
	reg_val = reg_val >> FAN54015_REG_LIMIT_CURRENT_SHIFT;

	switch (reg_val) {
	case 0:
		*limit_cur = 100000;
		break;
	case 1:
		*limit_cur = 500000;
		break;
	case 2:
		*limit_cur = 800000;
		break;
	case 3:
		*limit_cur = 2000000;
		break;
	default:
		*limit_cur = 100000;
	}

	return 0;
}

static int fan54015_charger_get_health(struct fan54015_charger_info *info, u32 *health)
{
	*health = POWER_SUPPLY_HEALTH_GOOD;

	return 0;
}

static int fan54015_charger_feed_watchdog(struct fan54015_charger_info *info)
{
	int ret;

	ret = fan54015_update_bits(info, FAN54015_REG_0,
				   FAN54015_REG_RESET_MASK, FAN54015_REG_RESET);
	if (ret)
		dev_err(info->dev, "fan54015 is failed to feeding watchdog, ret = %d\n", ret);

	return ret;
}

static int fan54015_charger_get_status(struct fan54015_charger_info *info)
{
	if (info->charging == true)
		return POWER_SUPPLY_STATUS_CHARGING;
	else
		return POWER_SUPPLY_STATUS_NOT_CHARGING;
}

static int fan54015_charger_set_status(struct fan54015_charger_info *info, int val)
{
	int ret = 0;

	if (!val && info->charging) {
		fan54015_charger_stop_charge(info);
		info->charging = false;
	} else if (val && !info->charging) {
		ret = fan54015_charger_start_charge(info);
		if (ret)
			dev_err(info->dev, "start charge failed\n");
		else
			info->charging = true;
	}

	return ret;
}

static int fan54015_charger_usb_get_property(struct power_supply *psy,
					     enum power_supply_property psp,
					     union power_supply_propval *val)
{
	struct fan54015_charger_info *info = power_supply_get_drvdata(psy);
	u32 cur, health, status = 0;
	int ret = 0;

	if (!info) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = fan54015_charger_get_status(info);
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		if (!info->charging) {
			val->intval = 0;
		} else {
			ret = fan54015_charger_get_current(info, &cur);
			if (ret)
				goto out;

			val->intval = cur;
		}
		break;

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		if (!info->charging) {
			val->intval = 0;
		} else {
			ret = fan54015_charger_get_limit_current(info, &cur);
			if (ret)
				goto out;

			val->intval = cur;
		}
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		if (info->charging) {
			val->intval = 0;
		} else {
			ret = fan54015_charger_get_health(info, &health);
			if (ret)
				goto out;

			val->intval = health;
		}
		break;

	case POWER_SUPPLY_PROP_CALIBRATE:
		ret = regmap_read(info->pmic, info->charger_pd, &status);
		if (ret) {
			dev_err(info->dev, "get charge status failed\n");
			goto out;
		}
		val->intval = !(status & info->charger_pd_mask);
		break;

	default:
		ret = -EINVAL;
	}

out:
	mutex_unlock(&info->lock);
	return ret;
}

static int
fan54015_charger_usb_set_property(struct power_supply *psy,
				  enum power_supply_property psp,
				  const union power_supply_propval *val)
{
	struct fan54015_charger_info *info = power_supply_get_drvdata(psy);
	int ret = 0;

	if (!info) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = fan54015_charger_set_current(info, val->intval);
		if (ret < 0)
			dev_err(info->dev, "set charge current failed\n");
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = fan54015_charger_set_limit_current(info, val->intval);
		if (ret < 0)
			dev_err(info->dev, "set input current limit failed\n");
		break;

	case POWER_SUPPLY_PROP_STATUS:
		ret = fan54015_charger_set_status(info, val->intval);
		if (ret < 0)
			dev_err(info->dev, "set charge status failed\n");
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		ret = fan54015_charger_set_termina_vol(info, val->intval / 1000);
		if (ret < 0)
			dev_err(info->dev, "failed to set terminate voltage\n");
		break;

	case POWER_SUPPLY_PROP_CALIBRATE:
		ret = 0;
		if (val->intval == true) {
			ret = fan54015_charger_start_charge(info);
			if (ret)
				dev_err(info->dev, "failed to start charge\n");
		} else if (val->intval == false)
			fan54015_charger_stop_charge(info);

		dev_info(info->dev, "POWER_SUPPLY_PROP_CHARGING_ENABLED: %s\n",
			 val->intval ? "enable" : "disable");
		break;

	case POWER_SUPPLY_PROP_PRESENT:
		info->is_charger_online = val->intval;
		if (val->intval == true)
			schedule_delayed_work(&info->wdt_work, 0);
		else
			cancel_delayed_work_sync(&info->wdt_work);

		break;

	default:
		ret = -EINVAL;
	}

	mutex_unlock(&info->lock);
	return ret;
}

static int
fan54015_charger_property_is_writeable(struct power_supply *psy, enum power_supply_property psp)
{
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
	case POWER_SUPPLY_PROP_CALIBRATE:
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_STATUS:
		ret = 1;
		break;

	default:
		ret = 0;
	}

	return ret;
}

static enum power_supply_property fan54015_usb_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CALIBRATE,
};

static const struct power_supply_desc fan54015_charger_desc = {
	.name			= "fan54015_charger",
	.type			= POWER_SUPPLY_TYPE_UNKNOWN,
	.properties		= fan54015_usb_props,
	.num_properties		= ARRAY_SIZE(fan54015_usb_props),
	.get_property		= fan54015_charger_usb_get_property,
	.set_property		= fan54015_charger_usb_set_property,
	.property_is_writeable	= fan54015_charger_property_is_writeable,
};

static void fan54015_charger_feed_watchdog_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct fan54015_charger_info *info =
		container_of(dwork, struct fan54015_charger_info, wdt_work);
	int ret;

	if (!info) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return;
	}

	ret = fan54015_charger_feed_watchdog(info);
	if (ret)
		schedule_delayed_work(&info->wdt_work, HZ * 5);
	else
		schedule_delayed_work(&info->wdt_work, HZ * 15);

}

#if IS_ENABLED(CONFIG_REGULATOR)
static void fan54015_charger_otg_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct fan54015_charger_info *info =
		container_of(dwork, struct fan54015_charger_info, otg_work);
	int ret;

	if (!info) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return;
	}

	if (!extcon_get_state(info->edev, EXTCON_USB)) {
		dev_dbg(info->dev, "%s:line%d:restart charger otg\n", __func__, __LINE__);
		ret = fan54015_update_bits(info, FAN54015_REG_1,
					   FAN54015_REG_HZ_MODE_MASK |
					   FAN54015_REG_OPA_MODE_MASK,
					   FAN54015_REG_OPA_MODE_MASK);
		if (ret)
			dev_err(info->dev, "restart fan54015 charger otg failed\n");
	}

	dev_dbg(info->dev, "%s:line%d:schedule_work\n", __func__, __LINE__);
	schedule_delayed_work(&info->otg_work, msecs_to_jiffies(500));
}

static int fan54015_charger_enable_otg(struct regulator_dev *dev)
{
	struct fan54015_charger_info *info = rdev_get_drvdata(dev);
	int ret = 0;

	if (!info) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}

	mutex_lock(&info->lock);

	/*
	 * Disable charger detection function in case
	 * affecting the OTG timing sequence.
	 */
	ret = regmap_update_bits(info->pmic, info->charger_detect,
				 BIT_DP_DM_BC_ENB, BIT_DP_DM_BC_ENB);
	if (ret) {
		dev_err(info->dev, "failed to disable bc1.2 detect function.\n");
		goto out;
	}

	ret = fan54015_update_bits(info, FAN54015_REG_1,
				   FAN54015_REG_HZ_MODE_MASK |
				   FAN54015_REG_OPA_MODE_MASK,
				   FAN54015_REG_OPA_MODE_MASK);
	if (ret) {
		dev_err(info->dev, "enable fan54015 otg failed\n");
		regmap_update_bits(info->pmic, info->charger_detect,
				   BIT_DP_DM_BC_ENB, 0);
		goto out;
	}

	dev_info(info->dev, "%s:line%d:enable_otg\n", __func__, __LINE__);
	info->otg_enable = true;
	schedule_delayed_work(&info->wdt_work,
			      msecs_to_jiffies(FAN54015_FEED_WATCHDOG_VALID_MS));
	schedule_delayed_work(&info->otg_work,
			      msecs_to_jiffies(FAN54015_OTG_VALID_MS));

out:
	mutex_unlock(&info->lock);
	return ret;

}

static int fan54015_charger_disable_otg(struct regulator_dev *dev)
{
	struct fan54015_charger_info *info = rdev_get_drvdata(dev);
	int ret = 0;

	if (!info) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}

	mutex_lock(&info->lock);

	info->otg_enable = false;
	cancel_delayed_work_sync(&info->wdt_work);
	cancel_delayed_work_sync(&info->otg_work);
	ret = fan54015_update_bits(info, FAN54015_REG_1,
				   FAN54015_REG_HZ_MODE_MASK |
				   FAN54015_REG_OPA_MODE_MASK,
				   0);
	if (ret) {
		dev_err(info->dev, "disable fan54015 otg failed\n");
		goto out;
	}

	/* Enable charger detection function to identify the charger type */
	ret = regmap_update_bits(info->pmic, info->charger_detect,
				 BIT_DP_DM_BC_ENB, 0);
	if (ret)
		dev_err(info->dev, "enable BC1.2 failed\n");

	dev_info(info->dev, "%s:line%d:disable_otg\n", __func__, __LINE__);
out:
	mutex_unlock(&info->lock);

	return ret;
}

static int fan54015_charger_vbus_is_enabled(struct regulator_dev *dev)
{
	struct fan54015_charger_info *info = rdev_get_drvdata(dev);
	int ret;
	u8 val;

	if (!info) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}

	mutex_lock(&info->lock);

	ret = fan54015_read(info, FAN54015_REG_1, &val);
	if (ret) {
		dev_err(info->dev, "failed to get fan54015 otg status\n");
		mutex_unlock(&info->lock);
		return ret;
	}

	val &= FAN54015_REG_OPA_MODE_MASK;
	dev_dbg(info->dev, "%s:line%d:vbus_is_enabled\n", __func__, __LINE__);

	mutex_unlock(&info->lock);
	return val;
}

static const struct regulator_ops fan54015_charger_vbus_ops = {
	.enable = fan54015_charger_enable_otg,
	.disable = fan54015_charger_disable_otg,
	.is_enabled = fan54015_charger_vbus_is_enabled,
};

static const struct regulator_desc fan54015_charger_vbus_desc = {
	.name = "otg-vbus",
	.of_match = "otg-vbus",
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
	.ops = &fan54015_charger_vbus_ops,
	.fixed_uV = 5000000,
	.n_voltages = 1,
};

static int
fan54015_charger_register_vbus_regulator(struct fan54015_charger_info *info)
{
	struct regulator_config cfg = { };
	struct regulator_dev *reg;
	int ret = 0;

	cfg.dev = info->dev;
	cfg.driver_data = info;
	reg = devm_regulator_register(info->dev,
				      &fan54015_charger_vbus_desc, &cfg);
	if (IS_ERR(reg)) {
		ret = PTR_ERR(reg);
		dev_err(info->dev, "Can't register regulator:%d\n", ret);
	}

	return ret;
}

#else
static int
fan54015_charger_register_vbus_regulator(struct fan54015_charger_info *info)
{
	return 0;
}
#endif

static int
fan54015_charger_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct device *dev = &client->dev;
	struct power_supply_config charger_cfg = { };
	struct fan54015_charger_info *info;
	struct device_node *regmap_np;
	struct platform_device *regmap_pdev;
	int ret;

	if (!adapter) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(dev, "No support for SMBUS_BYTE_DATA\n");
		return -ENODEV;
	}

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->client = client;
	info->dev = dev;

	i2c_set_clientdata(client, info);

	info->edev = extcon_get_edev_by_phandle(info->dev, 0);
	if (IS_ERR(info->edev)) {
		dev_err(dev, "failed to find vbus extcon device.\n");
		return -EPROBE_DEFER;
	}

	regmap_np = of_find_compatible_node(NULL, NULL, "sprd,sc27xx-syscon");
	if (!regmap_np) {
		dev_err(dev, "unable to get syscon node\n");
		return -ENODEV;
	}

	if (of_device_is_compatible(regmap_np->parent, "sprd,sc2730"))
		info->charger_pd_mask = FAN54015_DISABLE_PIN_MASK_2730;
	else if (of_device_is_compatible(regmap_np->parent, "sprd,sc2721"))
		info->charger_pd_mask = FAN54015_DISABLE_PIN_MASK_2721;
	else if (of_device_is_compatible(regmap_np->parent, "sprd,sc2720"))
		info->charger_pd_mask = FAN54015_DISABLE_PIN_MASK_2720;
	else {
		dev_err(dev, "failed to get charger_pd mask\n");
		return -EINVAL;
	}

	ret = of_property_read_u32_index(regmap_np, "reg", 1,
					 &info->charger_detect);
	if (ret) {
		dev_err(dev, "failed to get charger_detect\n");
		return -EINVAL;
	}

	ret = of_property_read_u32_index(regmap_np, "reg", 2,
					 &info->charger_pd);
	if (ret) {
		dev_err(dev, "failed to get charger_pd reg\n");
		return ret;
	}

	regmap_pdev = of_find_device_by_node(regmap_np);
	if (!regmap_pdev) {
		of_node_put(regmap_np);
		dev_err(dev, "unable to get syscon device\n");
		return -ENODEV;
	}

	of_node_put(regmap_np);
	info->pmic = dev_get_regmap(regmap_pdev->dev.parent, NULL);
	if (!info->pmic) {
		put_device(&regmap_pdev->dev);
		dev_err(dev, "unable to get pmic regmap device\n");
		return -ENODEV;
	}

	put_device(&regmap_pdev->dev);
	mutex_init(&info->lock);
	mutex_lock(&info->lock);

	charger_cfg.drv_data = info;
	charger_cfg.of_node = dev->of_node;
	info->psy_usb = devm_power_supply_register(dev,
						   &fan54015_charger_desc,
						   &charger_cfg);
	if (IS_ERR(info->psy_usb)) {
		dev_err(dev, "failed to register power supply\n");
		ret = PTR_ERR(info->psy_usb);
		goto err_regmap_exit;
	}

	ret = fan54015_charger_hw_init(info);
	if (ret) {
		dev_err(dev, "failed to fan54015_charger_hw_init\n");
		goto err_regmap_exit;
	}

	fan54015_charger_stop_charge(info);

	device_init_wakeup(info->dev, true);

	alarm_init(&info->wdg_timer, ALARM_BOOTTIME, NULL);
	INIT_DELAYED_WORK(&info->otg_work, fan54015_charger_otg_work);
	INIT_DELAYED_WORK(&info->wdt_work, fan54015_charger_feed_watchdog_work);

	ret = fan54015_charger_register_vbus_regulator(info);
	if (ret) {
		dev_err(dev, "failed to register vbus regulator.\n");
		goto err_regmap_exit;
	}

	mutex_unlock(&info->lock);

	fan54015_charger_dump_register(info);

	return 0;

err_regmap_exit:
	mutex_unlock(&info->lock);
	mutex_destroy(&info->lock);
	return ret;
}

static void fan54015_charger_shutdown(struct i2c_client *client)
{
	struct fan54015_charger_info *info = i2c_get_clientdata(client);
	int ret = 0;

	cancel_delayed_work_sync(&info->wdt_work);
	if (info->otg_enable) {
		info->otg_enable = false;
		cancel_delayed_work_sync(&info->otg_work);
		ret = fan54015_update_bits(info, FAN54015_REG_1,
					   FAN54015_REG_HZ_MODE_MASK |
					   FAN54015_REG_OPA_MODE_MASK,
					   0);
		if (ret)
			dev_err(info->dev, "disable fan54015 otg failed ret = %d\n", ret);

		/* Enable charger detection function to identify the charger type */
		ret = regmap_update_bits(info->pmic, info->charger_detect,
					 BIT_DP_DM_BC_ENB, 0);
		if (ret)
			dev_err(info->dev,
				"enable charger detection function failed ret = %d\n", ret);
	}
}

static int fan54015_charger_remove(struct i2c_client *client)
{
	struct fan54015_charger_info *info = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&info->wdt_work);
	cancel_delayed_work_sync(&info->otg_work);

	return 0;
}

#if IS_ENABLED(CONFIG_PM_SLEEP)
static int fan54015_charger_alarm_prepare(struct device *dev)
{
	struct fan54015_charger_info *info = dev_get_drvdata(dev);
	ktime_t now, add;

	if (!info) {
		pr_err("%s: info is null!\n", __func__);
		return 0;
	}

	if (!info->otg_enable)
		return 0;

	now = ktime_get_boottime();
	add = ktime_set(FAN54015_WDG_TIMER_S, 0);
	alarm_start(&info->wdg_timer, ktime_add(now, add));
	return 0;
}

static void fan54015_charger_alarm_complete(struct device *dev)
{
	struct fan54015_charger_info *info = dev_get_drvdata(dev);

	if (!info) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return;
	}

	if (!info->otg_enable)
		return;

	alarm_cancel(&info->wdg_timer);
}

static int fan54015_charger_suspend(struct device *dev)
{
	struct fan54015_charger_info *info = dev_get_drvdata(dev);

	if (!info) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (info->otg_enable || info->is_charger_online)
		/* feed watchdog first before suspend */
		fan54015_charger_feed_watchdog(info);

	if (!info->otg_enable)
		return 0;

	cancel_delayed_work_sync(&info->wdt_work);

	return 0;
}

static int fan54015_charger_resume(struct device *dev)
{
	struct fan54015_charger_info *info = dev_get_drvdata(dev);

	if (!info) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (info->otg_enable || info->is_charger_online)
		/* feed watchdog first after resume */
		fan54015_charger_feed_watchdog(info);

	if (!info->otg_enable)
		return 0;

	schedule_delayed_work(&info->wdt_work, HZ * 15);

	return 0;
}
#endif

static const struct dev_pm_ops fan54015_charger_pm_ops = {
	.prepare = fan54015_charger_alarm_prepare,
	.complete = fan54015_charger_alarm_complete,
	SET_SYSTEM_SLEEP_PM_OPS(fan54015_charger_suspend, fan54015_charger_resume)
};

static const struct i2c_device_id fan54015_i2c_id[] = {
	{"fan54015_chg", 0},
	{}
};

static const struct of_device_id fan54015_charger_of_match[] = {
	{ .compatible = "fairchild,fan54015_chg", },
	{ .compatible = "prisemi,psc5415z_chg", },
	{ }
};

MODULE_DEVICE_TABLE(of, fan54015_charger_of_match);

static struct i2c_driver fan54015_charger_driver = {
	.driver = {
		.name = "fan54015-charger",
		.of_match_table = fan54015_charger_of_match,
		.pm = &fan54015_charger_pm_ops,
	},
	.probe = fan54015_charger_probe,
	.shutdown = fan54015_charger_shutdown,
	.remove = fan54015_charger_remove,
	.id_table = fan54015_i2c_id,
};

module_i2c_driver(fan54015_charger_driver);
MODULE_DESCRIPTION("FAN54015 Charger Driver");
MODULE_LICENSE("GPL v2");
