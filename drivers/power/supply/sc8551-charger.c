// SPDX-License-Identifier: GPL-2.0:
// Copyright (c) 2021 unisoc.

/*
 * Driver for the SC Solutions sc8551 charger.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/bitops.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/power_supply.h>
#include <linux/power/charger-manager.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <linux/power/sc8551_reg.h>

enum {
	ADC_IBUS,
	ADC_VBUS,
	ADC_VAC,
	ADC_VOUT,
	ADC_VBAT,
	ADC_IBAT,
	ADC_TBUS,
	ADC_TBAT,
	ADC_TDIE,
	ADC_MAX_NUM,
};

#define SC8551_ROLE_STDALONE		0
#define SC8551_ROLE_SLAVE		1
#define SC8551_ROLE_MASTER		2

enum {
	SC8551_STDALONE,
	SC8551_SLAVE,
	SC8551_MASTER,
};

static int sc8551_mode_data[] = {
	[SC8551_STDALONE] = SC8551_STDALONE,
	[SC8551_MASTER] = SC8551_ROLE_MASTER,
	[SC8551_SLAVE] = SC8551_ROLE_SLAVE,
};

#define	BAT_OVP_ALARM			BIT(7)
#define BAT_OCP_ALARM			BIT(6)
#define	BUS_OVP_ALARM			BIT(5)
#define	BUS_OCP_ALARM			BIT(4)
#define	BAT_UCP_ALARM			BIT(3)
#define	VBUS_INSERT			BIT(2)
#define VBAT_INSERT			BIT(1)
#define	ADC_DONE			BIT(0)

#define BAT_OVP_FAULT			BIT(7)
#define BAT_OCP_FAULT			BIT(6)
#define BUS_OVP_FAULT			BIT(5)
#define BUS_OCP_FAULT			BIT(4)
#define TBUS_TBAT_ALARM			BIT(3)
#define TS_BAT_FAULT			BIT(2)
#define	TS_BUS_FAULT			BIT(1)
#define	TS_DIE_FAULT			BIT(0)

#define ADC_REG_BASE			SC8551_REG_16

/*below used for comm with other module*/
#define	BAT_OVP_FAULT_SHIFT		0
#define	BAT_OCP_FAULT_SHIFT		1
#define	BUS_OVP_FAULT_SHIFT		2
#define	BUS_OCP_FAULT_SHIFT		3
#define	BAT_THERM_FAULT_SHIFT		4
#define	BUS_THERM_FAULT_SHIFT		5
#define	DIE_THERM_FAULT_SHIFT		6

#define	BAT_OVP_FAULT_MASK		(1 << BAT_OVP_FAULT_SHIFT)
#define	BAT_OCP_FAULT_MASK		(1 << BAT_OCP_FAULT_SHIFT)
#define	BUS_OVP_FAULT_MASK		(1 << BUS_OVP_FAULT_SHIFT)
#define	BUS_OCP_FAULT_MASK		(1 << BUS_OCP_FAULT_SHIFT)
#define	BAT_THERM_FAULT_MASK		(1 << BAT_THERM_FAULT_SHIFT)
#define	BUS_THERM_FAULT_MASK		(1 << BUS_THERM_FAULT_SHIFT)
#define	DIE_THERM_FAULT_MASK		(1 << DIE_THERM_FAULT_SHIFT)

#define	BAT_OVP_ALARM_SHIFT		0
#define	BAT_OCP_ALARM_SHIFT		1
#define	BUS_OVP_ALARM_SHIFT		2
#define	BUS_OCP_ALARM_SHIFT		3
#define	BAT_THERM_ALARM_SHIFT		4
#define	BUS_THERM_ALARM_SHIFT		5
#define	DIE_THERM_ALARM_SHIFT		6
#define BAT_UCP_ALARM_SHIFT		7

#define	BAT_OVP_ALARM_MASK		(1 << BAT_OVP_ALARM_SHIFT)
#define	BAT_OCP_ALARM_MASK		(1 << BAT_OCP_ALARM_SHIFT)
#define	BUS_OVP_ALARM_MASK		(1 << BUS_OVP_ALARM_SHIFT)
#define	BUS_OCP_ALARM_MASK		(1 << BUS_OCP_ALARM_SHIFT)
#define	BAT_THERM_ALARM_MASK		(1 << BAT_THERM_ALARM_SHIFT)
#define	BUS_THERM_ALARM_MASK		(1 << BUS_THERM_ALARM_SHIFT)
#define	DIE_THERM_ALARM_MASK		(1 << DIE_THERM_ALARM_SHIFT)
#define	BAT_UCP_ALARM_MASK		(1 << BAT_UCP_ALARM_SHIFT)

#define VBAT_REG_STATUS_SHIFT		0
#define IBAT_REG_STATUS_SHIFT		1

#define VBAT_REG_STATUS_MASK		(1 << VBAT_REG_STATUS_SHIFT)
#define IBAT_REG_STATUS_MASK		(1 << VBAT_REG_STATUS_SHIFT)
/*end*/

struct sc8551_cfg {
	bool bat_ovp_disable;
	bool bat_ocp_disable;
	bool bat_ovp_alm_disable;
	bool bat_ocp_alm_disable;

	int bat_ovp_th;
	int bat_ovp_alm_th;
	int bat_ovp_default_alm_th;
	int bat_ocp_th;
	int bat_ocp_alm_th;
	int bat_delta_volt;

	bool bus_ovp_alm_disable;
	bool bus_ocp_disable;
	bool bus_ocp_alm_disable;

	int bus_ovp_th;
	int bus_ovp_alm_th;
	int bus_ocp_th;
	int bus_ocp_alm_th;

	bool bat_ucp_alm_disable;

	int bat_ucp_alm_th;
	int ac_ovp_th;

	bool bat_therm_disable;
	bool bus_therm_disable;
	bool die_therm_disable;

	int bat_therm_th; /*in %*/
	int bus_therm_th; /*in %*/
	int die_therm_th; /*in degC*/

	int sense_r_mohm;

	int ibat_reg_th;
	int vbat_reg_th;
	int vdrop_th;
	int vdrop_deglitch;
	int ss_timeout;
	int wdt_timer;
	int ibus_ucp;
	bool regulation_disable;

	const char *psy_fuel_gauge;
};

struct sc8551 {
	struct device *dev;
	struct i2c_client *client;

	int part_no;
	int revision;

	int mode;

	struct mutex data_lock;
	struct mutex i2c_rw_lock;
	struct mutex charging_disable_lock;
	struct mutex irq_complete;

	bool irq_waiting;
	bool irq_disabled;
	bool resume_completed;

	int irq_gpio;
	int irq;

	bool batt_present;
	bool vbus_present;

	bool usb_present;
	bool charge_enabled;	/* Register bit status */

	bool is_sc8551;
	int  vbus_error;

	/* ADC reading */
	int vbat_volt;
	int vbus_volt;
	int vout_volt;
	int vac_volt;

	int ibat_curr;
	int ibus_curr;

	int bat_temp;
	int bus_temp;
	int die_temp;

	/* alarm/fault status */
	bool bat_ovp_fault;
	bool bat_ocp_fault;
	bool bus_ovp_fault;
	bool bus_ocp_fault;

	bool bat_ovp_alarm;
	bool bat_ocp_alarm;
	bool bus_ovp_alarm;
	bool bus_ocp_alarm;

	bool bat_ucp_alarm;

	bool bus_err_lo;
	bool bus_err_hi;

	bool therm_shutdown_flag;
	bool therm_shutdown_stat;

	bool vbat_reg;
	bool ibat_reg;

	int  prev_alarm;
	int  prev_fault;

	int chg_ma;
	int chg_mv;

	int charge_state;

	struct sc8551_cfg *cfg;

	int skip_writes;
	int skip_reads;

	struct sc8551_platform_data *platform_data;

	struct delayed_work monitor_work;
	struct delayed_work wdt_work;

	struct dentry *debug_root;

	struct power_supply_desc psy_desc;
	struct power_supply_config psy_cfg;
	struct power_supply *fc2_psy;
};

static void sc8551_dump_reg(struct sc8551 *sc);

static int __sc8551_read_byte(struct sc8551 *sc, u8 reg, u8 *data)
{
	s32 ret;

	ret = i2c_smbus_read_byte_data(sc->client, reg);
	if (ret < 0) {
		dev_err(sc->dev, "i2c read fail: can't read from reg 0x%02X\n", reg);
		return ret;
	}

	*data = (u8) ret;

	return 0;
}

static int __sc8551_write_byte(struct sc8551 *sc, int reg, u8 val)
{
	s32 ret;

	ret = i2c_smbus_write_byte_data(sc->client, reg, val);
	if (ret < 0) {
		dev_err(sc->dev, "i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
			val, reg, ret);
		return ret;
	}
	return 0;
}

