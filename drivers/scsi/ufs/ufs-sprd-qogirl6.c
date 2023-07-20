/* SPDX-License-Identifier: GPL-2.0
 * Copyright (C) 2020 Unisoc Communications Inc.
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

#include <asm/unaligned.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/time.h>
#include <linux/sprd_soc_id.h>
#include <dt-bindings/soc/sprd,qogirl6-regs.h>
#include <linux/rpmb.h>
#include <linux/reset.h>
#include <trace/hooks/ufshcd.h>

#include "ufshcd.h"
#include "ufs.h"
#include "ufshcd-pltfrm.h"
#include "ufshci.h"
#include "ufs-sprd-qogirl6.h"
#include "ufs_quirks.h"
#include "unipro.h"
#include "ufs-sprd-ioctl.h"

int syscon_get_args(struct device *dev, struct ufs_sprd_host *host)
{
	u32 args[2];
	struct device_node *np = dev->of_node;
	struct platform_device *pdev = to_platform_device(dev);

	host->aon_apb_ufs_en.regmap =
			syscon_regmap_lookup_by_phandle_args(np, "aon_apb_ufs_en", 2, args);
	if (IS_ERR(host->aon_apb_ufs_en.regmap)) {
		pr_warn("failed to get apb ufs aon_apb_ufs_en\n");
		return PTR_ERR(host->aon_apb_ufs_en.regmap);
	} else {
		host->aon_apb_ufs_en.reg = args[0];
		host->aon_apb_ufs_en.mask = args[1];
	}

	pr_info("fangkuiufs host->aon_apb_ufs_en.regmap = %p",
		host->aon_apb_ufs_en.regmap);

	host->ap_ahb_ufs_clk.regmap =
			syscon_regmap_lookup_by_phandle_args(np, "ap_ahb_ufs_clk", 2, args);
	if (IS_ERR(host->ap_ahb_ufs_clk.regmap)) {
		pr_err("failed to get apb ufs ap_ahb_ufs_clk\n");
		return PTR_ERR(host->ap_ahb_ufs_clk.regmap);
	} else {
		host->ap_ahb_ufs_clk.reg = args[0];
		host->ap_ahb_ufs_clk.mask = args[1];
	}

	host->ap_apb_ufs_en.regmap =
			syscon_regmap_lookup_by_phandle_args(np, "ap_apb_ufs_en", 2, args);
	if (IS_ERR(host->ap_apb_ufs_en.regmap)) {
		pr_err("failed to get apb ufs ap_apb_ufs_en\n");
		return PTR_ERR(host->ap_apb_ufs_en.regmap);
	} else {
		host->ap_apb_ufs_en.reg = args[0];
		host->ap_apb_ufs_en.mask = args[1];
	}

	host->ufs_refclk_on.regmap =
			syscon_regmap_lookup_by_phandle_args(np, "ufs_refclk_on", 2, args);
	if (IS_ERR(host->ufs_refclk_on.regmap)) {
		pr_warn("failed to get ufs_refclk_on\n");
		return PTR_ERR(host->ufs_refclk_on.regmap);
	} else {
		host->ufs_refclk_on.reg = args[0];
		host->ufs_refclk_on.mask = args[1];
	}

	host->ahb_ufs_lp.regmap =
			syscon_regmap_lookup_by_phandle_args(np, "ahb_ufs_lp", 2, args);
	if (IS_ERR(host->ahb_ufs_lp.regmap)) {
		pr_warn("failed to get ahb_ufs_lp\n");
		return PTR_ERR(host->ahb_ufs_lp.regmap);
	} else {
		host->ahb_ufs_lp.reg = args[0];
		host->ahb_ufs_lp.mask = args[1];
	}

	host->ahb_ufs_force_isol.regmap =
			syscon_regmap_lookup_by_phandle_args(np, "ahb_ufs_force_isol", 2, args);
	if (IS_ERR(host->ahb_ufs_force_isol.regmap)) {
		pr_err("failed to get ahb_ufs_force_isol 1\n");
		return PTR_ERR(host->ahb_ufs_force_isol.regmap);
	} else {
		host->ahb_ufs_force_isol.reg = args[0];
		host->ahb_ufs_force_isol.mask = args[1];
	}

	host->ahb_ufs_cb.regmap =
			syscon_regmap_lookup_by_phandle_args(np, "ahb_ufs_cb", 2, args);
	if (IS_ERR(host->ahb_ufs_cb.regmap)) {
		pr_err("failed to get ahb_ufs_cb\n");
		return PTR_ERR(host->ahb_ufs_cb.regmap);
	} else {
		host->ahb_ufs_cb.reg = args[0];
		host->ahb_ufs_cb.mask = args[1];
	}
	host->ahb_ufs_ies_en.regmap =
			syscon_regmap_lookup_by_phandle_args(np, "ahb_ufs_ies_en", 2, args);
	if (IS_ERR(host->ahb_ufs_ies_en.regmap)) {
		pr_err("failed to get ahb_ufs_ies_en\n");
		return PTR_ERR(host->ahb_ufs_ies_en.regmap);
	} else {
		host->ahb_ufs_ies_en.reg = args[0];
		host->ahb_ufs_ies_en.mask = args[1];
	}

	host->ahb_ufs_cg_pclkreq.regmap =
			syscon_regmap_lookup_by_phandle_args(np, "ahb_ufs_cg_pclkreq", 2, args);
	if (IS_ERR(host->ahb_ufs_cg_pclkreq.regmap)) {
		pr_err("failed to get ahb_ufs_cg_pclkreq\n");
		return PTR_ERR(host->ahb_ufs_cg_pclkreq.regmap);
	} else {
		host->ahb_ufs_cg_pclkreq.reg = args[0];
		host->ahb_ufs_cg_pclkreq.mask = args[1];
	}

	host->pclk = devm_clk_get(&pdev->dev, "ufs_pclk");
	if (IS_ERR(host->pclk)) {
		dev_warn(&pdev->dev,
			"can't get the clock dts config: ufs_pclk\n");
			host->pclk = NULL;
	}

	host->pclk_source = devm_clk_get(&pdev->dev, "ufs_pclk_source");
	if (IS_ERR(host->pclk_source)) {
		dev_warn(&pdev->dev,
			"can't get the clock dts config: ufs_pclk_source\n");
			host->pclk_source = NULL;
	}
	clk_set_parent(host->pclk, host->pclk_source);

	host->hclk = devm_clk_get(&pdev->dev, "ufs_hclk");
	if (IS_ERR(host->hclk)) {
		dev_warn(&pdev->dev,
			"can't get the clock dts config: ufs_hclk\n");
			host->hclk = NULL;
	}

	host->hclk_source = devm_clk_get(&pdev->dev, "ufs_hclk_source");
	if (IS_ERR(host->hclk_source)) {
		dev_warn(&pdev->dev,
			"can't get the clock dts config: ufs_hclk_source\n");
			host->hclk_source = NULL;
	}
	clk_set_parent(host->hclk, host->hclk_source);

	return 0;
}

static inline int ufs_sprd_mask(void __iomem *base, u32 mask, u32 reg)
{
	u32 tmp;

	tmp = readl((base) + (reg));
	if (tmp & mask)
		return 1;
	else
		return 0;
}

/*
 * ufs_sprd_rmwl - read modify write into a register
 * @base - base address
 * @mask - mask to apply on read value
 * @val - actual value to write
 * @reg - register address
 */
