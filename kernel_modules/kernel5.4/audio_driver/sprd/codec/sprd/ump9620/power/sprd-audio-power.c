// SPDX-License-Identifier: GPL-2.0-only
/*
 * sound/soc/sprd/codec/sprd/sc2730/sprd-audio-power.c
 *
 * SPRD-AUDIO-POWER -- SpreadTrum intergrated audio power supply.
 *
 * Copyright (C) 2016 SpreadTrum Ltd.
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
#include "sprd-asoc-debug.h"
#define pr_fmt(fmt) pr_sprd_fmt("POWER")""fmt

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/slab.h>

#include "sprd-audio.h"
#include "sprd-audio-power.h"
#include "sprd-asoc-common.h"

#ifdef CONFIG_AUDIO_POWER_ALLOW_UNSUPPORTED
#define UNSUP_MASK	0x0000
#else
#define UNSUP_MASK	0x8000
#endif

#define UNSUP(x)	(UNSUP_MASK | (x))
#define IS_UNSUP(x)	(UNSUP_MASK & (x))
#define LDO_MV(x)	(~UNSUP_MASK & (x))

#define SPRD_AUDIO_POWER_LDO_LOW(label, r_supply, d_id, \
e_reg, e_bit, p_enable, p_disable, s_ctrl, \
reg, mask, shift, table, table_size, d_on_delay, d_off_delay) \
static struct sprd_audio_power_info SPRD_AUDIO_POWER_INFO_##label = { \
	.id = d_id, \
	.en_reg = e_reg, \
	.en_bit = e_bit, \
	.power_enable = p_enable, \
	.power_disable = p_disable, \
	.sleep_ctrl = s_ctrl, \
	.v_reg = reg, \
	.v_mask = mask, \
	.v_shift = shift, \
	.v_table_len = table_size, \
	.v_table = table, \
	.on_delay = d_on_delay, \
	.off_delay = d_off_delay, \
	.desc = { \
		.name = #label, \
		.id = SPRD_AUDIO_POWER_##label, \
		.n_voltages = table_size, \
		.ops = &sprd_audio_power_ops, \
		.type = REGULATOR_VOLTAGE, \
		.owner = THIS_MODULE, \
	}, \
}

#define SPRD_AUDIO_POWER_LDO(label, r_supply, id, en_reg, en_bit, sleep_ctrl, \
		v_reg, v_mask, v_shift, v_table, on_delay, off_delay) \
SPRD_AUDIO_POWER_LDO_LOW(label, r_supply, id, en_reg, en_bit, 0, 0, \
		sleep_ctrl, v_reg, v_mask, v_shift, v_table, \
		ARRAY_SIZE(v_table), on_delay, off_delay)

#define SPRD_AUDIO_POWER_SIMPLE_LDO(label, r_supply, id, en_reg, en_bit, \
		sleep_ctrl, on_delay, off_delay) \
SPRD_AUDIO_POWER_LDO_LOW(label, r_supply, id, en_reg, en_bit, 0, 0, \
		sleep_ctrl, 0, 0, 0, 0, 0, on_delay, off_delay)

#define SPRD_AUDIO_POWER_REG_LDO(label, id, power_enable, power_disable) \
SPRD_AUDIO_POWER_LDO_LOW(label, 0, id, 0, 0, power_enable, power_disable, \
		0, 0, 0, 0, 0, 0, 0, 0)

static inline int sprd_power_read(struct sprd_audio_power_info *info, int reg)
{
	return arch_audio_power_read(reg);
}

static inline int
sprd_power_write(struct sprd_audio_power_info *info, int reg, int val)
{
	int ret = 0;

	ret = arch_audio_power_write(reg, val);

	sp_asoc_pr_reg("[0x%04x] W:[0x%08x] R:[0x%08x]\n", (reg - SPRD_CODEC_AP_BASE) & 0xFFFF, val,
		       arch_audio_power_read(reg));

	return ret;
}

static inline int
sprd_power_u_bits(struct sprd_audio_power_info *info,
		  unsigned int reg, unsigned int mask, unsigned int value)
{
	int ret = 0;

	ret = arch_audio_power_write_mask(reg, value, mask);
	sp_asoc_pr_reg("[0x%04x] U:[0x%08x] R:[0x%08x] MASK:0x%x NAME:%s\n",
		(reg - SPRD_CODEC_AP_BASE) & 0xFFFF, value, arch_audio_power_read(reg),
		mask, info->desc.name);

	return ret;
}

static int sprd_audio_power_is_enabled(struct regulator_dev *rdev)
{
	int is_enable;
	struct sprd_audio_power_info *info = rdev_get_drvdata(rdev);

	sp_asoc_pr_dbg("%s Is Enabled\n", rdev->desc->name);

	if (info->en_reg)
		is_enable = sprd_power_read(info, info->en_reg) & info->en_bit;
	else
		is_enable = info->en_bit;

	return !!is_enable;
}

static int audio_power_set_voltage(struct regulator_dev *rdev, int min_uV,
				   int max_uV)
{
	struct sprd_audio_power_info *info = rdev_get_drvdata(rdev);
	int vsel;

	if (!info->v_table) {
		pr_err("%s info->v_table is NULL!\n", __func__);
		return -EINVAL;
	}

	for (vsel = 0; vsel < info->v_table_len; vsel++) {
		int mV = info->v_table[vsel];
		int uV;

		if (IS_UNSUP(mV))
			continue;
		uV = LDO_MV(mV) * 1000;

		/* use the first in-range value */
		if (min_uV <= uV && uV <= max_uV) {
			sp_asoc_pr_dbg("%s: %dus\n", __func__, uV);
			sprd_power_u_bits(info,
					  info->v_reg,
					  info->v_mask << info->v_shift,
					  vsel << info->v_shift);

			return vsel;
		}
	}

	return -EDOM;
}