static int sc8551_read_byte(struct sc8551 *sc, u8 reg, u8 *data)
{
	int ret;

	if (sc->skip_reads) {
		*data = 0;
		return 0;
	}

	mutex_lock(&sc->i2c_rw_lock);
	ret = __sc8551_read_byte(sc, reg, data);
	mutex_unlock(&sc->i2c_rw_lock);

	return ret;
}

static int sc8551_write_byte(struct sc8551 *sc, u8 reg, u8 data)
{
	int ret;

	if (sc->skip_writes)
		return 0;

	mutex_lock(&sc->i2c_rw_lock);
	ret = __sc8551_write_byte(sc, reg, data);
	mutex_unlock(&sc->i2c_rw_lock);

	return ret;
}

static int sc8551_update_bits(struct sc8551 *sc, u8 reg, u8 mask, u8 data)
{
	int ret;
	u8 tmp;

	if (sc->skip_reads || sc->skip_writes)
		return 0;

	mutex_lock(&sc->i2c_rw_lock);
	ret = __sc8551_read_byte(sc, reg, &tmp);
	if (ret) {
		dev_err(sc->dev, "Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= data & mask;

	ret = __sc8551_write_byte(sc, reg, tmp);
	if (ret)
		dev_err(sc->dev, "Failed: reg=%02X, ret=%d\n", reg, ret);

out:
	mutex_unlock(&sc->i2c_rw_lock);
	return ret;
}

static int sc8551_enable_charge(struct sc8551 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8551_CHG_ENABLE;
	else
		val = SC8551_CHG_DISABLE;

	val <<= SC8551_CHG_EN_SHIFT;

	dev_info(sc->dev, "sc8551 charger %s\n", enable == false ? "disable" : "enable");
	ret = sc8551_update_bits(sc, SC8551_REG_0C, SC8551_CHG_EN_MASK, val);

	return ret;
}

static int sc8551_check_charge_enabled(struct sc8551 *sc, bool *enabled)
{
	int ret;
	u8 val;

	ret = sc8551_read_byte(sc, SC8551_REG_0C, &val);
	if (ret < 0) {
		dev_err(sc->dev, "failed to check charge enable, ret = %d\n", ret);
		*enabled = false;
		return ret;
	}

	*enabled = !!(val & SC8551_CHG_EN_MASK);
	return ret;
}

static int sc8551_reset(struct sc8551 *sc, bool reset)
{
	u8 val;

	if (reset)
		val = SC8551_REG_RST_ENABLE;
	else
		val = SC8551_REG_RST_DISABLE;

	val <<= SC8551_REG_RST_SHIFT;

	return sc8551_update_bits(sc, SC8551_REG_0B, SC8551_REG_RST_MASK, val);
}

static int sc8551_enable_wdt(struct sc8551 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8551_WATCHDOG_ENABLE;
	else
		val = SC8551_WATCHDOG_DISABLE;

	val <<= SC8551_WATCHDOG_DIS_SHIFT;

	ret = sc8551_update_bits(sc, SC8551_REG_0B,
				 SC8551_WATCHDOG_DIS_MASK, val);
	return ret;
}

static int sc8551_set_wdt(struct sc8551 *sc, int ms)
{
	int ret;
	u8 val;

	if (ms == 500)
		val = SC8551_WATCHDOG_0P5S;
	else if (ms == 1000)
		val = SC8551_WATCHDOG_1S;
	else if (ms == 5000)
		val = SC8551_WATCHDOG_5S;
	else
		val = SC8551_WATCHDOG_30S;

	val <<= SC8551_WATCHDOG_SHIFT;

	ret = sc8551_update_bits(sc, SC8551_REG_0B,
				 SC8551_WATCHDOG_MASK, val);
	return ret;
}

static int sc8551_enable_batovp(struct sc8551 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8551_BAT_OVP_ENABLE;
	else
		val = SC8551_BAT_OVP_DISABLE;

	val <<= SC8551_BAT_OVP_DIS_SHIFT;

	ret = sc8551_update_bits(sc, SC8551_REG_00,
				 SC8551_BAT_OVP_DIS_MASK, val);
	return ret;
}

static int sc8551_set_batovp_th(struct sc8551 *sc, int threshold)
{
	int ret;
	u8 val;

	if (threshold < SC8551_BAT_OVP_BASE)
		threshold = SC8551_BAT_OVP_BASE;
	else if (threshold > SC8551_BAT_OVP_MAX)
		threshold = SC8551_BAT_OVP_MAX;

	val = (threshold - SC8551_BAT_OVP_BASE) / SC8551_BAT_OVP_LSB;

	val <<= SC8551_BAT_OVP_SHIFT;

	ret = sc8551_update_bits(sc, SC8551_REG_00,
				 SC8551_BAT_OVP_MASK, val);
	return ret;
}

static int sc8551_enable_batocp(struct sc8551 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8551_BAT_OCP_ENABLE;
	else
		val = SC8551_BAT_OCP_DISABLE;

	val <<= SC8551_BAT_OCP_DIS_SHIFT;

	ret = sc8551_update_bits(sc, SC8551_REG_02,
				 SC8551_BAT_OCP_DIS_MASK, val);
	return ret;
}

static int sc8551_set_batocp_th(struct sc8551 *sc, int threshold)
{
	int ret;
	u8 val;

	if (threshold < SC8551_BAT_OCP_BASE)
		threshold = SC8551_BAT_OCP_BASE;
	else if (threshold > SC8551_BAT_OCP_MAX)
		threshold = SC8551_BAT_OCP_MAX;

	val = (threshold - SC8551_BAT_OCP_BASE) / SC8551_BAT_OCP_LSB;

	val <<= SC8551_BAT_OCP_SHIFT;

	ret = sc8551_update_bits(sc, SC8551_REG_02,
				 SC8551_BAT_OCP_MASK, val);
	return ret;
}

static int sc8551_set_busovp_th(struct sc8551 *sc, int threshold)
{
	int ret;
	u8 val;

	if (threshold < SC8551_BUS_OVP_BASE)
		threshold = SC8551_BUS_OVP_BASE;
	else if (threshold > SC8551_BUS_OVP_MAX)
		threshold = SC8551_BUS_OVP_MAX;

	val = (threshold - SC8551_BUS_OVP_BASE) / SC8551_BUS_OVP_LSB;

	val <<= SC8551_BUS_OVP_SHIFT;

	ret = sc8551_update_bits(sc, SC8551_REG_06,
				 SC8551_BUS_OVP_MASK, val);
	return ret;
}

static int sc8551_enable_busocp(struct sc8551 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8551_BUS_OCP_ENABLE;
	else
		val = SC8551_BUS_OCP_DISABLE;

	val <<= SC8551_BUS_OCP_DIS_SHIFT;

	ret = sc8551_update_bits(sc, SC8551_REG_08,
				 SC8551_BUS_OCP_DIS_MASK, val);
	return ret;
}

static int sc8551_set_busocp_th(struct sc8551 *sc, int threshold)
{
	int ret;
	u8 val;

	if (threshold < SC8551_BUS_OCP_BASE)
		threshold = SC8551_BUS_OCP_BASE;
	else if (threshold > SC8551_BUS_OCP_MAX)
		threshold = SC8551_BUS_OCP_MAX;

	val = (threshold - SC8551_BUS_OCP_BASE) / SC8551_BUS_OCP_LSB;

	val <<= SC8551_BUS_OCP_SHIFT;

	ret = sc8551_update_bits(sc, SC8551_REG_08,
				 SC8551_BUS_OCP_MASK, val);
	return ret;
}

static int sc8551_set_acovp_th(struct sc8551 *sc, int threshold)
{
	int ret;
	u8 val;

	if (threshold < SC8551_AC_OVP_BASE)
		threshold = SC8551_AC_OVP_BASE;
	else if (threshold > SC8551_AC_OVP_MAX)
		threshold = SC8551_AC_OVP_MAX;

	val = (threshold - SC8551_AC_OVP_BASE) / SC8551_AC_OVP_LSB;

	val <<= SC8551_AC_OVP_SHIFT;

	ret = sc8551_update_bits(sc, SC8551_REG_05,
				 SC8551_AC_OVP_MASK, val);

	return ret;
}

static int sc8551_set_vdrop_th(struct sc8551 *sc, int threshold)
{
	int ret;
	u8 val;

	if (threshold == 300)
		val = SC8551_VDROP_THRESHOLD_300MV;
	else
		val = SC8551_VDROP_THRESHOLD_400MV;

	val <<= SC8551_VDROP_THRESHOLD_SET_SHIFT;

	ret = sc8551_update_bits(sc, SC8551_REG_05,
				 SC8551_VDROP_THRESHOLD_SET_MASK,
				 val);

	return ret;
}

static int sc8551_set_vdrop_deglitch(struct sc8551 *sc, int us)
{
	int ret;
	u8 val;

	if (us == 8)
		val = SC8551_VDROP_DEGLITCH_8US;
	else
		val = SC8551_VDROP_DEGLITCH_5MS;

	val <<= SC8551_VDROP_DEGLITCH_SET_SHIFT;

	ret = sc8551_update_bits(sc, SC8551_REG_05,
				 SC8551_VDROP_DEGLITCH_SET_MASK,
				 val);
	return ret;
}

static int sc8551_enable_bat_therm(struct sc8551 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8551_TSBAT_ENABLE;
	else
		val = SC8551_TSBAT_DISABLE;

	val <<= SC8551_TSBAT_DIS_SHIFT;

	ret = sc8551_update_bits(sc, SC8551_REG_0C,
				 SC8551_TSBAT_DIS_MASK, val);
	return ret;
}

static int sc8551_enable_bus_therm(struct sc8551 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8551_TSBUS_ENABLE;
	else
		val = SC8551_TSBUS_DISABLE;

	val <<= SC8551_TSBUS_DIS_SHIFT;

	ret = sc8551_update_bits(sc, SC8551_REG_0C,
				 SC8551_TSBUS_DIS_MASK, val);
	return ret;
}

static int sc8551_enable_adc(struct sc8551 *sc, bool enable)
{
	int ret;
	u8 val;

	if (enable)
		val = SC8551_ADC_ENABLE;
	else
		val = SC8551_ADC_DISABLE;

	val <<= SC8551_ADC_EN_SHIFT;

	ret = sc8551_update_bits(sc, SC8551_REG_14,
				 SC8551_ADC_EN_MASK, val);
	return ret;
}

static int sc8551_set_adc_scanrate(struct sc8551 *sc, bool oneshot)
{
	int ret;
	u8 val;

	if (oneshot)
		val = SC8551_ADC_RATE_ONESHOT;
	else
		val = SC8551_ADC_RATE_CONTINUOUS;

	val <<= SC8551_ADC_RATE_SHIFT;

	ret = sc8551_update_bits(sc, SC8551_REG_14,
				 SC8551_ADC_EN_MASK, val);
	return ret;
}

static int sc8551_get_adc_data(struct sc8551 *sc, int channel,  int *result)
{
	int ret;
	u8 val_l, val_h;
	u16 val;

	if (channel >= ADC_MAX_NUM)
		return 0;

	ret = sc8551_read_byte(sc, ADC_REG_BASE + (channel << 1), &val_h);
	ret = sc8551_read_byte(sc, ADC_REG_BASE + (channel << 1) + 1, &val_l);

	if (ret < 0)
		return ret;

	val = (val_h << 8) | val_l;
	if (sc->is_sc8551) {
		if (channel == ADC_IBUS)
			val = DIV_ROUND_CLOSEST(val * 15625, 10000);
		else if (channel == ADC_VBUS)
			val = DIV_ROUND_CLOSEST(val * 375, 100);
		else if (channel == ADC_VAC)
			val = val * 5;
		else if (channel == ADC_VOUT)
			val = DIV_ROUND_CLOSEST(val * 125, 100);
		else if (channel == ADC_VBAT)
			val = DIV_ROUND_CLOSEST(val * 125, 100);
		else if (channel == ADC_IBAT)
			val = DIV_ROUND_CLOSEST(val * 3125, 1000);
		else if (channel == ADC_TDIE)
			val = DIV_ROUND_CLOSEST(val * 5, 10);
	}

	*result = val;

	return 0;
}

static int sc8551_get_ibat_now_mA(struct sc8551 *sc, int *batt_ma)
{
	struct power_supply *fuel_gauge;
	union power_supply_propval fgu_val;
	int ret = 0;

	if (sc->cfg->psy_fuel_gauge) {
		fuel_gauge = power_supply_get_by_name(sc->cfg->psy_fuel_gauge);
		if (!fuel_gauge)
			return -ENODEV;

		fgu_val.intval = CM_IBAT_CURRENT_NOW_CMD;
		ret = power_supply_get_property(fuel_gauge, POWER_SUPPLY_PROP_CURRENT_NOW,
						&fgu_val);
		power_supply_put(fuel_gauge);
		if (ret) {
			dev_err(sc->dev, "%s, get batt_ma error, ret=%d\n", __func__, ret);
			return ret;
		}

		*batt_ma = DIV_ROUND_CLOSEST(fgu_val.intval, 1000);
	} else {
		ret = sc8551_get_adc_data(sc, ADC_IBAT, batt_ma);
		if (ret)
			dev_err(sc->dev, "%s, get batt_ma error, ret=%d\n", __func__, ret);
	}

	return ret;
}

static int sc8551_set_adc_scan(struct sc8551 *sc, int channel, bool enable)
{
	int ret;
	u8 reg;
	u8 mask;
	u8 shift;
	u8 val;

	if (channel > ADC_MAX_NUM)
		return -EINVAL;

	if (channel == ADC_IBUS) {
		reg = SC8551_REG_14;
		shift = SC8551_IBUS_ADC_DIS_SHIFT;
		mask = SC8551_IBUS_ADC_DIS_MASK;
	} else {
		reg = SC8551_REG_15;
		shift = 8 - channel;
		mask = 1 << shift;
	}

	if (enable)
		val = 0 << shift;
	else
		val = 1 << shift;

	ret = sc8551_update_bits(sc, reg, mask, val);

	return ret;
}

static int sc8551_set_alarm_int_mask(struct sc8551 *sc, u8 mask)
{
	int ret;
	u8 val;

	ret = sc8551_read_byte(sc, SC8551_REG_0F, &val);
	if (ret)
		return ret;

	val |= mask;

	return sc8551_write_byte(sc, SC8551_REG_0F, val);
}

static int sc8551_set_sense_resistor(struct sc8551 *sc, int r_mohm)
{
	int ret;
	u8 val;

	if (r_mohm == 2)
		val = SC8551_SET_IBAT_SNS_RES_2MHM;
	else if (r_mohm == 5)
		val = SC8551_SET_IBAT_SNS_RES_5MHM;
	else
		return -EINVAL;

	val <<= SC8551_SET_IBAT_SNS_RES_SHIFT;

	ret = sc8551_update_bits(sc, SC8551_REG_2B,
				 SC8551_SET_IBAT_SNS_RES_MASK,
				 val);
	return ret;
}

static int sc8551_disable_regulation(struct sc8551 *sc, bool disable)
{
	int ret;
	u8 val;

	if (disable)
		val = SC8551_EN_REGULATION_DISABLE;
	else
		val = SC8551_EN_REGULATION_ENABLE;

	val <<= SC8551_EN_REGULATION_SHIFT;

	ret = sc8551_update_bits(sc, SC8551_REG_2B,
				 SC8551_EN_REGULATION_MASK,
				 val);

	return ret;
}

static int sc8551_set_ss_timeout(struct sc8551 *sc, int timeout)
{
	int ret;
	u8 val;

	switch (timeout) {
	case 0:
		val = SC8551_SS_TIMEOUT_DISABLE;
		break;
	case 40:
		val = SC8551_SS_TIMEOUT_40MS;
		break;
	case 80:
		val = SC8551_SS_TIMEOUT_80MS;
		break;
	case 320:
		val = SC8551_SS_TIMEOUT_320MS;
		break;
	case 1280:
		val = SC8551_SS_TIMEOUT_1280MS;
		break;
	case 5120:
		val = SC8551_SS_TIMEOUT_5120MS;
		break;
	case 20480:
		val = SC8551_SS_TIMEOUT_20480MS;
		break;
	case 81920:
		val = SC8551_SS_TIMEOUT_81920MS;
		break;
	default:
		val = SC8551_SS_TIMEOUT_DISABLE;
		break;
	}

	val <<= SC8551_SS_TIMEOUT_SET_SHIFT;

	ret = sc8551_update_bits(sc, SC8551_REG_2B,
				 SC8551_SS_TIMEOUT_SET_MASK,
				 val);

	return ret;
}

static int sc8551_set_ibat_reg_th(struct sc8551 *sc, int th_ma)
{
	int ret;
	u8 val;

	if (th_ma == 200)
		val = SC8551_IBAT_REG_200MA;
	else if (th_ma == 300)
		val = SC8551_IBAT_REG_300MA;
	else if (th_ma == 400)
		val = SC8551_IBAT_REG_400MA;
	else
		val = SC8551_IBAT_REG_500MA;

	val <<= SC8551_IBAT_REG_SHIFT;
	ret = sc8551_update_bits(sc, SC8551_REG_2C,
				 SC8551_IBAT_REG_MASK,
				 val);

	return ret;
}

static int sc8551_set_vbat_reg_th(struct sc8551 *sc, int th_mv)
{
	int ret;
	u8 val;

	if (th_mv == 50)
		val = SC8551_VBAT_REG_50MV;
	else if (th_mv == 100)
		val = SC8551_VBAT_REG_100MV;
	else if (th_mv == 150)
		val = SC8551_VBAT_REG_150MV;
	else
		val = SC8551_VBAT_REG_200MV;

	val <<= SC8551_VBAT_REG_SHIFT;

	ret = sc8551_update_bits(sc, SC8551_REG_2C,
				 SC8551_VBAT_REG_MASK,
				 val);

	return ret;
}

static int sc8551_set_ibus_ucp(struct sc8551 *sc, int val)
{
	int ret;

	if (val < SC8551_IBUS_LOW_DG_10US)
		val = SC8551_IBUS_LOW_DG_10US;
	else if (val > SC8551_IBUS_LOW_DG_5MS)
		val = SC8551_IBUS_LOW_DG_5MS;

	val <<= SC8551_IBUS_LOW_DG_SHIFT;

	ret = sc8551_update_bits(sc, SC8551_REG_2E, SC8551_IBUS_LOW_DG_MASK, val);
	return ret;
}

static int sc8551_get_work_mode(struct sc8551 *sc, int *mode)
{
	int ret;
	u8 val;

	ret = sc8551_read_byte(sc, SC8551_REG_0C, &val);
	if (ret) {
		dev_err(sc->dev, "Failed to read operation mode register\n");
		return ret;
	}

	val = (val & SC8551_MS_MASK) >> SC8551_MS_SHIFT;
	if (val == SC8551_MS_MASTER)
		*mode = SC8551_ROLE_MASTER;
	else if (val == SC8551_MS_SLAVE)
		*mode = SC8551_ROLE_SLAVE;
	else
		*mode = SC8551_ROLE_STDALONE;

	dev_info(sc->dev, "work mode:%s\n", *mode == SC8551_ROLE_STDALONE ? "Standalone" :
		 (*mode == SC8551_ROLE_SLAVE ? "Slave" : "Master"));
	return ret;
}

static int sc8551_check_vbus_error_status(struct sc8551 *sc)
{
	int ret;
	u8 data;

	sc->bus_err_lo = false;
	sc->bus_err_hi = false;

	ret = sc8551_read_byte(sc, SC8551_REG_0A, &data);
	if (ret == 0) {
		dev_err(sc->dev, "vbus error >>>>%02x\n", data);
		sc->bus_err_lo = !!(data & SC8551_VBUS_ERRORLO_STAT_MASK);
		sc->bus_err_hi = !!(data & SC8551_VBUS_ERRORHI_STAT_MASK);
		sc->vbus_error = data;
	}

	return ret;
}

static int sc8551_detect_device(struct sc8551 *sc)
{
	int ret;
	u8 data;

	ret = sc8551_read_byte(sc, SC8551_REG_13, &data);
	if (ret < 0) {
		dev_err(sc->dev, "Failed to get device id, ret = %d\n", ret);
		return ret;
	}

	sc->part_no = (data & SC8551_DEV_ID_MASK);
	sc->part_no >>= SC8551_DEV_ID_SHIFT;

	if (sc->part_no != SC8551_DEV_ID) {
		dev_err(sc->dev, "The device id is 0x%x\n", sc->part_no);
		ret = -EINVAL;
	}

	return ret;
}

static int sc8551_parse_dt(struct sc8551 *sc, struct device *dev)
{
	int ret;
	struct device_node *np = dev->of_node;

	sc->cfg = devm_kzalloc(dev, sizeof(struct sc8551_cfg), GFP_KERNEL);
	if (!sc->cfg)
		return -ENOMEM;

	sc->cfg->bat_ovp_disable = of_property_read_bool(np,
			"sc,sc8551,bat-ovp-disable");
	sc->cfg->bat_ocp_disable = of_property_read_bool(np,
			"sc,sc8551,bat-ocp-disable");
	sc->cfg->bat_ovp_alm_disable = of_property_read_bool(np,
			"sc,sc8551,bat-ovp-alarm-disable");
	sc->cfg->bat_ocp_alm_disable = of_property_read_bool(np,
			"sc,sc8551,bat-ocp-alarm-disable");
	sc->cfg->bus_ocp_disable = of_property_read_bool(np,
			"sc,sc8551,bus-ocp-disable");
	sc->cfg->bus_ovp_alm_disable = of_property_read_bool(np,
			"sc,sc8551,bus-ovp-alarm-disable");
	sc->cfg->bus_ocp_alm_disable = of_property_read_bool(np,
			"sc,sc8551,bus-ocp-alarm-disable");
	sc->cfg->bat_ucp_alm_disable = of_property_read_bool(np,
			"sc,sc8551,bat-ucp-alarm-disable");
	sc->cfg->bat_therm_disable = of_property_read_bool(np,
			"sc,sc8551,bat-therm-disable");
	sc->cfg->bus_therm_disable = of_property_read_bool(np,
			"sc,sc8551,bus-therm-disable");

	ret = of_property_read_u32(np, "sc,sc8551,bat-ovp-threshold",
				   &sc->cfg->bat_ovp_th);
	if (ret) {
		dev_err(sc->dev, "failed to read bat-ovp-threshold\n");
		return ret;
	}
	ret = of_property_read_u32(np, "sc,sc8551,bat-ovp-alarm-threshold",
				   &sc->cfg->bat_ovp_alm_th);
	if (ret) {
		dev_err(sc->dev, "failed to read bat-ovp-alarm-threshold\n");
		return ret;
	}
	sc->cfg->bat_ovp_default_alm_th = sc->cfg->bat_ovp_alm_th;

	ret = of_property_read_u32(np, "sc,sc8551,bat-ocp-threshold",
				   &sc->cfg->bat_ocp_th);
	if (ret) {
		dev_err(sc->dev, "failed to read bat-ocp-threshold\n");
		return ret;
	}
	ret = of_property_read_u32(np, "sc,sc8551,bat-ocp-alarm-threshold",
				   &sc->cfg->bat_ocp_alm_th);
	if (ret) {
		dev_err(sc->dev, "failed to read bat-ocp-alarm-threshold\n");
		return ret;
	}
	ret = of_property_read_u32(np, "sc,sc8551,bus-ovp-threshold",
				   &sc->cfg->bus_ovp_th);
	if (ret) {
		dev_err(sc->dev, "failed to read bus-ovp-threshold\n");
		return ret;
	}
	ret = of_property_read_u32(np, "sc,sc8551,bus-ovp-alarm-threshold",
				   &sc->cfg->bus_ovp_alm_th);
	if (ret) {
		dev_err(sc->dev, "failed to read bus-ovp-alarm-threshold\n");
		return ret;
	}
	ret = of_property_read_u32(np, "sc,sc8551,bus-ocp-threshold",
				   &sc->cfg->bus_ocp_th);
	if (ret) {
		dev_err(sc->dev, "failed to read bus-ocp-threshold\n");
		return ret;
	}
	ret = of_property_read_u32(np, "sc,sc8551,bus-ocp-alarm-threshold",
				   &sc->cfg->bus_ocp_alm_th);
	if (ret) {
		dev_err(sc->dev, "failed to read bus-ocp-alarm-threshold\n");
		return ret;
	}
	ret = of_property_read_u32(np, "sc,sc8551,bat-ucp-alarm-threshold",
				   &sc->cfg->bat_ucp_alm_th);
	if (ret) {
		dev_err(sc->dev, "failed to read bat-ucp-alarm-threshold\n");
		return ret;
	}
	ret = of_property_read_u32(np, "sc,sc8551,bat-therm-threshold",
				   &sc->cfg->bat_therm_th);
	if (ret) {
		dev_err(sc->dev, "failed to read bat-therm-threshold\n");
		return ret;
	}
	ret = of_property_read_u32(np, "sc,sc8551,bus-therm-threshold",
				   &sc->cfg->bus_therm_th);
	if (ret) {
		dev_err(sc->dev, "failed to read bus-therm-threshold\n");
		return ret;
	}
	ret = of_property_read_u32(np, "sc,sc8551,die-therm-threshold",
				   &sc->cfg->die_therm_th);
	if (ret) {
		dev_err(sc->dev, "failed to read die-therm-threshold\n");
		return ret;
	}

	ret = of_property_read_u32(np, "sc,sc8551,ac-ovp-threshold",
				   &sc->cfg->ac_ovp_th);
	if (ret) {
		dev_err(sc->dev, "failed to read ac-ovp-threshold\n");
		return ret;
	}

	ret = of_property_read_u32(np, "sc,sc8551,sense-resistor-mohm",
				   &sc->cfg->sense_r_mohm);
	if (ret) {
		dev_err(sc->dev, "failed to read sense-resistor-mohm\n");
		return ret;
	}

	ret = of_property_read_u32(np, "sc,sc8551,ibat-regulation-threshold",
				   &sc->cfg->ibat_reg_th);
	if (ret) {
		dev_err(sc->dev, "failed to read ibat-regulation-threshold\n");
		return ret;
	}

	ret = of_property_read_u32(np, "sc,sc8551,vbat-regulation-threshold",
				   &sc->cfg->vbat_reg_th);
	if (ret) {
		dev_err(sc->dev, "failed to read vbat-regulation-threshold\n");
		return ret;
	}

	ret = of_property_read_u32(np, "sc,sc8551,vdrop-threshold",
				   &sc->cfg->vdrop_th);
	if (ret) {
		dev_err(sc->dev, "failed to read vdrop-threshold\n");
		return ret;
	}

	ret = of_property_read_u32(np, "sc,sc8551,vdrop-deglitch",
				   &sc->cfg->vdrop_deglitch);
	if (ret) {
		dev_err(sc->dev, "failed to read vdrop-deglitch\n");
		return ret;
	}

	sc->cfg->regulation_disable = of_property_read_bool(np, "sc,sc8551,regulation_disable");
	ret = of_property_read_u32(np, "sc,sc8551,ss-timeout",
				   &sc->cfg->ss_timeout);
	if (ret) {
		dev_err(sc->dev, "failed to read ss-timeout\n");
		return ret;
	}

	ret = of_property_read_u32(np, "sc,sc8551,watchdog-timer",
				   &sc->cfg->wdt_timer);
	if (ret) {
		dev_err(sc->dev, "failed to read watchdog-timer\n");
		return ret;
	}

	ret = of_property_read_u32(np, "sc,sc8551,ibus-ucp",
				   &sc->cfg->ibus_ucp);
	if (ret) {
		dev_err(sc->dev, "failed to read ibus-ucp\n");
		return ret;
	}

	if (sc->cfg->bat_ovp_th && sc->cfg->bat_ovp_alm_th) {
		sc->cfg->bat_delta_volt = sc->cfg->bat_ovp_th - sc->cfg->bat_ovp_alm_th;
		if (sc->cfg->bat_delta_volt < 0)
			sc->cfg->bat_delta_volt = 0;
	}

	ret = of_get_named_gpio(np, "sc,sc8551,interrupt_gpios", 0);
	if (ret < 0) {
		dev_err(sc->dev, "no intr_gpio info\n");
		return ret;
	}
	sc->irq_gpio = ret;

	return 0;
}

static int sc8551_init_protection(struct sc8551 *sc)
{
	int ret;

	ret = sc8551_enable_batovp(sc, !sc->cfg->bat_ovp_disable);
	dev_info(sc->dev, "%s bat ovp %s\n",
		 sc->cfg->bat_ovp_disable ? "disable" : "enable",
		 !ret ? "successfullly" : "failed");

	ret = sc8551_enable_batocp(sc, !sc->cfg->bat_ocp_disable);
	dev_info(sc->dev, "%s bat ocp %s\n",
		 sc->cfg->bat_ocp_disable ? "disable" : "enable",
		 !ret ? "successfullly" : "failed");

	ret = sc8551_enable_busocp(sc, !sc->cfg->bus_ocp_disable);
	dev_info(sc->dev, "%s bus ocp %s\n",
		 sc->cfg->bus_ocp_disable ? "disable" : "enable",
		 !ret ? "successfullly" : "failed");

	ret = sc8551_enable_bat_therm(sc, !sc->cfg->bat_therm_disable);
	dev_info(sc->dev, "%s bat therm %s\n",
		 sc->cfg->bat_therm_disable ? "disable" : "enable",
		 !ret ? "successfullly" : "failed");

	ret = sc8551_enable_bus_therm(sc, !sc->cfg->bus_therm_disable);
	dev_info(sc->dev, "%s bus therm %s\n",
		 sc->cfg->bus_therm_disable ? "disable" : "enable",
		 !ret ? "successfullly" : "failed");

	ret = sc8551_set_batovp_th(sc, sc->cfg->bat_ovp_th);
	dev_info(sc->dev, "set bat ovp th %d %s\n", sc->cfg->bat_ovp_th,
		 !ret ? "successfully" : "failed");

	sc->cfg->bat_ovp_alm_th = sc->cfg->bat_ovp_default_alm_th;

	ret = sc8551_set_batocp_th(sc, sc->cfg->bat_ocp_th);
	dev_info(sc->dev, "set bat ocp threshold %d %s\n", sc->cfg->bat_ocp_th,
		 !ret ? "successfully" : "failed");

	ret = sc8551_set_busovp_th(sc, sc->cfg->bus_ovp_th);
	dev_info(sc->dev, "set bus ovp threshold %d %s\n", sc->cfg->bus_ovp_th,
		 !ret ? "successfully" : "failed");

	ret = sc8551_set_busocp_th(sc, sc->cfg->bus_ocp_th);
	dev_info(sc->dev, "set bus ocp threshold %d %s\n", sc->cfg->bus_ocp_th,
		 !ret ? "successfully" : "failed");

	ret = sc8551_set_acovp_th(sc, sc->cfg->ac_ovp_th);
	dev_info(sc->dev, "set ac ovp threshold %d %s\n", sc->cfg->ac_ovp_th,
		 !ret ? "successfully" : "failed");

	return 0;
}

static int sc8551_init_adc(struct sc8551 *sc)
{
	sc8551_set_adc_scanrate(sc, false);
	sc8551_set_adc_scan(sc, ADC_IBUS, true);
	sc8551_set_adc_scan(sc, ADC_VBUS, true);
	sc8551_set_adc_scan(sc, ADC_VOUT, false);
	sc8551_set_adc_scan(sc, ADC_VBAT, true);
	sc8551_set_adc_scan(sc, ADC_IBAT, true);
	sc8551_set_adc_scan(sc, ADC_TBUS, true);
	sc8551_set_adc_scan(sc, ADC_TBAT, true);
	sc8551_set_adc_scan(sc, ADC_TDIE, true);
	sc8551_set_adc_scan(sc, ADC_VAC, true);

	sc8551_enable_adc(sc, true);

	return 0;
}

static int sc8551_init_int_src(struct sc8551 *sc)
{
	int ret;
	/*TODO:be careful ts bus and ts bat alarm bit mask is in
	 *	fault mask register, so you need call
	 *	sc8551_set_fault_int_mask for tsbus and tsbat alarm
	 */
	ret = sc8551_set_alarm_int_mask(sc, ADC_DONE |
					VBAT_INSERT |
					VBUS_INSERT);
	if (ret) {
		dev_err(sc->dev, "failed to set alarm mask:%d\n", ret);
		return ret;
	}

	return ret;
}

static int sc8551_init_regulation(struct sc8551 *sc)
{
	sc8551_set_ibat_reg_th(sc, sc->cfg->ibat_reg_th);
	sc8551_set_vbat_reg_th(sc, sc->cfg->vbat_reg_th);

	sc8551_set_vdrop_deglitch(sc, sc->cfg->vdrop_deglitch);
	sc8551_set_vdrop_th(sc, sc->cfg->vdrop_th);

	sc8551_disable_regulation(sc, sc->cfg->regulation_disable);

	sc8551_set_ibus_ucp(sc, sc->cfg->ibus_ucp);

	return 0;
}

static int sc8551_init_device(struct sc8551 *sc)
{
	sc8551_reset(sc, false);
	sc8551_enable_wdt(sc, false);

	sc8551_set_ss_timeout(sc, sc->cfg->ss_timeout);
	sc8551_set_sense_resistor(sc, sc->cfg->sense_r_mohm);

	sc8551_init_protection(sc);
	sc8551_init_adc(sc);
	sc8551_init_int_src(sc);

	sc8551_init_regulation(sc);

	return 0;
}

static int sc8551_set_present(struct sc8551 *sc, bool present)
{
	int ret;

	sc->usb_present = present;

	if (present) {
		sc8551_init_device(sc);
		ret = sc8551_set_wdt(sc, sc->cfg->wdt_timer);
		if (ret) {
			dev_err(sc->dev, "%s, faied to set wdt time, ret=%d\n", __func__, ret);
			return ret;
		}

		ret = sc8551_enable_wdt(sc, true);
		if (ret) {
			dev_err(sc->dev, "%s, faied to enable wdt, ret=%d\n", __func__, ret);
			return ret;
		}
		schedule_delayed_work(&sc->wdt_work, 0);
	}
	return 0;
}

static ssize_t sc8551_show_registers(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct sc8551 *sc = dev_get_drvdata(dev);
	u8 addr;
	u8 val;
	u8 tmpbuf[300];
	int len;
	int idx = 0;
	int ret;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "sc8551");
	for (addr = 0x0; addr <= 0x31; addr++) {
		ret = sc8551_read_byte(sc, addr, &val);
		if (ret == 0) {
			len = snprintf(tmpbuf, PAGE_SIZE - idx,
				       "Reg[%.2X] = 0x%.2x\n", addr, val);
			memcpy(&buf[idx], tmpbuf, len);
			idx += len;
		}
	}

	return idx;
}

static ssize_t sc8551_store_register(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf,
				     size_t count)
{
	struct sc8551 *sc = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg <= 0x31)
		sc8551_write_byte(sc, (unsigned char)reg, (unsigned char)val);

	return count;
}
static DEVICE_ATTR(registers, 0644, sc8551_show_registers, sc8551_store_register);