static inline void ufs_sprd_rmwl(void __iomem *base, u32 mask, u32 val, u32 reg)
{
	u32 tmp;

	tmp = readl((base) + (reg));
	tmp &= ~mask;
	tmp |= (val & mask);
	writel(tmp, (base) + (reg));
}
static void ufs_remap_or(struct syscon_ufs *sysconufs)
{
	unsigned int value = 0;

	regmap_read(sysconufs->regmap,
		    sysconufs->reg, &value);
	value =	value | sysconufs->mask;
	regmap_write(sysconufs->regmap,
		     sysconufs->reg, value);
}
void ufs_sprd_reset_pre(struct ufs_sprd_host *host)
{
	ufs_remap_or(&(host->ap_ahb_ufs_clk));
	regmap_update_bits(host->aon_apb_ufs_en.regmap,
			   host->aon_apb_ufs_en.reg,
			   host->aon_apb_ufs_en.mask,
			   host->aon_apb_ufs_en.mask);
	regmap_update_bits(host->ahb_ufs_lp.regmap,
			   host->ahb_ufs_lp.reg,
			   host->ahb_ufs_lp.mask,
			   host->ahb_ufs_lp.mask);
	regmap_update_bits(host->ahb_ufs_force_isol.regmap,
			   host->ahb_ufs_force_isol.reg,
			   host->ahb_ufs_force_isol.mask,
			   0);

	if (readl(host->aon_apb_reg + REG_AON_APB_AON_VER_ID))
		regmap_update_bits(host->ahb_ufs_ies_en.regmap,
				  host->ahb_ufs_ies_en.reg,
				  host->ahb_ufs_ies_en.mask,
				  host->ahb_ufs_ies_en.mask);
}

int ufs_sprd_reset(struct ufs_sprd_host *host)
{
	int ret = 0;
	u32 aon_ver_id = 0;

	sprd_get_soc_id(AON_VER_ID, &aon_ver_id, 1);

	dev_info(host->hba->dev, "ufs hardware reset!\n");
	/* TODO: HW reset will be simple in next version. */

	regmap_update_bits(host->ap_apb_ufs_en.regmap,
			   host->ap_apb_ufs_en.reg,
			   host->ap_apb_ufs_en.mask,
			   0);

	/* ufs global reset */
	ret = reset_control_assert(host->ap_apb_ufs_glb_rst);
	if (ret) {
		dev_err(host->hba->dev, "assert ufs_glb_rst failed, ret = %d!\n", ret);
		goto out;
	}
	usleep_range(10, 20);
	ret = reset_control_deassert(host->ap_apb_ufs_glb_rst);
	if (ret) {
		dev_err(host->hba->dev, "deassert ufs_glb_rst failed, ret = %d!\n", ret);
		goto out;
	}

	/* Configs need strict squence. */
	regmap_update_bits(host->ap_apb_ufs_en.regmap,
			   host->ap_apb_ufs_en.reg,
			   host->ap_apb_ufs_en.mask,
			   host->ap_apb_ufs_en.mask);
	/* ahb enable */
	ufs_remap_or(&(host->ap_ahb_ufs_clk));

	regmap_update_bits(host->aon_apb_ufs_en.regmap,
			   host->aon_apb_ufs_en.reg,
			   host->aon_apb_ufs_en.mask,
			   host->aon_apb_ufs_en.mask);

	/* cbline reset */
	regmap_update_bits(host->ahb_ufs_cb.regmap,
			   host->ahb_ufs_cb.reg,
			   host->ahb_ufs_cb.mask,
			   host->ahb_ufs_cb.mask);

	/* apb reset */
	ufs_sprd_rmwl(host->ufs_analog_reg, MPHY_2T2R_APB_RESETN,
			0, MPHY_2T2R_APB_REG1);
	usleep_range(1000, 1100);
	ufs_sprd_rmwl(host->ufs_analog_reg, MPHY_2T2R_APB_RESETN,
			MPHY_2T2R_APB_RESETN, MPHY_2T2R_APB_REG1);


	/* phy config */
	ufs_sprd_rmwl(host->ufs_analog_reg, MPHY_CDR_MONITOR_BYPASS_MASK,
			MPHY_CDR_MONITOR_BYPASS_ENABLE, MPHY_DIG_CFG7_LANE0);
	ufs_sprd_rmwl(host->ufs_analog_reg, MPHY_CDR_MONITOR_BYPASS_MASK,
			MPHY_CDR_MONITOR_BYPASS_ENABLE, MPHY_DIG_CFG7_LANE1);
	ufs_sprd_rmwl(host->ufs_analog_reg, MPHY_RXOFFSETCALDONEOVR_MASK,
			MPHY_RXOFFSETCALDONEOVR_ENABLE, MPHY_DIG_CFG20_LANE0);
	ufs_sprd_rmwl(host->ufs_analog_reg, MPHY_RXOFFOVRVAL_MASK,
			MPHY_RXOFFOVRVAL_ENABLE, MPHY_DIG_CFG20_LANE0);
	ufs_sprd_rmwl(host->ufs_analog_reg, MPHY_RXCFGG1_MASK,
			MPHY_RXCFGG1_VAL, MPHY_DIG_CFG49_LANE0);
	ufs_sprd_rmwl(host->ufs_analog_reg, MPHY_RXCFGG1_MASK,
			MPHY_RXCFGG1_VAL, MPHY_DIG_CFG49_LANE1);
	ufs_sprd_rmwl(host->ufs_analog_reg, MPHY_RXCFGG3_MASK,
			MPHY_RXCFGG3_VAL, MPHY_DIG_CFG51_LANE0);
	ufs_sprd_rmwl(host->ufs_analog_reg, MPHY_RXCFGG3_MASK,
			MPHY_RXCFGG3_VAL, MPHY_DIG_CFG51_LANE1);
	ufs_sprd_rmwl(host->ufs_analog_reg, FIFO_ENABLE_MASK,
			FIFO_ENABLE_MASK, MPHY_LANE0_FIFO);
	ufs_sprd_rmwl(host->ufs_analog_reg, FIFO_ENABLE_MASK,
			FIFO_ENABLE_MASK, MPHY_LANE1_FIFO);
	ufs_sprd_rmwl(host->ufs_analog_reg, MPHY_TACTIVATE_TIME_200US,
			MPHY_TACTIVATE_TIME_200US, MPHY_TACTIVATE_TIME_LANE0);
	ufs_sprd_rmwl(host->ufs_analog_reg, MPHY_TACTIVATE_TIME_200US,
			MPHY_TACTIVATE_TIME_200US, MPHY_TACTIVATE_TIME_LANE1);
	ufs_sprd_rmwl(host->ufs_analog_reg, MPHY_RXHSG3SYNCCAP_MASK,
			MPHY_RXHSG3SYNCCAP_VAL, MPHY_DIG_CFG72_LANE0);
	ufs_sprd_rmwl(host->ufs_analog_reg, MPHY_RXHSG3SYNCCAP_MASK,
			MPHY_RXHSG3SYNCCAP_VAL, MPHY_DIG_CFG72_LANE1);

	/* add cdr count time */
	ufs_sprd_rmwl(host->ufs_analog_reg, MPHY_RX_STEP4_CYCLE_G3_MASK,
			MPHY_RX_STEP4_CYCLE_G3_VAL, MPHY_DIG_CFG60_LANE0);
	ufs_sprd_rmwl(host->ufs_analog_reg, MPHY_RX_STEP4_CYCLE_G3_MASK,
			MPHY_RX_STEP4_CYCLE_G3_VAL, MPHY_DIG_CFG60_LANE1);

	/* cbline reset */
	regmap_update_bits(host->ahb_ufs_cb.regmap,
			  host->ahb_ufs_cb.reg,
			  host->ahb_ufs_cb.mask,
			  0);

	/* enable refclk */
	regmap_update_bits(host->ufs_refclk_on.regmap,
			  host->ufs_refclk_on.reg,
			  host->ufs_refclk_on.mask,
			  host->ufs_refclk_on.mask);
	regmap_update_bits(host->ahb_ufs_lp.regmap,
			  host->ahb_ufs_lp.reg,
			  host->ahb_ufs_lp.mask,
			  host->ahb_ufs_lp.mask);
	regmap_update_bits(host->ahb_ufs_force_isol.regmap,
			  host->ahb_ufs_force_isol.reg,
			  host->ahb_ufs_force_isol.mask,
			  0);

	/* ufs soft reset */
	ret = reset_control_assert(host->ap_apb_ufs_rst);
	if (ret) {
		dev_err(host->hba->dev, "assert ufs_rst failed, ret = %d!\n", ret);
		goto out;
	}
	usleep_range(10, 20);
	ret = reset_control_deassert(host->ap_apb_ufs_rst);
	if (ret) {
		dev_err(host->hba->dev, "deassert ufs_rst failed, ret = %d!\n", ret);
		goto out;
	}

	regmap_update_bits(host->ahb_ufs_ies_en.regmap,
			  host->ahb_ufs_ies_en.reg,
			  host->ahb_ufs_ies_en.mask,
			  host->ahb_ufs_ies_en.mask);
	ufs_remap_or(&(host->ahb_ufs_cg_pclkreq));

	ufs_sprd_rmwl(host->ufs_analog_reg, MPHY_ANR_MPHY_CTRL2_REFCLKON_MASK,
			MPHY_ANR_MPHY_CTRL2_REFCLKON_VAL, MPHY_ANR_MPHY_CTRL2);
	usleep_range(1, 2);
	ufs_sprd_rmwl(host->ufs_analog_reg, MPHY_REG_SEL_CFG_0_REFCLKON_MASK,
			MPHY_REG_SEL_CFG_0_REFCLKON_VAL, MPHY_REG_SEL_CFG_0);
	usleep_range(1, 2);
	ufs_sprd_rmwl(host->ufs_analog_reg, MPHY_APB_REFCLK_AUTOH8_EN_MASK,
			MPHY_APB_REFCLK_AUTOH8_EN_VAL, MPHY_DIG_CFG14_LANE0);

	usleep_range(1, 2);
	if (aon_ver_id == AON_VER_UFS) {
		ufs_sprd_rmwl(host->ufs_analog_reg, MPHY_APB_PLLTIMER_MASK,
				MPHY_APB_PLLTIMER_VAL, MPHY_DIG_CFG18_LANE0);
		ufs_sprd_rmwl(host->ufs_analog_reg,
				MPHY_APB_HSTXSCLKINV1_MASK,
				MPHY_APB_HSTXSCLKINV1_VAL,
				MPHY_DIG_CFG19_LANE0);
	}

	ufs_sprd_rmwl(host->ufs_analog_reg, MPHY_APB_RX_CFGRXBIASLSENVAL_MASK,
			MPHY_APB_RX_CFGRXBIASLSENVAL_MASK, MPHY_DIG_CFG32_LANE0);
	ufs_sprd_rmwl(host->ufs_analog_reg, MPHY_APB_RX_CFGRXBIASLSENOVR_MASK,
			MPHY_APB_RX_CFGRXBIASLSENOVR_MASK, MPHY_DIG_CFG32_LANE0);
	ufs_sprd_rmwl(host->ufs_analog_reg, MPHY_APB_OVR_REG_LS_LDO_STABLE_MASK,
			MPHY_APB_OVR_REG_LS_LDO_STABLE_MASK, MPHY_DIG_CFG1_LANE0);
	ufs_sprd_rmwl(host->ufs_analog_reg, MPHY_APB_REG_LS_LDO_STABLE_MASK,
			MPHY_APB_REG_LS_LDO_STABLE_MASK, MPHY_DIG_CFG17_LANE0);

	ufs_sprd_rmwl(host->ufs_analog_reg, MPHY_APB_RX_CFGRXBIASLSENVAL_MASK,
			MPHY_APB_RX_CFGRXBIASLSENVAL_MASK, MPHY_DIG_CFG32_LANE1);
	ufs_sprd_rmwl(host->ufs_analog_reg, MPHY_APB_RX_CFGRXBIASLSENOVR_MASK,
			MPHY_APB_RX_CFGRXBIASLSENOVR_MASK, MPHY_DIG_CFG32_LANE1);
	ufs_sprd_rmwl(host->ufs_analog_reg, MPHY_APB_OVR_REG_LS_LDO_STABLE_MASK,
			MPHY_APB_OVR_REG_LS_LDO_STABLE_MASK, MPHY_DIG_CFG1_LANE1);
	ufs_sprd_rmwl(host->ufs_analog_reg, MPHY_APB_REG_LS_LDO_STABLE_MASK,
			MPHY_APB_REG_LS_LDO_STABLE_MASK, MPHY_DIG_CFG17_LANE1);

out:
	return ret;
}