static int sprd_audio_power_enable(struct regulator_dev *rdev)
{
	struct sprd_audio_power_info *info = rdev_get_drvdata(rdev);
	int ret = 0;

	sp_asoc_pr_dbg("%s Enable\n", rdev->desc->name);

	if (info->en_reg) {
		ret = sprd_power_u_bits(info,
					info->en_reg, info->en_bit,
					info->en_bit);
	} else {
		if (info->power_enable)
			ret = info->power_enable(info);
		info->en_bit = 1;
	}

	udelay(info->on_delay);

	if (info->v_table) {
		if (info->min_uV || info->max_uV)
			audio_power_set_voltage(rdev, info->min_uV,
						info->max_uV);
	}

	return ret;
}

static int sprd_audio_power_disable(struct regulator_dev *rdev)
{
	struct sprd_audio_power_info *info = rdev_get_drvdata(rdev);
	int ret = 0;

	sp_asoc_pr_dbg("%s Disable\n", rdev->desc->name);

	if (info->en_reg) {
		ret = sprd_power_u_bits(info, info->en_reg, info->en_bit, 0);
	} else {
		if (info->power_disable)
			ret = info->power_disable(info);
		info->en_bit = 0;
	}

	udelay(info->off_delay);

	return ret;
}

static int sprd_audio_power_get_status(struct regulator_dev *rdev)
{
	sp_asoc_pr_dbg("%s Get Status\n", rdev->desc->name);

	if (!sprd_audio_power_is_enabled(rdev))
		return REGULATOR_STATUS_OFF;

	return REGULATOR_STATUS_NORMAL;
}