static int sc8551_create_device_node(struct device *dev)
{
	return device_create_file(dev, &dev_attr_registers);
}

static enum power_supply_property sc8551_charger_props[] = {
	POWER_SUPPLY_PROP_CALIBRATE,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
};

static void sc8551_check_alarm_status(struct sc8551 *sc);
static void sc8551_check_fault_status(struct sc8551 *sc);

static int sc8551_get_present_status(struct sc8551 *sc, int *intval)
{
	int ret = 0;
	u8 reg_val;
	bool result = false;

	if (*intval == CM_USB_PRESENT_CMD) {
		result = sc->usb_present;
	} else if (*intval == CM_BATTERY_PRESENT_CMD) {
		ret = sc8551_read_byte(sc, SC8551_REG_0D, &reg_val);
		if (!ret)
			sc->batt_present = !!(reg_val & VBAT_INSERT);
		result = sc->batt_present;
	} else if (*intval == CM_VBUS_PRESENT_CMD) {
		ret = sc8551_read_byte(sc, SC8551_REG_0D, &reg_val);
		if (!ret)
			sc->vbus_present  = !!(reg_val & VBUS_INSERT);
		result = sc->vbus_present;
	} else {
		dev_err(sc->dev, "get present cmd = %d is error\n", *intval);
	}

	*intval = result;

	return ret;
}