static int is_ufs_sprd_host_in_pwm(struct ufs_hba *hba)
{
	int ret = 0;
	u32 pwr_mode = 0;

	ret = ufshcd_dme_get(hba, UIC_ARG_MIB(PA_PWRMODE),
			     &pwr_mode);
	if (ret)
		goto out;
	if (((pwr_mode>>0)&0xf) == SLOWAUTO_MODE ||
		((pwr_mode>>0)&0xf) == SLOW_MODE     ||
		((pwr_mode>>4)&0xf) == SLOWAUTO_MODE ||
		((pwr_mode>>4)&0xf) == SLOW_MODE) {
		ret = SLOW_MODE | (SLOW_MODE << 4);
	}

out:
	return ret;
}

static int sprd_ufs_pwrchange(struct ufs_hba *hba)
{
	int ret;
	struct ufs_pa_layer_attr pwr_info;

	pwr_info.gear_rx = UFS_PWM_G1;
	pwr_info.gear_tx = UFS_PWM_G1;
	pwr_info.lane_rx = 1;
	pwr_info.lane_tx = 1;
	pwr_info.pwr_rx = SLOW_MODE;
	pwr_info.pwr_tx = SLOW_MODE;
	pwr_info.hs_rate = 0;

	ret = ufshcd_config_pwr_mode(hba, &(pwr_info));
	if (ret)
		goto out;
	if ((((hba->max_pwr_info.info.pwr_tx) << 4) |
		(hba->max_pwr_info.info.pwr_rx)) == HS_MODE_VAL)
		ret = ufshcd_config_pwr_mode(hba, &(hba->max_pwr_info.info));

out:
	return ret;

}

void read_ufs_debug_bus(struct ufs_hba *hba)
{
	u32 sigsel[] = {0x1, 0x16, 0x17, 0x1D, 0x1E, 0x1F, 0x20, 0x21};
	u32 debugbus_data;
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);
	int i;

	if (!host->dbg_apb_reg) {
		dev_warn(hba->dev, "can't get ufs debug bus base.\n");
		return;
	}

	/* read aon ufs mphy debugbus */
	dev_err(hba->dev, "No ufs mphy debugbus single.\n");

	/* read ap ufshcd debugbus */
	writel(0x0, host->dbg_apb_reg + 0x18);
	dev_err(hba->dev, "ap ufshcd debugbus_data as follow(syssel:0x0):\n");
	for (i = 0; i < ARRAY_SIZE(sigsel); i++) {
		writel(sigsel[i] << 8, host->dbg_apb_reg + 0x1c);
		debugbus_data = readl(host->dbg_apb_reg + 0x50);
		dev_err(hba->dev, "sig_sel: 0x%x. debugbus_data: 0x%x\n", sigsel[i], debugbus_data);
	}
	dev_err(hba->dev, "ap ufshcd debugbus_data end.\n");
}

/*
 * ufs_sprd_init - find other essential mmio bases
 * @hba: host controller instance
 * Returns 0 on success, non-zero value on failure
 */