static int sprd_audio_power_set_mode(struct regulator_dev *rdev,
	unsigned int mode)
{
	int ret = 0;
	struct sprd_audio_power_info *info = rdev_get_drvdata(rdev);

	sp_asoc_pr_dbg("%s Set Mode 0x%x\n", rdev->desc->name, mode);

	if (!info->sleep_ctrl)
		return ret;

	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		ret = info->sleep_ctrl(info, 0);
		break;
	case REGULATOR_MODE_STANDBY:
		ret = info->sleep_ctrl(info, 1);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static const u16 BG_VSEL_table[] = {
	1550, 1500
};

static const u16 VB_VSEL_table[] = {
	3000, 3025, 3050, 3075,
	3100, 3125, 3150, 3175,
	3200, 3225, 3250, 3275,
	3300, 3325, 3350, 3375,
	3400, 3425, 3450, 3475,
	3500, 3525, 3550, 3575,
};

static const u16 VMIC_VSEL_table[] = {
	2200, 2400, 2500, 2600,
	2700, 2800, 2900, 3000,
};

static const u16 HIB_VSEL_table[] = {
	1000, 950, 900, 850, 800, 750,
	700, 650, 600, 550, 500, 450, 400,
};

static int sprd_audio_power_list_voltage(struct regulator_dev *rdev,
					 unsigned int index)
{
	struct sprd_audio_power_info *info = rdev_get_drvdata(rdev);
	int mV;

	sp_asoc_pr_dbg("%s List Voltage\n", rdev->desc->name);

	if (!info->v_table)
		return 0;

	if (index >= info->v_table_len)
		return 0;

	mV = info->v_table[index];

	return IS_UNSUP(mV) ? 0 : (LDO_MV(mV) * 1000);
}

static int
sprd_audio_power_set_voltage(struct regulator_dev *rdev, int min_uV, int max_uV,
			     unsigned int *selector)
{
	struct sprd_audio_power_info *info = rdev_get_drvdata(rdev);
	int vsel;

	sp_asoc_pr_info("%s Set Voltage %d--%d\n", rdev->desc->name, min_uV,
			max_uV);

	if (!info->v_table)
		return 0;
	info->min_uV = min_uV;
	info->max_uV = max_uV;

	vsel = audio_power_set_voltage(rdev, min_uV, max_uV);
	if (vsel != -EDOM)
		*selector = vsel;

	return vsel;
}

static int sprd_audio_power_get_voltage(struct regulator_dev *rdev)
{
	struct sprd_audio_power_info *info = rdev_get_drvdata(rdev);
	int vsel;

	sp_asoc_pr_dbg("%s Get Voltage\n", rdev->desc->name);

	if (!info->v_table)
		return 0;

	vsel = (sprd_power_read(info, info->v_reg) & info->v_mask)
	    >> info->v_shift;

	return LDO_MV(info->v_table[vsel]) * 1000;
}

static struct regulator_ops sprd_audio_power_ops = {
	.list_voltage = sprd_audio_power_list_voltage,

	.set_voltage = sprd_audio_power_set_voltage,
	.get_voltage = sprd_audio_power_get_voltage,

	.enable = sprd_audio_power_enable,
	.disable = sprd_audio_power_disable,
	.is_enabled = sprd_audio_power_is_enabled,

	.set_mode = sprd_audio_power_set_mode,

	.get_status = sprd_audio_power_get_status,
};

static int power_sleep(void *data, int en)
{
	struct sprd_audio_power_info *info = data;
	int reg, mask, value;
	int ret = -EINVAL;

	switch (info->desc.id) {
	case SPRD_AUDIO_POWER_VB:
		reg = ANA_PMU0;
		mask = VB_SLEEP_EN;
		value = en ? mask : 0;
		ret = sprd_power_u_bits(info, reg, mask, value);
		break;
	case SPRD_AUDIO_POWER_MICBIAS1:
		reg = ANA_PMU1;
		mask = MIC1_BIAS_SLEEP_EN;
		value = en ? mask : 0;
		ret = sprd_power_u_bits(info, reg, mask, value);
		break;
	case SPRD_AUDIO_POWER_MICBIAS2:
		reg = ANA_PMU1;
		mask = MIC2_BIAS_SLEEP_EN;
		value = en ? mask : 0;
		ret = sprd_power_u_bits(info, reg, mask, value);
		break;
	case SPRD_AUDIO_POWER_MICBIAS3:
		reg = ANA_PMU1;
		mask = MIC3_BIAS_SLEEP_EN;
		value = en ? mask : 0;
		ret = sprd_power_u_bits(info, reg, mask, value);
		break;
	case SPRD_AUDIO_POWER_HEADMICBIAS:
		reg = ANA_PMU1;
		mask = HMIC_BIAS_SLEEP_EN | HMIC_BIAS_VREF_SEL;
		value = en ? HMIC_BIAS_SLEEP_EN : HMIC_BIAS_VREF_SEL;
		ret = sprd_power_u_bits(info, reg, mask, value);
		break;
	default:
		WARN_ON(1);
	}

	sp_asoc_pr_dbg("audio power %d sleep ret:%d\n",
		info->desc.id, ret);
	return ret;
}

static int power_enable(struct sprd_audio_power_info *info)
{
	int reg, value;
	int ret = -EINVAL;

	switch (info->desc.id) {
	case SPRD_AUDIO_POWER_MICBIAS1:
		reg = ANA_PMU0;
		value = BIAS_EN;
		ret = sprd_power_u_bits(info, reg, value, value);

		if (ret < 0) {
			sp_asoc_pr_info("audio power %d enable errer:%d,\n",
				info->desc.id, ret);
			return ret;
		}
		reg = ANA_PMU1;
		value = MIC1_BIAS_EN;
		ret = sprd_power_u_bits(info, reg, value, value);
		break;
	case SPRD_AUDIO_POWER_MICBIAS2:
		reg = ANA_PMU0;
		value = BIAS_EN;
		ret = sprd_power_u_bits(info, reg, value, value);

		if (ret < 0) {
			sp_asoc_pr_info("audio power %d enable errer:%d,\n",
				info->desc.id, ret);
			return ret;
		}
		reg = ANA_PMU1;
		value = MIC2_BIAS_EN;
		ret = sprd_power_u_bits(info, reg, value, value);
		break;
	case SPRD_AUDIO_POWER_MICBIAS3:
		reg = ANA_PMU0;
		value = BIAS_EN;
		ret = sprd_power_u_bits(info, reg, value, value);

		if (ret < 0) {
			sp_asoc_pr_info("audio power %d enable errer:%d,\n",
				info->desc.id, ret);
			return ret;
		}
		reg = ANA_PMU1;
		value = MIC3_BIAS_EN;
		ret = sprd_power_u_bits(info, reg, value, value);
		break;
	case SPRD_AUDIO_POWER_HEADMICBIAS:
		reg = ANA_PMU1;
		value = HMIC_BIAS_VREF_SEL | HMIC_BIAS_SOFT_EN | HMIC_BIAS_EN;
		ret = sprd_power_u_bits(info, reg, value, value);
		if (ret < 0) {
			sp_asoc_pr_info("audio power %d enable errer:%d,\n",
				info->desc.id, ret);
			return ret;
		}
		ret = sprd_power_u_bits(info, ANA_PMU5, HMIC_BIAS_V(0xf),
					HMIC_BIAS_V(0x9));
		break;
	default:
		WARN_ON(1);
	}

	sp_asoc_pr_dbg("audio power %d enable ret:%d\n",
		info->desc.id, ret);
	return ret;
}

static int power_disable(struct sprd_audio_power_info *info)
{
	int reg, mask;
	int ret = -EINVAL;

	switch (info->desc.id) {
	case SPRD_AUDIO_POWER_MICBIAS1:
		reg = ANA_PMU0;
		mask = BIAS_EN;
		ret = sprd_power_u_bits(info, reg, mask, 0);

		if (ret < 0) {
			sp_asoc_pr_info("audio power %d disable errer:%d,\n",
				info->desc.id, ret);
			return ret;
		}
		reg = ANA_PMU1;
		mask = MIC1_BIAS_EN;
		ret = sprd_power_u_bits(info, reg, mask, 0);
		break;
	case SPRD_AUDIO_POWER_MICBIAS2:
		reg = ANA_PMU0;
		mask = BIAS_EN;
		ret = sprd_power_u_bits(info, reg, mask, 0);

		if (ret < 0) {
			sp_asoc_pr_info("audio power %d disable errer:%d,\n",
				info->desc.id, ret);
			return ret;
		}
		reg = ANA_PMU1;
		mask = MIC2_BIAS_EN;
		ret = sprd_power_u_bits(info, reg, mask, 0);
		break;
	case SPRD_AUDIO_POWER_MICBIAS3:
		reg = ANA_PMU0;
		mask = BIAS_EN;
		ret = sprd_power_u_bits(info, reg, mask, 0);

		if (ret < 0) {
			sp_asoc_pr_info("audio power %d disable errer:%d,\n",
				info->desc.id, ret);
			return ret;
		}
		reg = ANA_PMU1;
		mask = MIC3_BIAS_EN;
		ret = sprd_power_u_bits(info, reg, mask, 0);
		break;
	case SPRD_AUDIO_POWER_HEADMICBIAS:
		reg = ANA_PMU1;
		mask = HMIC_BIAS_VREF_SEL | HMIC_BIAS_SOFT_EN | HMIC_BIAS_EN;
		ret = sprd_power_u_bits(info, reg, mask, 0);
		break;
	default:
		WARN_ON(1);
	}

	sp_asoc_pr_dbg("audio power %d disable ret:%d\n",
		info->desc.id, ret);
	return ret;
}


static inline int vreg_enable(struct sprd_audio_power_info *info)
{
	int ret = 0;
	static int i;

	sp_asoc_pr_info("audio power  %d  enable, %s\n",
		info->desc.id, __func__);
	arch_audio_codec_analog_reg_enable();
	if (i == 0) {
		arch_audio_codec_analog_reset();
		i = 1;
	}

	return ret;
}

static inline int vreg_disable(struct sprd_audio_power_info *info)
{
	ADEBUG();
	arch_audio_codec_analog_reg_disable();

	return 0;
}

/*
 * We list regulators here if systems need some level of
 * software control over them after boot.
 */
SPRD_AUDIO_POWER_REG_LDO(VREG, 1, vreg_enable, vreg_disable);
SPRD_AUDIO_POWER_SIMPLE_LDO(VB, "VREG", 2, ANA_PMU0,
			VB_EN, power_sleep, 0, 0);
SPRD_AUDIO_POWER_SIMPLE_LDO(BG, "VB", 3, ANA_PMU0,
			BG_EN, NULL, 0, 0);
SPRD_AUDIO_POWER_SIMPLE_LDO(BIAS, "VB", 4, ANA_PMU0,
			BIAS_EN, NULL, 0, 0);
SPRD_AUDIO_POWER_LDO_LOW(MICBIAS1, "VB", 5, ANA_PMU1,
			MIC1_BIAS_EN, NULL, NULL, power_sleep,
			 0, 0, 0, 0, 0, 0, 0);
SPRD_AUDIO_POWER_LDO_LOW(MICBIAS2, "VB", 6, ANA_PMU1,
			MIC2_BIAS_EN, NULL, NULL, power_sleep,
			 0, 0, 0, 0, 0, 0, 0);
SPRD_AUDIO_POWER_LDO_LOW(MICBIAS3, "VB", 7, ANA_PMU1,
			MIC3_BIAS_EN, NULL, NULL, power_sleep,
			 0, 0, 0, 0, 0, 0, 0);
SPRD_AUDIO_POWER_LDO_LOW(HEADMICBIAS, "BG", 8, 0, 0,
			 power_enable, power_disable, power_sleep,
			 0, 0, 0, 0, 0, 0, 0);
SPRD_AUDIO_POWER_SIMPLE_LDO(DCL, "VB", 9, ANA_DCL1,
			DCL_EN, NULL, 0, 0);
SPRD_AUDIO_POWER_SIMPLE_LDO(DIG_CLK_INTC, "DCL", 10, ANA_DCL1,
			DIG_CLK_INTC_EN, NULL, 0, 0);
SPRD_AUDIO_POWER_SIMPLE_LDO(DIG_CLK_HID, "DCL", 11, ANA_DCL1,
			DIG_CLK_HID_EN, NULL, 0, 0);
SPRD_AUDIO_POWER_SIMPLE_LDO(CLK_DCL_32K, "DCL", 12, ANA_CLK0,
			CLK_DCL_32K_EN, NULL, 0, 0);
SPRD_AUDIO_POWER_SIMPLE_LDO(DAHPL_CHN, "VB", 13, ANA_DAC1,
			DAHPL_EN, NULL, 0, 0);

#define SPRD_OF_MATCH(comp, label) \
	{ \
		.compatible = comp, \
		.data = &SPRD_AUDIO_POWER_INFO_##label, \
	}

static const struct of_device_id sprd_audio_power_of_match[] = {
	SPRD_OF_MATCH("sp,audio-vreg", VREG),
	SPRD_OF_MATCH("sp,audio-vb", VB),
	SPRD_OF_MATCH("sp,audio-bg", BG),
	SPRD_OF_MATCH("sp,audio-bias", BIAS),
	SPRD_OF_MATCH("sp,audio-micbias1", MICBIAS1),
	SPRD_OF_MATCH("sp,audio-micbias2", MICBIAS2),
	SPRD_OF_MATCH("sp,audio-micbias3", MICBIAS3),
	SPRD_OF_MATCH("sp,audio-headmicbias", HEADMICBIAS),
	SPRD_OF_MATCH("sp,audio-dcl", DCL),
	SPRD_OF_MATCH("sp,audio-dig-clk-intc", DIG_CLK_INTC),
	SPRD_OF_MATCH("sp,audio-dig-clk-hid", DIG_CLK_HID),
	SPRD_OF_MATCH("sp,audio-clk-dcl-32k", CLK_DCL_32K),
	SPRD_OF_MATCH("sp,audio-dahpl-chn", DAHPL_CHN),
};

static struct device_node *sprd_audio_power_get_ana_node(void)
{
	int i;
	struct device_node *np;
	const char * const comp[] = {
		"unisoc,ump9620-audio-codec",
	};

	/* Check if sc272x codec is available. */
	for (i = 0; i < ARRAY_SIZE(comp); i++) {
		np = of_find_compatible_node(
			NULL, NULL, comp[i]);
		if (np)
			return np;
	}

	return NULL;
}

static int sprd_audio_power_config_reg_vars(void)
{
	struct device_node *node;
	struct regmap *adi_rgmp;
	struct platform_device *pdev;
	u32 val;
	int ret = 0;

	if (arch_audio_codec_get_regmap() != NULL) {
		pr_info("%s regmap exist return 0\n", __func__);
		return 0;
	}

	node = sprd_audio_power_get_ana_node();
	if (!node) {
		pr_err("ERR: [%s] there must be a analog node!\n", __func__);
		return -ENODEV;
	}
	pdev = of_find_device_by_node(node);
	if (unlikely(!pdev)) {
		pr_err("ERR: [%s] this node has no pdev?!\n", __func__);
		ret = -EPROBE_DEFER;
		goto put_node;
	}
	adi_rgmp = dev_get_regmap(pdev->dev.parent, NULL);
	if (!adi_rgmp) {
		pr_err("ERR: [%s] spi device is not ready yet!\n", __func__);
		ret = -EPROBE_DEFER;
		goto put_node;
	}
	if (of_property_read_u32(node, "reg", &val)) {
		dev_err(&pdev->dev, "%s :no property of reg\n", __func__);
		ret = -ENXIO;
		goto put_node;
	}
	arch_audio_codec_set_regmap(adi_rgmp);
	arch_audio_codec_set_reg_offset((unsigned long)val);
put_node:
	of_node_put(node);

	return ret;
}

static int sprd_audio_power_probe(struct platform_device *pdev)
{
	int i, id, ret;
	struct sprd_audio_power_info *info;
	const struct sprd_audio_power_info *template;
	struct regulator_init_data *initdata;
	struct regulation_constraints *c;
	struct regulator_dev *rdev;
	struct regulator_config config = { };
	int min_index = 0;
	int max_index = 0;

	id = pdev->id;

	sp_asoc_pr_info("Probe %d\n", id);

	ret = sprd_audio_power_config_reg_vars();
	if (ret) {
		pr_err("%s: config register variables failed!\n", __func__);
		return ret;
	}

	initdata = pdev->dev.platform_data;
	for (i = 0, template = NULL; i < ARRAY_SIZE(sprd_audio_power_of_match);
	     i++) {
		template = sprd_audio_power_of_match[i].data;
		if (!template || template->desc.id != id)
			continue;
		break;
	}

	if (!template)
		return -ENODEV;

	if (!initdata)
		return -EINVAL;

	info = kmemdup(template, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	/* Constrain board-specific capabilities according to what
	 * this driver and the chip itself can actually do.
	 */
	c = &initdata->constraints;
	c->valid_modes_mask |= REGULATOR_MODE_NORMAL | REGULATOR_MODE_STANDBY;
	c->valid_ops_mask |= REGULATOR_CHANGE_VOLTAGE
	    | REGULATOR_CHANGE_MODE | REGULATOR_CHANGE_STATUS;

	if (info->v_table) {
		for (i = 0; i < info->v_table_len; i++) {
			if (info->v_table[i] < info->v_table[min_index])
				min_index = i;
			if (info->v_table[i] > info->v_table[max_index])
				max_index = i;
		}
		c->min_uV = LDO_MV((info->v_table)[min_index]) * 1000;
		c->max_uV = LDO_MV((info->v_table)[max_index]) * 1000;
		sp_asoc_pr_info("min_uV:%d, max_uV:%d\n", c->min_uV, c->max_uV);
	}
	info->min_uV = 0;
	info->max_uV = 0;

	config.dev = &pdev->dev;
	config.init_data = initdata;
	config.driver_data = info;
	config.of_node = pdev->dev.of_node;

	rdev = regulator_register(&info->desc, &config);
	if (IS_ERR(rdev)) {
		dev_err(&pdev->dev, "ERR:Can't Register %s, %ld\n",
			info->desc.name, PTR_ERR(rdev));
		kfree(info);
		return PTR_ERR(rdev);
	}
	sp_asoc_pr_info("Register %s Success!\n", info->desc.name);

	platform_set_drvdata(pdev, rdev);

	return 0;
}

static int sprd_audio_power_remove(struct platform_device *pdev)
{
	struct regulator_dev *rdev = platform_get_drvdata(pdev);
	struct twlreg_info *info = rdev->reg_data;

	regulator_unregister(platform_get_drvdata(pdev));
	kfree(info);

	return 0;
}

static struct platform_driver sprd_audio_power_driver = {
	.driver = {
		   .name = "sprd-audio-power",
		   .owner = THIS_MODULE,
		   },
	.probe = sprd_audio_power_probe,
	.remove = sprd_audio_power_remove,
};

static int __init sprd_audio_power_regulator_init(void)
{
	return platform_driver_register(&sprd_audio_power_driver);
}

subsys_initcall_sync(sprd_audio_power_regulator_init);

static void __exit sprd_audio_power_regulator_exit(void)
{
	platform_driver_unregister(&sprd_audio_power_driver);
}

module_exit(sprd_audio_power_regulator_exit);

MODULE_DESCRIPTION("SPRD audio power regulator driver");
MODULE_AUTHOR("Jian Chen <jian.chen@spreadtrum.com>");
MODULE_ALIAS("platform:sprd-audio-power");
MODULE_LICENSE("GPL");