static int sc8551_get_temperature(struct sc8551 *sc, int *intval)
{
	int ret = 0;
	int result = 0;

	if (*intval == CMD_BATT_TEMP_CMD) {
		ret = sc8551_get_adc_data(sc, ADC_TBAT, &result);
		if (!ret)
			sc->bat_temp = result;
	} else if (*intval == CM_BUS_TEMP_CMD) {
		ret = sc8551_get_adc_data(sc, ADC_TBUS, &result);
		if (!ret)
			sc->bus_temp = result;
	} else if (*intval == CM_DIE_TEMP_CMD) {
		ret = sc8551_get_adc_data(sc, ADC_TDIE, &result);
		if (!ret)
			sc->die_temp = result;
	} else {
		dev_err(sc->dev, "get temperature cmd = %d is error\n", *intval);
	}

	*intval = result;

	return ret;
}

static void sc8551_clear_alarm_status(struct sc8551 *sc)
{
	sc->bat_ucp_alarm = false;
	sc->bat_ocp_alarm = false;
	sc->bat_ovp_alarm = false;
	sc->bus_ovp_alarm = false;
	sc->bus_ocp_alarm = false;
}

static int sc8551_get_alarm_status(struct sc8551 *sc)
{
	int ret, batt_ma, batt_mv, vbus_mv, ibus_ma;

	ret = sc8551_get_ibat_now_mA(sc, &batt_ma);
	if (ret) {
		dev_err(sc->dev, "%s[%d], get batt_ma error, ret=%d\n",
			__func__, __LINE__, ret);
		return ret;
	}

	ret = sc8551_get_adc_data(sc, ADC_VBAT, &batt_mv);
	if (ret) {
		dev_err(sc->dev, "%s[%d], get batt_mv error, ret=%d\n",
			__func__, __LINE__, ret);
		return ret;
	}

	ret = sc8551_get_adc_data(sc, ADC_VBUS, &vbus_mv);
	if (ret) {
		dev_err(sc->dev, "%s[%d], get vbus_mv error, ret=%d\n",
			__func__, __LINE__, ret);
		return ret;
	}

	ret = sc8551_get_adc_data(sc, ADC_IBUS, &ibus_ma);
	if (ret) {
		dev_err(sc->dev, "%s[%d], get ibus_ma error, ret=%d\n",
			__func__, __LINE__, ret);
		return ret;
	}

	if (!sc->cfg->bat_ucp_alm_disable && sc->cfg->bat_ucp_alm_th > 0 &&
	    batt_ma < sc->cfg->bat_ucp_alm_th)
		sc->bat_ucp_alarm = true;

	if (!sc->cfg->bat_ocp_alm_disable && sc->cfg->bat_ocp_alm_th > 0 &&
	    batt_ma > sc->cfg->bat_ocp_alm_th)
		sc->bat_ocp_alarm = true;

	if (!sc->cfg->bat_ovp_alm_disable && sc->cfg->bat_ovp_alm_th > 0 &&
	    batt_mv > sc->cfg->bat_ovp_alm_th)
		sc->bat_ovp_alarm = true;

	if (!sc->cfg->bus_ovp_alm_disable && sc->cfg->bus_ovp_alm_th > 0 &&
	    vbus_mv > sc->cfg->bus_ovp_alm_th)
		sc->bus_ovp_alarm = true;

	if (!sc->cfg->bus_ocp_alm_disable && sc->cfg->bus_ocp_alm_th > 0 &&
	    ibus_ma > sc->cfg->bus_ocp_alm_th)
		sc->bus_ocp_alarm = true;

	dev_dbg(sc->dev, "%s, batt_ma = %d, batt_mv = %d, vbus_mv = %d, ibus_ma = %d,"
		" bat_ucp_alarm = %d, bat_ocp_alarm = %d, bat_ovp_alarm = %d,"
		" bus_ovp_alarm = %d, bus_ocp_alarm = %d\n",
		__func__, batt_ma, batt_mv, vbus_mv, ibus_ma, sc->bat_ucp_alarm,
		sc->bat_ocp_alarm, sc->bat_ovp_alarm, sc->bus_ovp_alarm, sc->bus_ocp_alarm);

	return 0;
}