static int ufs_sprd_init(struct ufs_hba *hba)
{
	struct device *dev = hba->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct ufs_sprd_host *host;
	struct resource *res;

	host = devm_kzalloc(dev, sizeof(*host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	host->hba = hba;
	ufshcd_set_variant(hba, host);

	syscon_get_args(dev, host);

	host->ap_apb_ufs_rst = devm_reset_control_get(dev, "ufs_rst");
	if (IS_ERR(host->ap_apb_ufs_rst)) {
		dev_err(dev, "%s get ufs_rst failed, err%ld\n",
			__func__, PTR_ERR(host->ap_apb_ufs_rst));
		host->ap_apb_ufs_rst = NULL;
		return -ENODEV;
	}

	host->ap_apb_ufs_glb_rst = devm_reset_control_get(dev, "ufs_glb_rst");
	if (IS_ERR(host->ap_apb_ufs_glb_rst)) {
		dev_err(dev, "%s get ufs_glb_rst failed, err%ld\n",
			__func__, PTR_ERR(host->ap_apb_ufs_glb_rst));
		host->ap_apb_ufs_glb_rst = NULL;
		return -ENODEV;
	}

	hba->host->hostt->ioctl = ufshcd_sprd_ioctl;
#ifdef CONFIG_COMPAT
	hba->host->hostt->compat_ioctl = ufshcd_sprd_ioctl;
#endif

	hba->quirks |= UFSHCD_QUIRK_BROKEN_UFS_HCI_VERSION |
		       UFSHCD_QUIRK_DELAY_BEFORE_DME_CMDS;
	hba->caps |= UFSHCD_CAP_CLK_GATING | UFSHCD_CAP_CRYPTO |
		     UFSHCD_CAP_WB_EN | UFSHCD_CAP_HIBERN8_WITH_CLK_GATING;

	res = platform_get_resource_byname(pdev,
					IORESOURCE_MEM, "ufs_analog_reg");
	if (!res) {
		dev_err(dev, "Missing ufs_analog_reg register resource\n");
		return -ENODEV;
	}
	host->ufs_analog_reg = devm_ioremap_nocache(dev, res->start,
	resource_size(res));
	if (IS_ERR(host->ufs_analog_reg)) {
		dev_err(dev, "%s: could not map ufs_analog_reg, err %ld\n",
			__func__, PTR_ERR(host->ufs_analog_reg));
		host->ufs_analog_reg = NULL;
		return -ENODEV;
	}

	res = platform_get_resource_byname(pdev,
			IORESOURCE_MEM, "aon_apb_reg");
	if (!res) {
		dev_err(dev, "Missing aon_apb_reg register resource\n");
		return -ENODEV;
	}
	host->aon_apb_reg = devm_ioremap_nocache(dev, res->start,
			resource_size(res));
	if (IS_ERR(host->aon_apb_reg)) {
		dev_err(dev, "%s: could not map aon_apb_reg, err %ld\n",
				__func__, PTR_ERR(host->aon_apb_reg));
		host->aon_apb_reg = NULL;
		return -ENODEV;
	}

	host->dbg_apb_reg = devm_ioremap(dev, REG_DEBUG_APB_BASE, 0x100);
	if (IS_ERR(host->dbg_apb_reg)) {
		pr_err("error to ioremap ufs debug bus base.\n");
		host->dbg_apb_reg = NULL;
	}

	ufs_sprd_reset_pre(host);
	return 0;
}

/*
 * ufs_sprd_hw_init - controller enable and reset
 * @hba: host controller instance
 */
int ufs_sprd_hw_init(struct ufs_hba *hba)
{
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);

	return ufs_sprd_reset(host);
}

static void ufs_sprd_exit(struct ufs_hba *hba)
{
	struct device *dev = hba->dev;
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);

	devm_kfree(dev, host);
	hba->priv = NULL;
}

static u32 ufs_sprd_get_ufs_hci_version(struct ufs_hba *hba)
{
	return UFSHCI_VERSION_21;
}

static int ufs_sprd_hce_enable_notify(struct ufs_hba *hba,
				      enum ufs_notify_change_status status)
{
	int err = 0;
	unsigned long flags;

	switch (status) {
	case PRE_CHANGE:
		/* Do hardware reset before host controller enable. */
		err = ufs_sprd_hw_init(hba);
		if (err) {
			dev_err(hba->dev, "%s: ufs hardware init failed!\n", __func__);
			return err;
		}

		spin_lock_irqsave(hba->host->host_lock, flags);
		ufshcd_writel(hba, 0, REG_AUTO_HIBERNATE_IDLE_TIMER);
		spin_unlock_irqrestore(hba->host->host_lock, flags);
		hba->capabilities &= ~MASK_AUTO_HIBERN8_SUPPORT;
		hba->ahit = 0;

		ufshcd_writel(hba, CONTROLLER_ENABLE, REG_CONTROLLER_ENABLE);
		break;
	case POST_CHANGE:
		ufshcd_writel(hba, CLKDIV, HCLKDIV_REG);
		break;
	default:
		dev_err(hba->dev, "%s: invalid status %d\n", __func__, status);
		err = -EINVAL;
		break;
	}

	return err;
}

static int ufs_sprd_apply_dev_quirks(struct ufs_hba *hba)
{
	int ret = 0;
	u32 granularity, peer_granularity;
	u32 pa_tactivate, peer_pa_tactivate;
	u32 pa_tactivate_us, peer_pa_tactivate_us, max_pa_tactivate_us;
	u8 gran_to_us_table[] = {1, 4, 8, 16, 32, 100};
	u32 new_pa_tactivate, new_peer_pa_tactivate;
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);

	host->wlun_dev_add = true;

	ret = ufshcd_dme_get(hba, UIC_ARG_MIB(PA_GRANULARITY),
				  &granularity);
	if (ret)
		goto out;

	ret = ufshcd_dme_peer_get(hba, UIC_ARG_MIB(PA_GRANULARITY),
				  &peer_granularity);
	if (ret)
		goto out;

	if ((granularity < PA_GRANULARITY_MIN_VAL) ||
	    (granularity > PA_GRANULARITY_MAX_VAL)) {
		dev_err(hba->dev, "%s: invalid host PA_GRANULARITY %d",
			__func__, granularity);
		return -EINVAL;
	}

	if ((peer_granularity < PA_GRANULARITY_MIN_VAL) ||
	    (peer_granularity > PA_GRANULARITY_MAX_VAL)) {
		dev_err(hba->dev, "%s: invalid device PA_GRANULARITY %d",
			__func__, peer_granularity);
		return -EINVAL;
	}

	ret = ufshcd_dme_get(hba, UIC_ARG_MIB(PA_TACTIVATE), &pa_tactivate);
	if (ret)
		goto out;

	ret = ufshcd_dme_peer_get(hba, UIC_ARG_MIB(PA_TACTIVATE),
				  &peer_pa_tactivate);
	if (ret)
		goto out;

	pa_tactivate_us = pa_tactivate * gran_to_us_table[granularity - 1];
	peer_pa_tactivate_us = peer_pa_tactivate *
			gran_to_us_table[peer_granularity - 1];
	max_pa_tactivate_us = (pa_tactivate_us > peer_pa_tactivate_us) ?
			pa_tactivate_us : peer_pa_tactivate_us;

	new_peer_pa_tactivate = (max_pa_tactivate_us + 400) /
			gran_to_us_table[peer_granularity - 1];

	ret = ufshcd_dme_peer_set(hba, UIC_ARG_MIB(PA_TACTIVATE),
				  new_peer_pa_tactivate);
	if (ret) {
		dev_err(hba->dev, "%s: peer_pa_tactivate set err ", __func__);
		goto out;
	}

	new_pa_tactivate = (max_pa_tactivate_us + 300) /
			gran_to_us_table[granularity - 1];
	ret = ufshcd_dme_set(hba, UIC_ARG_MIB(PA_TACTIVATE),
			     new_pa_tactivate);
	if (ret) {
		dev_err(hba->dev, "%s: pa_tactivate set err ", __func__);
		goto out;
	}

	dev_warn(hba->dev, "%s: %d,%d,%d,%d",
		 __func__, new_peer_pa_tactivate,
		 peer_granularity, new_pa_tactivate, granularity);

out:
	return ret;
}

static int ufs_sprd_link_startup_notify(struct ufs_hba *hba,
					enum ufs_notify_change_status status)
{
	int err = 0;

	switch (status) {
	case PRE_CHANGE:
		/* UFS device needs 32us PA_Saveconfig Time */
		ufshcd_dme_set(hba, UIC_ARG_MIB(VS_DEBUGSAVECONFIGTIME), 0x13);

		/*
		 * Some UFS devices (and may be host) have issues if LCC is
		 * enabled. So we are setting PA_Local_TX_LCC_Enable to 0
		 * before link startup which will make sure that both host
		 * and device TX LCC are disabled once link startup is
		 * completed.
		 */
		if (ufshcd_get_local_unipro_ver(hba) != UFS_UNIPRO_VER_1_41)
			err = ufshcd_dme_set(hba,
					UIC_ARG_MIB(PA_LOCAL_TX_LCC_ENABLE),
					0);

		break;
	case POST_CHANGE:
		hba->clk_gating.delay_ms = 10;
		break;
	default:
		err = -EINVAL;
		break;
	}

	return err;
}
static int ufs_compare_dev_req_pwr_mode(struct ufs_hba *hba, struct ufs_pa_layer_attr *dev_req_params)
{
	struct ufs_pa_layer_attr  max_pwr_info_raw = {0};
	struct ufs_pa_layer_attr *max_pwr_info = &max_pwr_info_raw;
	struct ufs_pa_layer_attr *pwr_info = dev_req_params;
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);


	/* Get the connected lane count */
	ufshcd_dme_get(hba, UIC_ARG_MIB(PA_CONNECTEDRXDATALANES),
			&max_pwr_info->lane_rx);
	ufshcd_dme_get(hba, UIC_ARG_MIB(PA_CONNECTEDTXDATALANES),
			&max_pwr_info->lane_tx);
	/*
	 *  First, get the maximum gears of HS speed.
	 *  If a zero value, it means there is no HSGEAR capability.
	 *  Then, get the maximum gears of PWM speed.
	 *  */
	if (pwr_info->pwr_tx == FAST_MODE)
		ufshcd_dme_get(hba, UIC_ARG_MIB(PA_MAXRXHSGEAR),
				&max_pwr_info->gear_rx);
	else if (pwr_info->pwr_tx == SLOW_MODE)
		ufshcd_dme_get(hba, UIC_ARG_MIB(PA_MAXRXPWMGEAR),
				&max_pwr_info->gear_rx);

	if (pwr_info->pwr_rx == FAST_MODE)
		ufshcd_dme_peer_get(hba, UIC_ARG_MIB(PA_MAXRXHSGEAR),
				&max_pwr_info->gear_tx);
	else if (pwr_info->pwr_rx == SLOW_MODE)
		ufshcd_dme_peer_get(hba, UIC_ARG_MIB(PA_MAXRXPWMGEAR),
				&max_pwr_info->gear_tx);

	memcpy(&(host->dts_pwr_info), pwr_info, sizeof(struct ufs_pa_layer_attr));

	/* if already configured to the requested pwr_mode */
	if (max_pwr_info->gear_rx < pwr_info->gear_rx  ||
			max_pwr_info->gear_tx < pwr_info->gear_tx  ||
			max_pwr_info->lane_rx < pwr_info->lane_rx  ||
			max_pwr_info->lane_tx < pwr_info->lane_tx) {
		dev_err(hba->dev, "%s: the dev_req_pwr can not compare\n", __func__);
		return -EINVAL;
	}

	return 0;
}


static int ufs_compare_max_pwr_mode(struct ufs_hba *hba)
{
	struct ufs_pa_layer_attr  pwr_info_raw;
	struct ufs_pa_layer_attr *pwr_info = &pwr_info_raw;
	struct ufs_pa_layer_attr *max_pwr_info = &hba->max_pwr_info.info;
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);

	if (!hba->max_pwr_info.is_valid && (max_pwr_info->pwr_tx != 1))
		return -EINVAL;

	pwr_info->pwr_tx = FAST_MODE;
	pwr_info->pwr_rx = FAST_MODE;
	pwr_info->hs_rate = PA_HS_MODE_B;

	/* Get the connected lane count */
	ufshcd_dme_get(hba, UIC_ARG_MIB(PA_CONNECTEDRXDATALANES),
			&pwr_info->lane_rx);
	ufshcd_dme_get(hba, UIC_ARG_MIB(PA_CONNECTEDTXDATALANES),
			&pwr_info->lane_tx);

	if (!pwr_info->lane_rx || !pwr_info->lane_tx) {
		dev_err(hba->dev, "%s: invalid connected lanes value. rx=%d, tx=%d\n",
				__func__,
				pwr_info->lane_rx,
				pwr_info->lane_tx);
		return -EINVAL;
	}

	/*
	 * First, get the maximum gears of HS speed.
	 * If a zero value, it means there is no HSGEAR capability.
	 * Then, get the maximum gears of PWM speed.
	 *  */
	ufshcd_dme_get(hba, UIC_ARG_MIB(PA_MAXRXHSGEAR), &pwr_info->gear_rx);
	if (!pwr_info->gear_rx) {
		ufshcd_dme_get(hba, UIC_ARG_MIB(PA_MAXRXPWMGEAR),
				&pwr_info->gear_rx);
		if (!pwr_info->gear_rx) {
			dev_err(hba->dev, "%s: invalid max pwm rx gear read = %d\n",
					__func__, pwr_info->gear_rx);
			return -EINVAL;
		}
		pwr_info->pwr_rx = SLOW_MODE;
	}

	ufshcd_dme_peer_get(hba, UIC_ARG_MIB(PA_MAXRXHSGEAR),
			&pwr_info->gear_tx);
	if (!pwr_info->gear_tx) {
		ufshcd_dme_peer_get(hba, UIC_ARG_MIB(PA_MAXRXPWMGEAR),
				&pwr_info->gear_tx);
		if (!pwr_info->gear_tx) {
			dev_err(hba->dev, "%s: invalid max pwm tx gear read = %d\n",
					__func__, pwr_info->gear_tx);
			return -EINVAL;
		}
		pwr_info->pwr_tx = SLOW_MODE;
	}
	memcpy(&(host->dts_pwr_info), max_pwr_info, sizeof(struct ufs_pa_layer_attr));

	/* if already configured to the requested pwr_mode */
	if (pwr_info->gear_rx != max_pwr_info->gear_rx  ||
			pwr_info->gear_tx != max_pwr_info->gear_tx  ||
			pwr_info->lane_rx != max_pwr_info->lane_rx   ||
			pwr_info->lane_tx != max_pwr_info->lane_tx   ||
			pwr_info->pwr_rx != max_pwr_info->pwr_rx     ||
			pwr_info->pwr_tx != max_pwr_info->pwr_tx     ||
			pwr_info->hs_rate != max_pwr_info->hs_rate) {
		dev_err(hba->dev, "%s: the max can not compare\n", __func__);
		return -EINVAL;
	}
	return 0;
}
static int ufs_sprd_pwr_post_compare(struct ufs_hba *hba)
{
	struct ufs_pa_layer_attr  pwr_info_raw = {0};
	struct ufs_pa_layer_attr *pwr_mode = &pwr_info_raw;
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);
	int ret = 0, pwr = 0;

	ret = ufshcd_dme_get(hba, UIC_ARG_MIB(PA_RXGEAR),
			&pwr_mode->gear_rx);
	if (ret)
		goto out;

	ret = ufshcd_dme_get(hba, UIC_ARG_MIB(PA_TXGEAR),
			&pwr_mode->gear_tx);
	if (ret)
		goto out;

	ret = ufshcd_dme_get(hba, UIC_ARG_MIB(PA_ACTIVERXDATALANES),
			&pwr_mode->lane_rx);
	if (ret)
		goto out;

	ret = ufshcd_dme_get(hba, UIC_ARG_MIB(PA_ACTIVETXDATALANES),
			&pwr_mode->lane_tx);
	if (ret)
		goto out;

	ret = ufshcd_dme_get(hba, UIC_ARG_MIB(PA_HSSERIES),
			&pwr_mode->hs_rate);
	if (ret)
		goto out;

	ret = ufshcd_dme_get(hba, UIC_ARG_MIB(PA_PWRMODE),
			&pwr);
	if (ret)
		goto out;
	pwr_mode->pwr_rx = (pwr >> 4) & 0xf;
	pwr_mode->pwr_tx = (pwr >> 0) & 0xf;
	if (pwr_mode->gear_rx == host->dts_pwr_info.gear_rx &&
			pwr_mode->gear_tx == host->dts_pwr_info.gear_tx &&
			pwr_mode->lane_rx == host->dts_pwr_info.lane_rx &&
			pwr_mode->lane_tx == host->dts_pwr_info.lane_tx &&
			pwr_mode->pwr_rx == host->dts_pwr_info.pwr_rx &&
			pwr_mode->pwr_tx == host->dts_pwr_info.pwr_tx &&
			pwr_mode->hs_rate == host->dts_pwr_info.hs_rate){
		pr_err("%s: ufs_sprd_pwr_post_compare success\n", __func__);
		return 0;
	}