static void sc8551_charger_watchdog_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct sc8551 *sc = container_of(dwork, struct sc8551, wdt_work);

	if (sc8551_set_wdt(sc, sc->cfg->wdt_timer) < 0)
		dev_err(sc->dev, "Fail to feed watchdog\n");

	schedule_delayed_work(&sc->wdt_work, HZ * 15);
}

static int sc8551_charger_get_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	struct sc8551 *sc = power_supply_get_drvdata(psy);
	int result = 0;
	int ret, cmd;
	u8 reg_val;

	if (!sc) {
		pr_err("%s[%d], NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}

	dev_dbg(sc->dev, ">>>>>psp = %d\n", psp);

	switch (psp) {
	case POWER_SUPPLY_PROP_CALIBRATE:
		if (!sc8551_check_charge_enabled(sc, &sc->charge_enabled))
			val->intval = sc->charge_enabled;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		cmd = val->intval;
		if (!sc8551_get_present_status(sc, &val->intval))
			dev_err(sc->dev, "fail to get present status, cmd = %d\n", cmd);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		ret = sc8551_read_byte(sc, SC8551_REG_0D, &reg_val);
		if (!ret)
			sc->vbus_present  = !!(reg_val & VBUS_INSERT);
		val->intval = sc->vbus_present;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = sc8551_get_adc_data(sc, ADC_VBAT, &result);
		if (!ret)
			sc->vbat_volt = result;

		val->intval = sc->vbat_volt * 1000;
		dev_dbg(sc->dev, "ADC_VBAT = %d\n", val->intval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		if (val->intval == CM_IBAT_CURRENT_NOW_CMD) {
			if (!sc8551_get_ibat_now_mA(sc, &result))
				sc->ibat_curr = result;

			val->intval = sc->ibat_curr * 1000;
			dev_dbg(sc->dev, "ADC_IBAT = %d\n", val->intval);
			break;
		}

		if (!sc8551_check_charge_enabled(sc, &sc->charge_enabled)) {
			if (!sc->charge_enabled) {
				val->intval = 0;
			} else {
				ret = sc8551_get_adc_data(sc, ADC_IBUS, &result);
				if (!ret)
					sc->ibus_curr = result;
				val->intval = sc->ibus_curr * 1000;
				dev_dbg(sc->dev, "ADC_IBUS = %d\n", val->intval);
			}
		}
		break;
	case POWER_SUPPLY_PROP_TEMP:
		cmd = val->intval;
		if (sc8551_get_temperature(sc, &val->intval))
			dev_err(sc->dev, "fail to get temperature, cmd = %d\n", cmd);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		ret = sc8551_get_adc_data(sc, ADC_VBUS, &result);
		if (!ret)
			sc->vbus_volt = result;

		val->intval = sc->vbus_volt * 1000;
		dev_dbg(sc->dev, "ADC_VBUS = %d\n", val->intval);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		if (val->intval == CM_SOFT_ALARM_HEALTH_CMD) {
			ret = sc8551_get_alarm_status(sc);
			if (ret) {
				dev_err(sc->dev, "fail to get alarm status, ret=%d\n", ret);
				val->intval = 0;
				break;
			}

			val->intval = ((sc->bat_ovp_alarm << CM_CHARGER_BAT_OVP_ALARM_SHIFT)
				| (sc->bat_ocp_alarm << CM_CHARGER_BAT_OCP_ALARM_SHIFT)
				| (sc->bat_ucp_alarm << CM_CHARGER_BAT_UCP_ALARM_SHIFT)
				| (sc->bus_ovp_alarm << CM_CHARGER_BUS_OVP_ALARM_SHIFT)
				| (sc->bus_ocp_alarm << CM_CHARGER_BUS_OCP_ALARM_SHIFT));

			sc8551_clear_alarm_status(sc);
			break;
		}

		if (val->intval == CM_BUS_ERR_HEALTH_CMD) {
			sc8551_check_vbus_error_status(sc);
			val->intval = (sc->bus_err_lo  << CM_CHARGER_BUS_ERR_LO_SHIFT);
			val->intval |= (sc->bus_err_hi  << CM_CHARGER_BUS_ERR_HI_SHIFT);
			break;
		}

		sc8551_check_fault_status(sc);
		val->intval = ((sc->bat_ovp_fault << CM_CHARGER_BAT_OVP_FAULT_SHIFT)
			| (sc->bat_ocp_fault << CM_CHARGER_BAT_OCP_FAULT_SHIFT)
			| (sc->bus_ovp_fault << CM_CHARGER_BUS_OVP_FAULT_SHIFT)
			| (sc->bus_ocp_fault << CM_CHARGER_BUS_OCP_FAULT_SHIFT));

		sc8551_check_alarm_status(sc);
		sc8551_dump_reg(sc);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		if (!sc8551_check_charge_enabled(sc, &sc->charge_enabled)) {
			if (!sc->charge_enabled)
				val->intval = 0;
			else
				val->intval = sc->cfg->bus_ocp_alm_th * 1000;
		}
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		if (!sc8551_check_charge_enabled(sc, &sc->charge_enabled)) {
			if (!sc->charge_enabled)
				val->intval = 0;
			else
				val->intval = sc->cfg->bat_ocp_alm_th * 1000;
		}
		break;
	default:
		return -EINVAL;

	}

	return 0;
}

static int sc8551_charger_set_property(struct power_supply *psy,
				       enum power_supply_property prop,
				       const union power_supply_propval *val)
{
	struct sc8551 *sc = power_supply_get_drvdata(psy);
	int ret;

	if (!sc) {
		pr_err("%s[%d], NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}

	dev_dbg(sc->dev, "<<<<<prop = %d\n", prop);

	switch (prop) {
	case POWER_SUPPLY_PROP_CALIBRATE:
		if (!val->intval) {
			sc8551_enable_adc(sc, false);
			cancel_delayed_work_sync(&sc->wdt_work);
		}

		ret = sc8551_enable_charge(sc, val->intval);
		if (ret)
			dev_err(sc->dev, "%s, failed to %s charge\n",
				__func__, val->intval ? "enable" : "disable");

		if (sc8551_check_charge_enabled(sc, &sc->charge_enabled))
			dev_err(sc->dev, "%s, failed to check charge enabled\n", __func__);

		dev_info(sc->dev, "%s, %s charge %s\n", __func__,
			 val->intval ? "enable" : "disable", !ret ? "successfully" : "failed");
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		if (val->intval == CM_USB_PRESENT_CMD)
			sc8551_set_present(sc, true);
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		ret = sc8551_set_batovp_th(sc, val->intval / 1000);

		sc->cfg->bat_ovp_alm_th = val->intval / 1000 - sc->cfg->bat_delta_volt;
		dev_info(sc->dev, "set bat ovp th %d mv %s, soft set bat ovp alm th %d mv\n",
			 val->intval / 1000, !ret ? "successfully" : "failed",
			 sc->cfg->bat_ovp_alm_th);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int sc8551_charger_is_writeable(struct power_supply *psy,
				       enum power_supply_property prop)
{
	int ret;

	switch (prop) {
	case POWER_SUPPLY_PROP_CALIBRATE:
	case POWER_SUPPLY_PROP_PRESENT:
		ret = 1;
		break;
	default:
		ret = 0;
		break;
	}
	return ret;
}

static int sc8551_psy_register(struct sc8551 *sc)
{
	sc->psy_cfg.drv_data = sc;
	sc->psy_cfg.of_node = sc->dev->of_node;

	if (sc->mode == SC8551_ROLE_MASTER)
		sc->psy_desc.name = "sc8551-master";
	else if (sc->mode == SC8551_ROLE_SLAVE)
		sc->psy_desc.name = "sc8551-slave";
	else
		sc->psy_desc.name = "sc8551-standalone";

	sc->psy_desc.type = POWER_SUPPLY_TYPE_UNKNOWN;
	sc->psy_desc.properties = sc8551_charger_props;
	sc->psy_desc.num_properties = ARRAY_SIZE(sc8551_charger_props);
	sc->psy_desc.get_property = sc8551_charger_get_property;
	sc->psy_desc.set_property = sc8551_charger_set_property;
	sc->psy_desc.property_is_writeable = sc8551_charger_is_writeable;

	sc->fc2_psy = devm_power_supply_register(sc->dev,
						 &sc->psy_desc, &sc->psy_cfg);
	if (IS_ERR(sc->fc2_psy)) {
		dev_err(sc->dev, "failed to register fc2_psy\n");
		return PTR_ERR(sc->fc2_psy);
	}

	dev_info(sc->dev, "%s power supply register successfully\n", sc->psy_desc.name);

	return 0;
}

static irqreturn_t sc8551_charger_interrupt(int irq, void *dev_id);

static int sc8551_init_irq(struct sc8551 *sc)
{
	int ret;
	const char *dev_name;

	gpio_free(sc->irq_gpio);

	dev_err(sc->dev, ">>>>>>>>>>>>%d\n", sc->irq_gpio);
	ret = gpio_request(sc->irq_gpio, "sc8551");
	if (ret < 0) {
		dev_err(sc->dev, "fail to request GPIO(%d)   %d\n", sc->irq_gpio, ret);
		return ret;
	}

	ret = gpio_direction_input(sc->irq_gpio);
	if (ret < 0) {
		dev_err(sc->dev, "fail to set GPIO%d as input pin(%d)\n", sc->irq_gpio, ret);
		return ret;
	}

	sc->irq = gpio_to_irq(sc->irq_gpio);
	if (sc->irq <= 0) {
		dev_err(sc->dev, "irq mapping fail\n");
		return 0;
	}

	dev_info(sc->dev, "irq : %d\n",  sc->irq);

	if (sc->mode == SC8551_ROLE_MASTER)
		dev_name = "sc8551 master irq";
	else if (sc->mode == SC8551_ROLE_SLAVE)
		dev_name = "sc8551 slave irq";
	else
		dev_name = "sc8551 standalone irq";

	ret = devm_request_threaded_irq(sc->dev, sc->irq,
					NULL, sc8551_charger_interrupt,
					IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					dev_name, sc);
	if (ret < 0)
		dev_err(sc->dev, "request irq for irq=%d failed, ret =%d\n", sc->irq, ret);

	enable_irq_wake(sc->irq);

	device_init_wakeup(sc->dev, 1);

	return 0;
}

static void sc8551_dump_reg(struct sc8551 *sc)
{

	int ret;
	u8 val;
	u8 addr;

	for (addr = SC8551_REG_00; addr <= SC8551_REG_31; addr++) {
		ret = sc8551_read_byte(sc, addr, &val);
		if (!ret)
			dev_err(sc->dev, "Reg[%02X] = 0x%02X\n", addr, val);
	}
}

static void sc8551_check_alarm_status(struct sc8551 *sc)
{
	int ret;
	u8 flag = 0;
	u8 stat = 0;

	mutex_lock(&sc->data_lock);

	ret = sc8551_read_byte(sc, SC8551_REG_08, &flag);
	if (!ret && (flag & SC8551_IBUS_UCP_FALL_FLAG_MASK))
		dev_dbg(sc->dev, "UCP_FLAG =0x%02X\n",
			!!(flag & SC8551_IBUS_UCP_FALL_FLAG_MASK));

	ret = sc8551_read_byte(sc, SC8551_REG_2D, &flag);
	if (!ret && (flag & SC8551_VDROP_OVP_FLAG_MASK))
		dev_dbg(sc->dev, "VDROP_OVP_FLAG =0x%02X\n",
			!!(flag & SC8551_VDROP_OVP_FLAG_MASK));

	/*read to clear alarm flag*/
	ret = sc8551_read_byte(sc, SC8551_REG_0E, &flag);
	if (!ret && flag)
		dev_dbg(sc->dev, "INT_FLAG =0x%02X\n", flag);

	ret = sc8551_read_byte(sc, SC8551_REG_0D, &stat);
	if (!ret && stat != sc->prev_alarm) {
		dev_dbg(sc->dev, "INT_STAT = 0X%02x\n", stat);
		sc->prev_alarm = stat;
		sc->batt_present  = !!(stat & VBAT_INSERT);
		sc->vbus_present  = !!(stat & VBUS_INSERT);
	}

	ret = sc8551_read_byte(sc, SC8551_REG_08, &stat);
	if (!ret && (stat & 0x50))
		dev_err(sc->dev, "Reg[05]BUS_UCPOVP = 0x%02X\n", stat);

	ret = sc8551_read_byte(sc, SC8551_REG_0A, &stat);
	if (!ret && (stat & 0x02))
		dev_err(sc->dev, "Reg[0A]CONV_OCP = 0x%02X\n", stat);

	mutex_unlock(&sc->data_lock);
}

static void sc8551_check_fault_status(struct sc8551 *sc)
{
	int ret;
	u8 flag = 0;
	u8 stat = 0;
	bool changed = false;

	mutex_lock(&sc->data_lock);

	ret = sc8551_read_byte(sc, SC8551_REG_10, &stat);
	if (!ret && stat)
		dev_err(sc->dev, "FAULT_STAT = 0x%02X\n", stat);

	ret = sc8551_read_byte(sc, SC8551_REG_11, &flag);
	if (!ret && flag)
		dev_err(sc->dev, "FAULT_FLAG = 0x%02X\n", flag);

	if (!ret && flag != sc->prev_fault) {
		changed = true;
		sc->prev_fault = flag;
		sc->bat_ovp_fault = !!(flag & BAT_OVP_FAULT);
		sc->bat_ocp_fault = !!(flag & BAT_OCP_FAULT);
		sc->bus_ovp_fault = !!(flag & BUS_OVP_FAULT);
		sc->bus_ocp_fault = !!(flag & BUS_OCP_FAULT);
	}

	mutex_unlock(&sc->data_lock);
}

/*
 * interrupt does nothing, just info event chagne, other module could get info
 * through power supply interface
 */
static irqreturn_t sc8551_charger_interrupt(int irq, void *dev_id)
{
	struct sc8551 *sc = dev_id;

	dev_info(sc->dev, "INT OCCURRED\n");
	cm_notify_event(sc->fc2_psy, CM_EVENT_INT, NULL);

	return IRQ_HANDLED;
}

static void determine_initial_status(struct sc8551 *sc)
{
	if (sc->client->irq)
		sc8551_charger_interrupt(sc->client->irq, sc);
}

static const struct of_device_id sc8551_charger_match_table[] = {
	{
		.compatible = "sc,sc8551-standalone",
		.data = &sc8551_mode_data[SC8551_STDALONE],
	},
	{
		.compatible = "sc,sc8551-master",
		.data = &sc8551_mode_data[SC8551_MASTER],
	},

	{
		.compatible = "sc,sc8551-slave",
		.data = &sc8551_mode_data[SC8551_SLAVE],
	},
	{},
};

static int sc8551_charger_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct sc8551 *sc;
	const struct of_device_id *match;
	struct device_node *node = client->dev.of_node;
	struct power_supply *fuel_gauge;
	int ret;

	sc = devm_kzalloc(&client->dev, sizeof(struct sc8551), GFP_KERNEL);
	if (!sc)
		return -ENOMEM;

	sc->dev = &client->dev;

	sc->client = client;

	mutex_init(&sc->i2c_rw_lock);
	mutex_init(&sc->data_lock);
	mutex_init(&sc->charging_disable_lock);
	mutex_init(&sc->irq_complete);

	sc->resume_completed = true;
	sc->irq_waiting = false;
	sc->is_sc8551 = true;

	ret = sc8551_detect_device(sc);
	if (ret) {
		dev_err(sc->dev, "No sc8551 device found!\n");
		return -ENODEV;
	}

	i2c_set_clientdata(client, sc);
	ret = sc8551_create_device_node(&(client->dev));
	if (ret) {
		dev_err(sc->dev, "failed to create device node, ret = %d\n", ret);
		return ret;
	}

	match = of_match_node(sc8551_charger_match_table, node);
	if (match == NULL) {
		dev_err(sc->dev, "device tree match not found!\n");
		return -ENODEV;
	}

	sc8551_get_work_mode(sc, &sc->mode);
	if (sc->mode !=  *(int *)match->data) {
		dev_err(sc->dev, "device operation mode mismatch with dts configuration\n");
		return -EINVAL;
	}

	ret = sc8551_parse_dt(sc, &client->dev);
	if (ret)
		return -EIO;

	of_property_read_string(node, "cm-fuel-gauge", &sc->cfg->psy_fuel_gauge);
	if (sc->cfg->psy_fuel_gauge) {
		fuel_gauge = power_supply_get_by_name(sc->cfg->psy_fuel_gauge);
		if (!fuel_gauge) {
			dev_err(sc->dev, "Cannot find power supply \"%s\"\n",
				sc->cfg->psy_fuel_gauge);
			return -EPROBE_DEFER;
		}

		power_supply_put(fuel_gauge);
		dev_info(sc->dev, "Get battery information from FGU\n");
	}

	ret = sc8551_init_device(sc);
	if (ret) {
		dev_err(sc->dev, "Failed to init device\n");
		return ret;
	}

	INIT_DELAYED_WORK(&sc->wdt_work, sc8551_charger_watchdog_work);
	ret = sc8551_psy_register(sc);
	if (ret)
		return ret;

	ret = sc8551_init_irq(sc);
	if (ret)
		return ret;

	determine_initial_status(sc);

	dev_info(sc->dev, "sc8551 probe successfully, Part Num:%d\n!",
		 sc->part_no);

	return 0;
}

static inline bool is_device_suspended(struct sc8551 *sc)
{
	return !sc->resume_completed;
}

static int sc8551_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sc8551 *sc = i2c_get_clientdata(client);

	mutex_lock(&sc->irq_complete);
	sc->resume_completed = false;
	mutex_unlock(&sc->irq_complete);
	dev_info(dev, "Suspend successfully!");

	return 0;
}

static int sc8551_suspend_noirq(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sc8551 *sc = i2c_get_clientdata(client);

	if (sc->irq_waiting) {
		pr_err_ratelimited("Aborting suspend, an interrupt was detected while suspending\n");
		return -EBUSY;
	}
	return 0;
}

static int sc8551_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sc8551 *sc = i2c_get_clientdata(client);

	mutex_lock(&sc->irq_complete);
	sc->resume_completed = true;
	if (sc->irq_waiting) {
		sc->irq_disabled = false;
		enable_irq(client->irq);
		mutex_unlock(&sc->irq_complete);
		sc8551_charger_interrupt(client->irq, sc);
	} else {
		mutex_unlock(&sc->irq_complete);
	}

	power_supply_changed(sc->fc2_psy);
	dev_err(dev, "Resume successfully!");

	return 0;
}
static int sc8551_charger_remove(struct i2c_client *client)
{
	struct sc8551 *sc = i2c_get_clientdata(client);

	sc8551_enable_adc(sc, false);
	cancel_delayed_work_sync(&sc->wdt_work);

	power_supply_unregister(sc->fc2_psy);

	mutex_destroy(&sc->charging_disable_lock);
	mutex_destroy(&sc->data_lock);
	mutex_destroy(&sc->i2c_rw_lock);
	mutex_destroy(&sc->irq_complete);

	return 0;
}

static void sc8551_charger_shutdown(struct i2c_client *client)
{
	struct sc8551 *sc = i2c_get_clientdata(client);

	sc8551_enable_adc(sc, false);
	cancel_delayed_work_sync(&sc->wdt_work);
}

static const struct dev_pm_ops sc8551_pm_ops = {
	.resume		= sc8551_resume,
	.suspend_noirq	= sc8551_suspend_noirq,
	.suspend	= sc8551_suspend,
};

static const struct i2c_device_id sc8551_charger_id[] = {
	{"sc8551-standalone", SC8551_ROLE_STDALONE},
	{"sc8551-master", SC8551_ROLE_MASTER},
	{"sc8551-slave", SC8551_ROLE_SLAVE},
	{},
};

static struct i2c_driver sc8551_charger_driver = {
	.driver		= {
		.name	= "sc8551-charger",
		.owner	= THIS_MODULE,
		.of_match_table = sc8551_charger_match_table,
		.pm	= &sc8551_pm_ops,
	},
	.id_table	= sc8551_charger_id,

	.probe		= sc8551_charger_probe,
	.remove		= sc8551_charger_remove,
	.shutdown	= sc8551_charger_shutdown,
};

module_i2c_driver(sc8551_charger_driver);

MODULE_DESCRIPTION("SC SC8551 Charge Pump Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Aiden-yu@southchip.com");