out:
	return -1;
}

static int ufs_sprd_pwr_change_notify(struct ufs_hba *hba,
		enum ufs_notify_change_status status,
		struct ufs_pa_layer_attr *dev_max_params,
		struct ufs_pa_layer_attr *dev_req_params)
{
	int err = 0;
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);

	if (!dev_req_params) {
		pr_err("%s: incoming dev_req_params is NULL\n", __func__);
		err = -EINVAL;
		goto out;
	}

	switch (status) {
	case PRE_CHANGE:
		host->times_pre_pwr++;
		err = -EPERM;
		if (err == 0) {
			if (ufs_compare_dev_req_pwr_mode(hba, dev_req_params)) {
				host->times_pre_compare_fail++;
				dev_err(hba->dev, "%s: err: compare_pwr\n", __func__);
#if defined(CONFIG_SPRD_DEBUG)
				panic("pre_compare_fail");
#endif
			} else
				dev_err(hba->dev, "%s: suc: compare_pwr\n", __func__);

		} else {
			if (ufs_compare_max_pwr_mode(hba)) {
				host->times_pre_compare_fail++;
				dev_err(hba->dev, "%s: err:  compare_max_pwr\n", __func__);
#if defined(CONFIG_SPRD_DEBUG)
				panic("pre_compare_fail");
#endif
			} else
				dev_err(hba->dev, "%s: suc: compare_max_pwr\n", __func__);
		}
		break;
	case POST_CHANGE:
		host->times_post_pwr++;
		/* if already configured to the requested pwr_mode */
		if (ufs_sprd_pwr_post_compare(hba)) {
			host->times_post_compare_fail++;
			dev_err(hba->dev, "%s: power configured error\n", __func__);
#if defined(CONFIG_SPRD_DEBUG)
			panic("post_compare_fail");
#endif
		} else {
			dev_err(hba->dev, "%s: power already configured\n", __func__);
		}
		/* Set auto h8 ilde time to 10ms */
		//ufshcd_auto_hibern8_enable(hba);
		break;
	default:
		err = -EINVAL;
		break;
	}

out:
	return err;
}

void ufs_set_hstxsclk(struct ufs_hba *hba)
{
	int ret;
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);

	ret = ufs_sprd_mask(host->ufs_analog_reg,
			MPHY_APB_HSTXSCLKINV1_MASK,
			MPHY_DIG_CFG19_LANE0);
	if (!ret) {
		ufs_sprd_rmwl(host->ufs_analog_reg,
				MPHY_APB_HSTXSCLKINV1_MASK,
				MPHY_APB_HSTXSCLKINV1_VAL,
				MPHY_DIG_CFG19_LANE0);
		pr_err("ufs_pwm2hs set hstxsclk\n");
	}

}
static int sprd_ufs_pwmmode_change(struct ufs_hba *hba)
{
	int ret;
	struct ufs_pa_layer_attr pwr_info;

	ret = is_ufs_sprd_host_in_pwm(hba);
	if (ret == (SLOW_MODE|(SLOW_MODE<<4)))
		return 0;

	pwr_info.gear_rx = UFS_PWM_G3;
	pwr_info.gear_tx = UFS_PWM_G3;
	pwr_info.lane_rx = 2;
	pwr_info.lane_tx = 2;
	pwr_info.pwr_rx = SLOW_MODE;
	pwr_info.pwr_tx = SLOW_MODE;
	pwr_info.hs_rate = 0;

	ret = ufshcd_config_pwr_mode(hba, &(pwr_info));

	return ret;
}
int hibern8_exit_check(struct ufs_hba *hba,
				enum uic_cmd_dme cmd,
				enum ufs_notify_change_status status)
{
	int ret;
	u32 aon_ver_id = 0;
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);

	ret = is_ufs_sprd_host_in_pwm(hba);
	if (ret == (SLOW_MODE|(SLOW_MODE<<4))) {
		sprd_get_soc_id(AON_VER_ID, &aon_ver_id, 1);
		if (host->ioctl_cmd == UFS_IOCTL_AFC_EXIT ||
				aon_ver_id == AON_VER_UFS) {
			ret = sprd_ufs_pwrchange(hba);
			if (ret) {
				pr_err("ufs_pwm2hs err");
			} else {
				ret = is_ufs_sprd_host_in_pwm(hba);
				if (ret == (SLOW_MODE|(SLOW_MODE<<4)) &&
						((((hba->max_pwr_info.info.pwr_tx) << 4) |
						  (hba->max_pwr_info.info.pwr_rx)) == HS_MODE_VAL))
					pr_err("ufs_pwm2hs fail");
				else {
					pr_err("ufs_pwm2hs succ\n");
					if (host->ioctl_cmd ==
							UFS_IOCTL_AFC_EXIT)
						complete(&host->hs_async_done);
				}
			}
		}
	}
	return 0;

}
static void ufs_sprd_hibern8_notify(struct ufs_hba *hba,
		enum uic_cmd_dme cmd,
		enum ufs_notify_change_status status)
{
	int ret;
	unsigned long flags;
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);

	switch (status) {
	case PRE_CHANGE:
		if (cmd == UIC_CMD_DME_HIBER_ENTER) {
			spin_lock_irqsave(hba->host->host_lock, flags);
			ufshcd_writel(hba, 0, REG_AUTO_HIBERNATE_IDLE_TIMER);
			spin_unlock_irqrestore(hba->host->host_lock, flags);
		}
		break;
	case POST_CHANGE:
		if (cmd == UIC_CMD_DME_HIBER_EXIT) {
			hba->caps &= ~UFSHCD_CAP_CLK_GATING;
			if (host->ioctl_cmd == UFS_IOCTL_ENTER_MODE) {
				ret = sprd_ufs_pwmmode_change(hba);
				if (ret)
					pr_err("change pwm mode failed!\n");
				else
					complete(&host->pwm_async_done);
			} else {
				hibern8_exit_check(hba, cmd, status);
			}
			hba->caps |= UFSHCD_CAP_CLK_GATING;
			/* Set auto h8 ilde time to 10ms */
			//ufshcd_auto_hibern8_enable(hba);
		}
		break;
	default:
		break;
	}
}

static int ufs_sprd_suspend(struct ufs_hba *hba, enum ufs_pm_op pm_op)
{
	hba->rpm_lvl = UFS_PM_LVL_1;
	hba->spm_lvl = UFS_PM_LVL_5;
	hba->uic_link_state = UIC_LINK_OFF_STATE;

	mdelay(30);
	return 0;
}

static int ufs_sprd_resume(struct ufs_hba *hba, enum ufs_pm_op pm_op)
{
	udelay(100);
	return 0;
}

static void ufs_sprd_device_reset(struct ufs_hba *hba)
{
	return;
}

static inline u16 ufs_sprd_wlun_to_scsi_lun(u8 upiu_wlun_id)
{
	return (upiu_wlun_id & ~UFS_UPIU_WLUN_ID) | SCSI_W_LUN_BASE;
}

static int ufs_sprd_get_sdev(struct ufs_hba *hba, uint channel,
			     uint id, u64 lun)
{
	struct scsi_device *sdev_rpmb;
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);
	int ret = 0;

	sdev_rpmb = __scsi_add_device(hba->host, channel, id, lun, NULL);
	if (IS_ERR(sdev_rpmb)) {
		ret = PTR_ERR(sdev_rpmb);
		return ret;
	}
	host->sdev_ufs_rpmb = sdev_rpmb;

	return ret;
}

static inline int ufs_sprd_read_geometry_desc_param(struct ufs_hba *hba,
			enum geometry_desc_param param_offset,
			u8 *param_read_buf, u32 param_size)
{
	return ufshcd_read_desc_param(hba, QUERY_DESC_IDN_GEOMETRY, 0,
				      param_offset, param_read_buf, param_size);
}

#define SEC_PROTOCOL_UFS  0xEC
#define SEC_SPECIFIC_UFS_RPMB 0x0001
#define SEC_PROTOCOL_CMD_SIZE 12
#define SEC_PROTOCOL_RETRIES 3
#define SEC_PROTOCOL_RETRIES_ON_RESET 10
#define SEC_PROTOCOL_TIMEOUT msecs_to_jiffies(1000)

static int ufs_sprd_rpmb_security_out(struct scsi_device *sdev,
				      struct rpmb_frame *frames, u32 cnt)
{
	struct scsi_sense_hdr sshdr;
	u32 trans_len = cnt * sizeof(struct rpmb_frame);
	int reset_retries = SEC_PROTOCOL_RETRIES_ON_RESET;
	int ret;
	u8 cmd[SEC_PROTOCOL_CMD_SIZE];
	char *sense = NULL;

	sense = kzalloc(SCSI_SENSE_BUFFERSIZE, GFP_NOIO);
	if (!sense) {
		pr_err("%s sense alloc failed\n", __func__);
		return -1;
	}

	memset(cmd, 0, SEC_PROTOCOL_CMD_SIZE);
	cmd[0] = SECURITY_PROTOCOL_OUT;
	cmd[1] = SEC_PROTOCOL_UFS;
	put_unaligned_be16(SEC_SPECIFIC_UFS_RPMB, cmd + 2);
	cmd[4] = 0;
	put_unaligned_be32(trans_len, cmd + 6);

	ret = scsi_test_unit_ready(sdev, SEC_PROTOCOL_TIMEOUT,
				   SEC_PROTOCOL_RETRIES, &sshdr);
	if (ret)
		dev_err(&sdev->sdev_gendev,
			"%s: rpmb scsi_test_unit_ready, ret=%d\n",
			__func__, ret);

retry:
	ret = __scsi_execute(sdev, cmd, DMA_TO_DEVICE, frames, trans_len,
			     sense, &sshdr, SEC_PROTOCOL_TIMEOUT,
			     SEC_PROTOCOL_RETRIES, 0, 0, NULL);

	if (ret && scsi_sense_valid(&sshdr) &&
	    sshdr.sense_key == UNIT_ATTENTION &&
	    sshdr.asc == 0x29 && sshdr.ascq == 0x00)
		/*
		 * Device reset might occur several times,
		 * give it one more chance
		 */
		if (--reset_retries > 0)
			goto retry;

	if (ret)
		dev_err(&sdev->sdev_gendev, "%s: failed with err %0x\n",
			__func__, ret);

	if (driver_byte(ret) & DRIVER_SENSE)
		scsi_print_sense_hdr(sdev, "rpmb: security out", &sshdr);

	kfree(sense);
	return ret;
}

static int ufs_sprd_rpmb_security_in(struct scsi_device *sdev,
				      struct rpmb_frame *frames, u32 cnt)
{
	struct scsi_sense_hdr sshdr;
	u32 alloc_len = cnt * sizeof(struct rpmb_frame);
	int reset_retries = SEC_PROTOCOL_RETRIES_ON_RESET;
	int ret;
	u8 cmd[SEC_PROTOCOL_CMD_SIZE];
	char *sense = NULL;

	sense = kzalloc(SCSI_SENSE_BUFFERSIZE, GFP_NOIO);
	if (!sense) {
		pr_err("%s sense alloc failed\n", __func__);
		return -1;
	}
	memset(cmd, 0, SEC_PROTOCOL_CMD_SIZE);
	cmd[0] = SECURITY_PROTOCOL_IN;
	cmd[1] = SEC_PROTOCOL_UFS;
	put_unaligned_be16(SEC_SPECIFIC_UFS_RPMB, cmd + 2);
	cmd[4] = 0;
	put_unaligned_be32(alloc_len, cmd + 6);

	ret = scsi_test_unit_ready(sdev, SEC_PROTOCOL_TIMEOUT,
				   SEC_PROTOCOL_RETRIES, &sshdr);
	if (ret)
		dev_err(&sdev->sdev_gendev,
			"%s: rpmb scsi_test_unit_ready, ret=%d\n",
			__func__, ret);

retry:
	ret = __scsi_execute(sdev, cmd, DMA_FROM_DEVICE, frames, alloc_len,
			     sense, &sshdr, SEC_PROTOCOL_TIMEOUT,
			     SEC_PROTOCOL_RETRIES, 0, 0, NULL);

	if (ret && scsi_sense_valid(&sshdr) &&
	    sshdr.sense_key == UNIT_ATTENTION &&
	    sshdr.asc == 0x29 && sshdr.ascq == 0x00)
		/*
		 * Device reset might occur several times,
		 * give it one more chance
		 */
		if (--reset_retries > 0)
			goto retry;

	if (ret)
		dev_err(&sdev->sdev_gendev, "%s: failed with err %0x\n",
			__func__, ret);

	if (driver_byte(ret) & DRIVER_SENSE)
		scsi_print_sense_hdr(sdev, "rpmb: security in", &sshdr);

	kfree(sense);
	return ret;
}

static int ufs_rpmb_cmd_seq(struct device *dev,
			    struct rpmb_cmd *cmds, u32 ncmds)
{
	unsigned long flags;
	struct ufs_hba *hba = dev_get_drvdata(dev);
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);
	struct scsi_device *sdev;
	struct rpmb_cmd *cmd;
	int i;
	int ret;

	spin_lock_irqsave(hba->host->host_lock, flags);
	sdev = host->sdev_ufs_rpmb;
	if (sdev) {
		ret = scsi_device_get(sdev);
		if (!ret && !scsi_device_online(sdev)) {
			ret = -ENODEV;
			scsi_device_put(sdev);
		}
	} else {
		ret = -ENODEV;
	}
	spin_unlock_irqrestore(hba->host->host_lock, flags);
	if (ret)
		return ret;

	for (ret = 0, i = 0; i < ncmds && !ret; i++) {
		cmd = &cmds[i];
		if (cmd->flags & RPMB_F_WRITE)
			ret = ufs_sprd_rpmb_security_out(sdev, cmd->frames,
							 cmd->nframes);
		else
			ret = ufs_sprd_rpmb_security_in(sdev, cmd->frames,
							cmd->nframes);
	}
	scsi_device_put(sdev);

	return ret;
}

static struct rpmb_ops ufshcd_rpmb_dev_ops = {
	.cmd_seq = ufs_rpmb_cmd_seq,
	.type = RPMB_TYPE_UFS,
};

static inline void ufs_sprd_rpmb_add(struct ufs_hba *hba)
{
	struct rpmb_dev *rdev;
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);
	u8 rw_size;
	int ret;

	ret = ufs_sprd_get_sdev(hba, 0, 0,
			ufs_sprd_wlun_to_scsi_lun(UFS_UPIU_RPMB_WLUN));
	if (ret) {
		dev_warn(hba->dev, "Cannot get rpmb dev!\n");
		return;
	}

	ret = ufs_sprd_read_geometry_desc_param(hba, GEOMETRY_DESC_PARAM_RPMB_RW_SIZE,
					&rw_size, sizeof(rw_size));
	if (ret) {
		dev_warn(hba->dev, "%s: cannot get rpmb rw limit %d\n",
			dev_name(hba->dev), ret);
		rw_size = 1;
	}

	ufshcd_rpmb_dev_ops.reliable_wr_cnt = rw_size;

	ret = scsi_device_get(host->sdev_ufs_rpmb);
	rdev = rpmb_dev_register(hba->dev, &ufshcd_rpmb_dev_ops);
	if (IS_ERR(rdev)) {
		dev_warn(hba->dev, "%s: cannot register to rpmb %ld\n",
			 dev_name(hba->dev), PTR_ERR(rdev));
		goto out_put_dev;
	}

	scsi_device_put(host->sdev_ufs_rpmb);
	return;

out_put_dev:
	scsi_device_put(host->sdev_ufs_rpmb);
	host->sdev_ufs_rpmb = NULL;
	host->wlun_dev_add = false;
}

static inline void ufs_sprd_rpmb_remove(struct ufs_hba *hba)
{
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);

	if (!host || !host->sdev_ufs_rpmb)
		return;

	rpmb_dev_unregister(hba->dev);
	scsi_device_put(host->sdev_ufs_rpmb);
	host->sdev_ufs_rpmb = NULL;
	host->wlun_dev_add = false;
}
void ufs_sprd_setup_xfer_req(struct ufs_hba *hba, int task_tag, bool scsi_cmd)
{
	struct ufshcd_lrb *lrbp;
	struct utp_transfer_req_desc *req_desc;
	u32 data_direction;
	u32 dword_0, crypto;

	lrbp = &hba->lrb[task_tag];
	req_desc = lrbp->utr_descriptor_ptr;
	dword_0 = le32_to_cpu(req_desc->header.dword_0);
	data_direction = dword_0 & (UTP_DEVICE_TO_HOST | UTP_HOST_TO_DEVICE);
	crypto = dword_0 & UTP_REQ_DESC_CRYPTO_ENABLE_CMD;
	if (!data_direction && crypto) {
		pr_err("ufs before dword_0 = %x,%x\n", dword_0, req_desc->header.dword_0);
		dword_0 &= ~(UTP_REQ_DESC_CRYPTO_ENABLE_CMD);
		req_desc->header.dword_0 = cpu_to_le32(dword_0);
		pr_err("ufs after dword_0 = %x,%x\n", dword_0, req_desc->header.dword_0);
	}
}

static void ufs_sprd_dbg_register_dump(struct ufs_hba *hba)
{
	read_ufs_debug_bus(hba);
}

static void ufs_sprd_linkup_start_tstamp(struct ufs_hba *hba,
					 struct uic_command *ucmd)
{
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);

	if (ucmd->command == UIC_CMD_DME_LINK_STARTUP)
		host->linkup_start_tstamp = ktime_get();
}

static void ufs_sprd_dco_calibration(struct ufs_hba *hba,
				     struct uic_command *ucmd)
{
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);
	int value = 0;
	int apb_dco_cal_result = 0;

	if (ucmd->command == UIC_CMD_DME_LINK_STARTUP) {
		while(1) {
			if(ktime_to_us(ktime_sub(ktime_get(),
			   host->linkup_start_tstamp)) > WAIT_1MS_TIMEOUT) {
				value = readl((host->ufs_analog_reg) +
					       MPHY_DIG_CFG62_LANE0);
				apb_dco_cal_result = (value >> 24);
				break;
			}
		}

		if (apb_dco_cal_result < APB_DCO_CAL_RESULT_RANGE) {
			ufs_sprd_rmwl(host->ufs_analog_reg,
				      MPHY_APB_REG_DCO_CTRLBIT,
				      MPHY_APB_REG_DCO_VALUE,
				      MPHY_DIG_CFG15_LANE0);
			ufs_sprd_rmwl(host->ufs_analog_reg,
				      MPHY_APB_OVR_REG_DCO_CTRLBIT,
				      MPHY_APB_OVR_REG_DCO_VALUE,
				      MPHY_DIG_CFG1_LANE0);
		}
	}
}

/*
 * struct ufs_hba_sprd_vops - UFS sprd specific variant operations
 *
 * The variant operations configure the necessary controller and PHY
 * handshake during initialization.
 */
static struct ufs_hba_variant_ops ufs_hba_sprd_vops = {
	.name = "sprd",
	.init = ufs_sprd_init,
	.exit = ufs_sprd_exit,
	.get_ufs_hci_version = ufs_sprd_get_ufs_hci_version,
	.hce_enable_notify = ufs_sprd_hce_enable_notify,
	.link_startup_notify = ufs_sprd_link_startup_notify,
	.pwr_change_notify = ufs_sprd_pwr_change_notify,
	.hibern8_notify = ufs_sprd_hibern8_notify,
	.setup_xfer_req = ufs_sprd_setup_xfer_req,
	.apply_dev_quirks = ufs_sprd_apply_dev_quirks,
	.suspend = ufs_sprd_suspend,
	.resume = ufs_sprd_resume,
	.dbg_register_dump = ufs_sprd_dbg_register_dump,
	.device_reset = ufs_sprd_device_reset,
};

static void ufs_sprd_vh_prepare_command(void *data, struct ufs_hba *hba,
					struct request *rq,
					struct ufshcd_lrb *lrbp,
					int *err)
{
	struct ufs_sprd_host *host = ufshcd_get_variant(hba);

	if (unlikely(host->ffu_is_process == TRUE))
		prepare_command_send_in_ffu_state(hba, lrbp, err);

	return;
}

/*
 * ufs_sprd_probe - probe routine of the driver
 * @pdev: pointer to Platform device handle
 *
 * Return zero for success and non-zero for failure
 */
static int ufs_sprd_probe(struct platform_device *pdev)
{
	int err;
	struct device *dev = &pdev->dev;
	struct ufs_hba *hba;
	struct ufs_sprd_host *host = NULL;
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);

	register_trace_android_vh_ufs_prepare_command(ufs_sprd_vh_prepare_command, NULL);

	ufs_hba_sprd_vops.android_kabi_reserved1 =
					(u64)ufs_sprd_linkup_start_tstamp;
	ufs_hba_sprd_vops.android_kabi_reserved2 = (u64)ufs_sprd_dco_calibration;
	/* Perform generic probe */
	err = ufshcd_pltfrm_init(pdev, &ufs_hba_sprd_vops);
	if (err) {
		dev_err(dev, "ufshcd_pltfrm_init() failed %d\n", err);
		goto out;
	}

	hba = platform_get_drvdata(pdev);
	host = ufshcd_get_variant(hba);
	host->wlun_dev_add = false;

	/* Poll dev init complete flag to be true*/
	while (time_before(jiffies, timeout) && !host->wlun_dev_add)
		usleep_range(5000, 10000);

	if (!host->wlun_dev_add)
		dev_warn(hba->dev, "Dev init not complete!\n");
	ufs_sprd_rpmb_add(hba);
out:
	return err;
}
/*
 * ufs_sprd_remove - set driver_data of the device to NULL
 * @pdev: pointer to platform device handle
 *
 * Always returns 0
 */
static int ufs_sprd_remove(struct platform_device *pdev)
{
	struct ufs_hba *hba =  platform_get_drvdata(pdev);

	pm_runtime_get_sync(&(pdev)->dev);
	ufs_sprd_rpmb_remove(hba);
	ufshcd_remove(hba);
	return 0;
}
/*
 * ufs_sprd_shutdown - set driver_data of the device to NULL
 * @pdev: pointer to platform device handle
 *
 * Always returns 0
 */
static void ufs_sprd_shutdown(struct platform_device *pdev)
{
	struct ufs_hba *hba =  platform_get_drvdata(pdev);

	ufs_sprd_rpmb_remove(hba);
	ufshcd_pltfrm_shutdown(pdev);
}
static const struct of_device_id ufs_sprd_of_match[] = {
	{.compatible = "sprd,ufshc"},
	{},
};

static const struct dev_pm_ops ufs_sprd_pm_ops = {
	.suspend = ufshcd_pltfrm_suspend,
	.resume = ufshcd_pltfrm_resume,
	.runtime_suspend = ufshcd_pltfrm_runtime_suspend,
	.runtime_resume = ufshcd_pltfrm_runtime_resume,
	.runtime_idle = ufshcd_pltfrm_runtime_idle,
};

static struct platform_driver ufs_sprd_pltform = {
	.probe = ufs_sprd_probe,
	.remove = ufs_sprd_remove,
	.shutdown = ufs_sprd_shutdown,
	.driver = {
		.name = "ufshcd-sprd",
		.pm = &ufs_sprd_pm_ops,
		.of_match_table = of_match_ptr(ufs_sprd_of_match),
	},
};
module_platform_driver(ufs_sprd_pltform);

MODULE_DESCRIPTION("Unisoc Specific UFSHCI driver");
MODULE_LICENSE("GPL v2");
