/**
 * phy-sprd-usb31-qogirn6pro.c - Unisoc USB31 PHY Glue layer
 *
 * Copyright (c) 2021 Unisoc Co., Ltd.
 *		http://www.unisoc.com
 *
 * Author: Westbobo Zhou <westbobo.zhou@unisoc.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/kthread.h>
#include <linux/power/sprd-ump96xx-bc1p2.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/usb/phy.h>
#include <uapi/linux/sched/types.h>
#include <dt-bindings/soc/sprd,qogirn6pro-mask.h>
#include <dt-bindings/soc/sprd,qogirn6pro-regs.h>
#include <linux/usb/sprd_usbm.h>
#include "ptn38003a-i2c.h"

struct sprd_ssphy {
	struct usb_phy		phy;
	void __iomem		*base;
	struct regmap		*aon_apb;
	struct regmap		*ipa_apb;
	struct regmap		*ipa_dispc1_glb_apb;
	struct regmap		*ipa_usb31_dp;
	struct regmap		*ipa_usb31_dptx;
	struct regmap		*ana_g0l;
	struct regmap           *pmic;
	struct regulator	*vdd;
	struct kthread_worker bc1p2_kworker;
	struct kthread_work bc1p2_kwork;
	struct task_struct *bc1p2_thread;
	u32			vdd_vol;
	u32			host_eye_pattern;
	u32			device_eye_pattern;
	u32			phy_tune2;
	u32			revision;
	atomic_t		reset;
	atomic_t		inited;
	atomic_t		susped;
	bool			is_host;
	spinlock_t		vbus_event_lock;
	bool vbus_events;
	struct mutex lock;
};

#define PHY_INIT_TIMEOUT 500

#define DISPC1_GLB_APB_EB		 (0x0)
#define DISPC1_GLB_APB_RST		 (0x4)

#define PHY0_SRAM_BYPASS		(0X48)
#define PHY_SRAM_INIT_CHECK_DONE	(0x74)
#define TYPEC_DISABLE_ACK		(0xc08)

#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_VBUSVALID       0x02000000
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_TESTCLK         0x01000000
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_TESTDATAIN      0xff0000
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_TESTADDR        0xf000
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_TESTDATAOUTSEL  0x800
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_TESTDATAOUT     0x380
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_BIST_MODE       0x7c
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_T2RCOMP         0x2
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_LPBK_END        0x1
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_DATABUS16_8     0x10000000
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_SUSPENDM        0x08000000
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_PORN            0x04000000
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_RESET           0x02000000
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_RXERROR         0x01000000
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_BYPASS_DRV_DP   0x00800000
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_BYPASS_DRV_DM   0x00400000
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_BYPASS_FS       0x00200000
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_BYPASS_IN_DP    0x00100000
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_BYPASS_IN_DM    0x80000
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_BYPASS_OUT_DP   0x40000
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_BYPASS_OUT_DM   0x20000
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_VBUSVLDEXT      0x10000
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_RESERVED        0x0000FFFF
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_REXTENABLE      0x4
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_DMPULLUP        0x2
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_SAMPLER_SEL     0x1
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_DPPULLDOWN      0x10
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_DMPULLDOWN      0x8
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_TXBITSTUFFENABLE 0x4
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_TXBITSTUFFENABLEH 0x2
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_SLEEPM          0x1
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_TUNEHSAMP       0x06000000
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_TFREGRES        0x01f80000
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_TFHSRES         0x7c000
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_TUNERISE        0x3000
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_TUNEOTG         0xe00
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_TUNEDSC         0x180
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_TUNESQ          0x78
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_TUNEEQ          0x7
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_TUNEPLLS        0x7800
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_PLL_PFD_DEADZONE 0x300
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_PLL_PFD_DELAY   0xc0
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_PLL_CP_IOFFSET_EN 0x20
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_PLL_CP_IOFFSET  0x1e
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_PLL_REF_DOUBLER_EN 0x1
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_BISTRAM_EN      0x2
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_BIST_MODE_EN    0x1
#define BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_ISO_SW_EN       0x1
#define BIT_ANLG_PHY_G0L_DBG_SEL_ANALOG_USB20_USB20_SUSPENDM 0x100
#define BIT_ANLG_PHY_G0L_DBG_SEL_ANALOG_USB20_USB20_PORN    0x80
#define BIT_ANLG_PHY_G0L_DBG_SEL_ANALOG_USB20_USB20_RESET   0x40
#define BIT_ANLG_PHY_G0L_DBG_SEL_ANALOG_USB20_USB20_BYPASS_FS 0x20
#define BIT_ANLG_PHY_G0L_DBG_SEL_ANALOG_USB20_USB20_BYPASS_IN_DM 0x10
#define BIT_ANLG_PHY_G0L_DBG_SEL_ANALOG_USB20_USB20_DPPULLDOWN 0x8
#define BIT_ANLG_PHY_G0L_DBG_SEL_ANALOG_USB20_USB20_DMPULLDOWN 0x4
#define BIT_ANLG_PHY_G0L_DBG_SEL_ANALOG_USB20_USB20_SLEEPM 0x2
#define BIT_ANLG_PHY_G0L_DBG_SEL_ANALOG_USB20_USB20_ISO_SW_EN 0x1

#define REG_ANLG_PHY_G0L_ANALOG_USB20_USB20_TEST_PIN        0x0000
#define REG_ANLG_PHY_G0L_ANALOG_USB20_USB20_UTMI_CTL1       0x0004
#define REG_ANLG_PHY_G0L_ANALOG_USB20_USB20_BATTER_PLL      0x0008
#define REG_ANLG_PHY_G0L_ANALOG_USB20_USB20_UTMI_CTL2       0x000C
#define REG_ANLG_PHY_G0L_ANALOG_USB20_USB20_TRIMMING        0x0010
#define REG_ANLG_PHY_G0L_ANALOG_USB20_USB20_PLL_CTRL        0x0014
#define REG_ANLG_PHY_G0L_ANALOG_USB20_USB20_PHY_BIST_TEST   0x0018
#define REG_ANLG_PHY_G0L_ANALOG_USB20_USB20_PHY             0x001C
#define REG_ANLG_PHY_G0L_ANALOG_USB20_REG_SEL_CFG_0         0x0020

#define DEFAULT_DEVICE_EYE_PATTERN			0x067bd1c0
#define DEFAULT_HOST_EYE_PATTERN			0x067bd1c0

/* Rest USB Core*/
static inline void sprd_ssphy_reset_core(struct sprd_ssphy *phy)
{
	u32 reg, msk;
	int ret = 0;

	dev_dbg(phy->phy.dev, "%s ipa usb&phy rst!\n", __func__);

	/* Purpose: To soft-reset USB control */
	msk = MASK_IPA_APB_USB_SOFT_RST | MASK_IPA_APB_PAM_U3_SOFT_RST;
	reg = msk;
	regmap_update_bits(phy->ipa_apb, REG_IPA_APB_IPA_RST, msk, reg);

	/* Reset USB2 PHY */
	if (!sprd_usbm_hsphy_get_onoff()) {
		msk = MASK_AON_APB_OTG_PHY_SOFT_RST | MASK_AON_APB_OTG_UTMI_SOFT_RST;
		reg = msk;
		regmap_update_bits(phy->aon_apb, REG_AON_APB_APB_RST1, msk, reg);
	}

	/* Reset USB31 PHY */
	ret |= regmap_read(phy->ipa_dispc1_glb_apb, DISPC1_GLB_APB_RST, &reg);
	reg |= BIT(4);
	ret |= regmap_write(phy->ipa_dispc1_glb_apb, DISPC1_GLB_APB_RST, reg);
	usleep_range(1, 10);
	/* ssphy power on @0x64900d14*/
	msk = MASK_AON_APB_PHY_TEST_POWERDOWN;
	regmap_update_bits(phy->aon_apb, REG_AON_APB_USB31DPCOMBPHY_CTRL, msk, 0);

	/*
	 *Reset signal should hold on for a while
	 *to issue resret process reliable.
	 */
	usleep_range(20000, 30000);
	msk = MASK_IPA_APB_USB_SOFT_RST | MASK_IPA_APB_PAM_U3_SOFT_RST;
	regmap_update_bits(phy->ipa_apb, REG_IPA_APB_IPA_RST, msk, 0);
	msk = MASK_AON_APB_OTG_PHY_SOFT_RST | MASK_AON_APB_OTG_UTMI_SOFT_RST;
	regmap_update_bits(phy->aon_apb, REG_AON_APB_APB_RST1, msk, 0);
	ret |= regmap_read(phy->ipa_dispc1_glb_apb, DISPC1_GLB_APB_RST, &reg);
	reg &= ~BIT(4);
	ret |= regmap_write(phy->ipa_dispc1_glb_apb, DISPC1_GLB_APB_RST, reg);
}

static int sprd_ssphy_set_vbus(struct usb_phy *x, int on)
{
	struct sprd_ssphy *phy = container_of(x, struct sprd_ssphy, phy);
	u32 reg, msk;
	int ret = 0;

	if (on) {
		regmap_write(phy->ana_g0l, REG_ANLG_PHY_G0L_ANALOG_USB20_USB20_TRIMMING,
					phy->host_eye_pattern);
		/* set USB connector type is A-type*/
		msk = MASK_AON_APB_USB2_PHY_IDDIG;
		ret |= regmap_update_bits(phy->aon_apb,
			REG_AON_APB_OTG_PHY_CTRL, msk, 0);

		msk = BIT_ANLG_PHY_G0L_DBG_SEL_ANALOG_USB20_USB20_DMPULLDOWN |
			BIT_ANLG_PHY_G0L_DBG_SEL_ANALOG_USB20_USB20_DPPULLDOWN;
		ret |= regmap_update_bits(phy->ana_g0l,
			REG_ANLG_PHY_G0L_ANALOG_USB20_REG_SEL_CFG_0,
			msk, msk);

		/* the pull down resistance on D-/D+ enable */
		msk = BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_DMPULLDOWN |
			BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_DPPULLDOWN;
		ret |= regmap_update_bits(phy->ana_g0l,
			REG_ANLG_PHY_G0L_ANALOG_USB20_USB20_UTMI_CTL2,
			msk, msk);

		reg = 0x200;
		msk = BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_RESERVED;
		ret |= regmap_update_bits(phy->ana_g0l,
			REG_ANLG_PHY_G0L_ANALOG_USB20_USB20_UTMI_CTL1,
			msk, reg);
		phy->is_host = true;
	} else {
		regmap_write(phy->ana_g0l, REG_ANLG_PHY_G0L_ANALOG_USB20_USB20_TRIMMING,
					phy->device_eye_pattern);

		if (!sprd_usbm_hsphy_get_onoff()) {
			reg = msk = MASK_AON_APB_USB2_PHY_IDDIG;
			ret |= regmap_update_bits(phy->aon_apb,
				REG_AON_APB_OTG_PHY_CTRL, msk, reg);

			msk = BIT_ANLG_PHY_G0L_DBG_SEL_ANALOG_USB20_USB20_DMPULLDOWN |
				BIT_ANLG_PHY_G0L_DBG_SEL_ANALOG_USB20_USB20_DPPULLDOWN;
			ret |= regmap_update_bits(phy->ana_g0l,
				REG_ANLG_PHY_G0L_ANALOG_USB20_REG_SEL_CFG_0,
				msk, msk);

			/* the pull down resistance on D-/D+ enable */
			msk = BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_DMPULLDOWN |
				BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_DPPULLDOWN;
			ret |= regmap_update_bits(phy->ana_g0l,
				REG_ANLG_PHY_G0L_ANALOG_USB20_USB20_UTMI_CTL2,
				msk, 0);

			msk = BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_RESERVED;
			ret |= regmap_update_bits(phy->ana_g0l,
				REG_ANLG_PHY_G0L_ANALOG_USB20_USB20_UTMI_CTL1,
				msk, 0);
		}
		phy->is_host = false;
	}

	return ret;
}

/* Requirement from usb analog compliance tests */
static void usb_config_4_cts(struct sprd_ssphy *phy)
{
	u32	reg;

	dev_dbg(phy->phy.dev, "%s enter\n", __func__);

	regmap_read(phy->ipa_usb31_dp, 0x20, &reg);
	/*enable cr apb*/
	reg |= BIT(4);
	regmap_write(phy->ipa_usb31_dp, 0x20, reg);
	/*enable cr clk*/
	reg |= BIT(3);
	regmap_write(phy->ipa_usb31_dp, 0x20, reg);

	/*phy reg addr 0x0021 SUP_DIG_LVL_OVER_IN*/
	reg = 0x10021;
	regmap_write(phy->ipa_usb31_dp, 0x8, reg);
	regmap_read(phy->ipa_usb31_dp, 0xc, &reg);
	/*bit7: TX_VBOOST_LVL_EN; bit6L4=5 TX_TXVBOOST_LVL*/
	reg &= ~(BIT(6) | BIT(5) | BIT(4));
	reg |= BIT(7) | BIT(6) | BIT(4);
	regmap_write(phy->ipa_usb31_dp, 0xc, reg);

	/*cfg for enter gen2*/
	reg = 0x10063;
	regmap_write(phy->ipa_usb31_dp, 0x8, reg);
	regmap_read(phy->ipa_usb31_dp, 0xc, &reg);
	reg |= BIT(8) | BIT(7) | BIT(1) | BIT(0);
	regmap_write(phy->ipa_usb31_dp, 0xc, reg);
}

static int sprd_ssphy_init(struct usb_phy *x)
{
	struct sprd_ssphy *phy = container_of(x, struct sprd_ssphy, phy);
	u32	reg, msk;
	int	ret = 0;
	int timeout;

	if (atomic_read(&phy->inited)) {
		dev_info(x->dev, "%s is already inited!\n", __func__);
		return 0;
	}

	ptn38003a_mode_usb32_set(1);

	/*
	 * Due to chip design, some chips may turn on vddusb by default,
	 * We MUST avoid turning it on twice.
	 */
	if (phy->vdd) {
		ret = regulator_enable(phy->vdd);
		if (ret) {
			dev_err(x->dev,
				"Failed to enable phy->vdd: %d\n", ret);
			return ret;
		}
	}

	sprd_usbm_ssphy_set_onoff(1);

	/* select the IPA_SYS USB controller */
	msk = MASK_AON_APB_USB20_CTRL_MUX_REG;
	reg = msk;
	ret |= regmap_update_bits(phy->aon_apb, REG_AON_APB_AON_SOC_USB_CTRL,
			 msk, reg);

	/* enable analog */
	msk = MASK_AON_APB_CGM_OTG_REF_EN | MASK_AON_APB_CGM_DPHY_REF_EN;
	reg = msk;
	ret |= regmap_update_bits(phy->aon_apb, REG_AON_APB_CGM_REG1, msk, reg);

	/*enable analog:0x64900004*/
	reg = MASK_AON_APB_AON_USB2_TOP_EB | MASK_AON_APB_OTG_PHY_EB |
						MASK_AON_APB_ANA_EB;
	msk = reg;
	ret |= regmap_update_bits(phy->aon_apb, REG_AON_APB_APB_EB1, msk, reg);

	/* utmisrp_bvalid  sys vbus valid:0x64900D14*/
	reg = MASK_AON_APB_SYS_VBUSVALID;
	msk = reg;
	ret |= regmap_update_bits(phy->aon_apb, REG_AON_APB_USB31DPCOMBPHY_CTRL, msk, reg);

	/*  usb eb and usb ref eb :0x25000004*/
	ret |= regmap_read(phy->ipa_apb, REG_IPA_APB_IPA_EB, &reg);
	reg |= MASK_IPA_APB_USB_EB | MASK_IPA_APB_USB_REF_EB;
	ret |= regmap_write(phy->ipa_apb, REG_IPA_APB_IPA_EB, reg);

	/* usb suspend eb :0x64900138*/
	reg = MASK_AON_APB_CGM_USB_SUSPEND_EN;
	msk = reg;
	ret |= regmap_update_bits(phy->aon_apb, REG_AON_APB_CGM_REG1, msk, reg);

	/*
	 * USB2 PHY power on: set pd_l/pd_s firstly, then set iso_sw.
	 * poweroff sequence is opposite
	 */
	msk = MASK_AON_APB_LVDSRF_PD_PD_L | MASK_AON_APB_LVDSRF_PS_PD_S;
	regmap_update_bits(phy->aon_apb, REG_AON_APB_MIPI_CSI_POWER_CTRL, msk, 0);

	ret |= regmap_update_bits(phy->ana_g0l,
		REG_ANLG_PHY_G0L_ANALOG_USB20_USB20_PHY,
		BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_ISO_SW_EN, 0);

	/* enable usb20 ISO AVDD1V8_USB*/
	msk = MASK_AON_APB_USB20_ISO_SW_EN;
	ret |= regmap_update_bits(phy->aon_apb, REG_AON_APB_AON_SOC_USB_CTRL,
			 msk, 0);

	/* ssphy power on @0x64900d14*/
	msk = MASK_AON_APB_PHY_TEST_POWERDOWN;
	regmap_update_bits(phy->aon_apb, REG_AON_APB_USB31DPCOMBPHY_CTRL, msk, 0);

	/*hsphy vbus valid */
	msk = MASK_AON_APB_OTG_VBUS_VALID_PHYREG;
	reg = msk;
	regmap_update_bits(phy->aon_apb, REG_AON_APB_OTG_PHY_TEST, msk, reg);
	/*ssphy vbus valid */
	msk = MASK_AON_APB_SYS_VBUSVALID;
	reg = msk;
	regmap_update_bits(phy->aon_apb, REG_AON_APB_USB31DPCOMBPHY_CTRL, msk, reg);

	msk = BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_VBUSVLDEXT;
	reg = msk;
	ret |= regmap_update_bits(phy->ana_g0l,
			REG_ANLG_PHY_G0L_ANALOG_USB20_USB20_UTMI_CTL1,	msk, reg);

	regmap_write(phy->ana_g0l, REG_ANLG_PHY_G0L_ANALOG_USB20_USB20_TRIMMING,
					phy->device_eye_pattern);

	/* Reset PHY */
	sprd_ssphy_reset_core(phy);

	/* wait phy_sram init down check bit2=1 */
	timeout = PHY_INIT_TIMEOUT;
	while (1) {
		ret |= regmap_read(phy->ipa_usb31_dp, PHY_SRAM_INIT_CHECK_DONE, &reg);
		if ((reg & BIT(2)) == BIT(2)) {
			break;
		} else {
			msleep(1);
			if (!--timeout) {
				dev_err(x->dev, "%s phy sram init failure\n", __func__);
				break;
			}
		}
	}

	usb_config_4_cts(phy);

	/* REG_TYPEC_CTRL: TYPEC_DISABLE_ACK */
	ret |= regmap_read(phy->ipa_usb31_dptx, TYPEC_DISABLE_ACK, &reg);
	reg |= BIT(0);
	ret |= regmap_write(phy->ipa_usb31_dptx, TYPEC_DISABLE_ACK, reg);

	/* TCA GCFG
	bit1:0 => 2b01 op_mode = controller synced mode
	bit3:2 => 2b00 reserved
	bit4 = 1 => USB device mode
	Bit31:5 => 0x0
	*/
	reg = readl_relaxed(phy->base + 0x010);
	reg &= ~(0xffffffc0);
	writel_relaxed(reg, phy->base + 0x010);
	reg &= ~(BIT(0) | BIT(4));
	if (phy->is_host)
		reg |= BIT(0);
	else
		reg |= BIT(0) | BIT(4);
	writel_relaxed(reg, phy->base + 0x010);

	/* TCA CTRLSYNCMODE CFG0 */
	reg = readl_relaxed(phy->base + 0x020);
	reg |= BIT(1) | BIT(2);
	writel_relaxed(reg, phy->base + 0x020);

	/*  TCA CTRLSYNCMODE CFG1 */
	reg = readl_relaxed(phy->base + 0x024);
	reg = 0x3d090;
	writel_relaxed(reg, phy->base + 0x024);

	/* clear TCA int status */
	reg = readl_relaxed(phy->base + 0x08);
	reg = 0xffff;
	writel_relaxed(reg, phy->base + 0x08);

	/* TCA INT EN
	xa_ack_event_en =1
	xa_timeout_event_en=1
	*/
	reg = readl_relaxed(phy->base + 0x04);
	reg |= BIT(0) | BIT(1);
	writel_relaxed(reg, phy->base + 0x04);

	/* usb3 switch port */
	/* remove usbonly,add combophy for dp/usb */
	ret |= regmap_read(phy->aon_apb, REG_AON_APB_BOOT_MODE, &reg);
	msk = readl_relaxed(phy->base + 0x14);
	if ((reg & BIT(10))) {
		msk &= ~(BIT(2) | BIT(3));
		msk |= BIT(0) | BIT(1) | BIT(4);
	} else {
		msk &= ~BIT(3);
		msk |= BIT(0) | BIT(1) | BIT(2) | BIT(4);
	}
	writel_relaxed(msk, phy->base + 0x14);

	msk = readl_relaxed(phy->base + 0x18);
	if (reg & BIT(10))
		msk &= ~BIT(2);
	else
		msk |= BIT(2);
	writel_relaxed(msk, phy->base + 0x18);

	msleep(10);
	/* wait tca interrupt */
	timeout = PHY_INIT_TIMEOUT;
	while (1) {
		reg = readl_relaxed(phy->base + 0x08);
		if ((reg & BIT(0)) == BIT(0) || (reg & BIT(1)) == BIT(1)) {
			msk = 0xffff;
			writel_relaxed(msk, phy->base + 0x08);
			break;
		} else {
			msleep(1);
			if (!--timeout) {
				dev_err(x->dev, "%s tca interrupt not ready\n", __func__);
				break;
			}
		}
	}

	if (!phy->pmic) {
		/*
		 *In FPGA platform, Disable low power will take some time
		 *before the DWC3 Core register is accessible.
		 */
		usleep_range(1000, 2000);
	}

	atomic_set(&phy->inited, 1);

	return ret;
}

/* Turn off PHY and core */
static void sprd_ssphy_shutdown(struct usb_phy *x)
{
	struct sprd_ssphy *phy = container_of(x, struct sprd_ssphy, phy);
	u32 msk = 0, reg = 0;

	if (!atomic_read(&phy->inited)) {
		dev_dbg(x->dev, "%s is already shut down\n", __func__);
		return;
	}

	dev_info(x->dev, "[%s]enter usbm_event_is_active(%d), usbm_hsphy_get_onoff(%d)\n",
		__func__, sprd_usbm_event_is_active(), sprd_usbm_hsphy_get_onoff());

	sprd_usbm_ssphy_set_onoff(0);
	if (!sprd_usbm_hsphy_get_onoff()) {
		msk = BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_ISO_SW_EN;
		reg = msk;
		regmap_update_bits(phy->ana_g0l, REG_ANLG_PHY_G0L_ANALOG_USB20_USB20_PHY,
			msk, reg);

		/*hsphy vbus invalid */
		msk = MASK_AON_APB_OTG_VBUS_VALID_PHYREG;
		regmap_update_bits(phy->aon_apb, REG_AON_APB_OTG_PHY_TEST, msk, 0);

		/* disable usb20 ISO*/
		msk = MASK_AON_APB_USB20_ISO_SW_EN;
		reg = msk;
		regmap_update_bits(phy->aon_apb, REG_AON_APB_AON_SOC_USB_CTRL,
				 msk, reg);

		/* hsphy power off */
		msk = MASK_AON_APB_LVDSRF_PD_PD_L | MASK_AON_APB_LVDSRF_PS_PD_S;
		reg = msk;
		regmap_update_bits(phy->aon_apb, REG_AON_APB_MIPI_CSI_POWER_CTRL, msk, reg);

		/* disable usb cgm ref */
		msk = MASK_AON_APB_CGM_OTG_REF_EN | MASK_AON_APB_CGM_DPHY_REF_EN;
		regmap_update_bits(phy->aon_apb, REG_AON_APB_CGM_REG1, msk, 0);

		/*disable analog:0x64900004*/
		msk = MASK_AON_APB_AON_USB2_TOP_EB | MASK_AON_APB_OTG_PHY_EB;
		regmap_update_bits(phy->aon_apb, REG_AON_APB_APB_EB1, msk, 0);
	}

	/*ssphy vbus invalid */
	msk = MASK_AON_APB_SYS_VBUSVALID;
	regmap_update_bits(phy->aon_apb, REG_AON_APB_USB31DPCOMBPHY_CTRL, msk, 0);

	/* Reset USB31 PHY */
	regmap_read(phy->ipa_dispc1_glb_apb, DISPC1_GLB_APB_RST, &reg);
	reg |= BIT(4);
	regmap_write(phy->ipa_dispc1_glb_apb, DISPC1_GLB_APB_RST, reg);

	usleep_range(1, 10);
	/* ssphy power off @0x64900d14*/
	msk = MASK_AON_APB_PHY_TEST_POWERDOWN;
	reg = msk;
	regmap_update_bits(phy->aon_apb, REG_AON_APB_USB31DPCOMBPHY_CTRL, msk, reg);

	/*
	 * Due to chip design, some chips may turn on vddusb by default,
	 * we MUST avoid turning it off twice.
	 */

	if (regulator_is_enabled(phy->vdd))
		regulator_disable(phy->vdd);

	ptn38003a_mode_usb32_set(0);

	atomic_set(&phy->inited, 0);
	atomic_set(&phy->reset, 0);
}

static ssize_t hsphy_device_eye_pattern_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct usb_phy *x = dev_get_drvdata(dev);
	struct sprd_ssphy *phy;

	if (!x)
		return -EINVAL;

	phy = container_of(x, struct sprd_ssphy, phy);

	return sprintf(buf, "0x%x\n", phy->device_eye_pattern);
}

static ssize_t hsphy_device_eye_pattern_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t size)
{
	struct usb_phy *x = dev_get_drvdata(dev);
	struct sprd_ssphy *phy;

	if (!x)
		return -EINVAL;

	phy = container_of(x, struct sprd_ssphy, phy);
	if (kstrtouint(buf, 16, &phy->device_eye_pattern) < 0)
		return -EINVAL;

	return size;
}
static DEVICE_ATTR_RW(hsphy_device_eye_pattern);

static ssize_t hsphy_host_eye_pattern_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct usb_phy *x = dev_get_drvdata(dev);
	struct sprd_ssphy *phy;

	if (!x)
		return -EINVAL;

	phy = container_of(x, struct sprd_ssphy, phy);

	return sprintf(buf, "0x%x\n", phy->host_eye_pattern);
}

static ssize_t hsphy_host_eye_pattern_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t size)
{
	struct usb_phy *x = dev_get_drvdata(dev);
	struct sprd_ssphy *phy;

	if (!x)
		return -EINVAL;

	phy = container_of(x, struct sprd_ssphy, phy);
	if (kstrtouint(buf, 16, &phy->host_eye_pattern) < 0)
		return -EINVAL;

	return size;
}
static DEVICE_ATTR_RW(hsphy_host_eye_pattern);

static ssize_t vdd_voltage_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct usb_phy *x = dev_get_drvdata(dev);
	struct sprd_ssphy *phy;

	if (!x)
		return -EINVAL;

	phy = container_of(x, struct sprd_ssphy, phy);

	return sprintf(buf, "%d\n", phy->vdd_vol);
}

static ssize_t vdd_voltage_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t size)
{
	struct usb_phy *x = dev_get_drvdata(dev);
	struct sprd_ssphy *phy;
	u32 vol;

	if (!x)
		return -EINVAL;

	phy = container_of(x, struct sprd_ssphy, phy);
	if (kstrtouint(buf, 16, &vol) < 0)
		return -EINVAL;

	if (vol < 1200000 || vol > 3750000)
		return -EINVAL;

	phy->vdd_vol = vol;

	return size;
}
static DEVICE_ATTR_RW(vdd_voltage);

static struct attribute *usb_ssphy_attrs[] = {
	&dev_attr_hsphy_device_eye_pattern.attr,
	&dev_attr_hsphy_host_eye_pattern.attr,
	&dev_attr_vdd_voltage.attr,
	NULL
};
ATTRIBUTE_GROUPS(usb_ssphy);

static void sprd_ssphy_get_bc1p2_type_work(struct kthread_work *work)
{
	struct sprd_ssphy *phy = container_of(work, struct sprd_ssphy, bc1p2_kwork);
	struct usb_phy *usb_phy;
	bool vbus_events;

	if (!phy) {
		pr_err("%s:line%d: phy NULL pointer!!!\n", __func__, __LINE__);
		return;
	}

	usb_phy = &phy->phy;
	if (!usb_phy) {
		pr_err("%s:line%d: usb_phy NULL pointer!!!\n", __func__, __LINE__);
		return;
	}

	mutex_lock(&phy->lock);
	spin_lock(&phy->vbus_event_lock);
	while (phy->vbus_events) {
		vbus_events = phy->vbus_events;
		phy->vbus_events = false;
		spin_unlock(&phy->vbus_event_lock);
		if (vbus_events)
			sprd_bc1p2_notify_charger(usb_phy);
		spin_lock(&phy->vbus_event_lock);
	}

	spin_unlock(&phy->vbus_event_lock);
	mutex_unlock(&phy->lock);
}

static void sprd_ssphy_usb_changed(struct sprd_ssphy *phy, enum usb_charger_state state)
{
	struct usb_phy *usb_phy = &phy->phy;

	spin_lock(&phy->vbus_event_lock);
	phy->vbus_events = true;

	usb_phy->chg_state = state;

	spin_unlock(&phy->vbus_event_lock);

	if (phy->bc1p2_thread)
		kthread_queue_work(&phy->bc1p2_kworker, &phy->bc1p2_kwork);
}

static int sprd_ssphy_vbus_notify(struct notifier_block *nb,
				unsigned long event, void *data)
{
	struct usb_phy *usb_phy = container_of(nb, struct usb_phy, vbus_nb);
	struct sprd_ssphy *phy = container_of(usb_phy, struct sprd_ssphy, phy);

	if (sprd_usbm_event_is_active()) {
		dev_dbg(usb_phy->dev, "%s is_active\n", __func__);
		return 0;
	}

	if (phy->is_host) {
		dev_info(usb_phy->dev, "USB PHY is host mode\n");
		return 0;
	}

	if (event)
		sprd_ssphy_usb_changed(phy, USB_CHARGER_PRESENT);
	else
		sprd_ssphy_usb_changed(phy, USB_CHARGER_ABSENT);

	return 0;
}

static enum usb_charger_type sprd_ssphy_charger_detect(struct usb_phy *x)
{
	enum usb_charger_type type = UNKNOWN_TYPE;

	type = sprd_bc1p2_charger_detect(x);

	return type;
}

static int sprd_ssphy_notify_connect(struct usb_phy *x,
				enum usb_device_speed speed)
{
	struct sprd_ssphy *phy = container_of(x, struct sprd_ssphy, phy);
	u32 msk = 0, reg = 0;

	if (!atomic_read(&phy->inited)) {
		dev_info(x->dev, "%s phy is not inited!\n", __func__);
		return 0;
	}

	if (phy->is_host)
		return 0;

	/*hsphy vbus valid */
	msk = MASK_AON_APB_OTG_VBUS_VALID_PHYREG;
	reg = msk;
	regmap_update_bits(phy->aon_apb, REG_AON_APB_OTG_PHY_TEST, msk, reg);
	/*ssphy vbus valid */
	msk = MASK_AON_APB_SYS_VBUSVALID;
	reg = msk;
	regmap_update_bits(phy->aon_apb, REG_AON_APB_USB31DPCOMBPHY_CTRL, msk, reg);

	msk = BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_VBUSVLDEXT;
	reg = msk;
	regmap_update_bits(phy->ana_g0l,
			REG_ANLG_PHY_G0L_ANALOG_USB20_USB20_UTMI_CTL1,	msk, reg);
	dev_info(x->dev, "ssphy set vbus valid!\n");
	return 0;
}

static int sprd_ssphy_notify_disconnect(struct usb_phy *x,
				enum usb_device_speed speed)
{
	struct sprd_ssphy *phy = container_of(x, struct sprd_ssphy, phy);
	u32 msk = 0;

	if (!atomic_read(&phy->inited)) {
		dev_info(x->dev, "%s phy is not inited!\n", __func__);
		return 0;
	}

	if (phy->is_host)
		return 0;

	/*hsphy vbus invalid */
	msk = MASK_AON_APB_OTG_VBUS_VALID_PHYREG;
	regmap_update_bits(phy->aon_apb, REG_AON_APB_OTG_PHY_TEST, msk, 0);
	/*ssphy vbus invalid */
	msk = MASK_AON_APB_SYS_VBUSVALID;
	regmap_update_bits(phy->aon_apb, REG_AON_APB_USB31DPCOMBPHY_CTRL, msk, 0);

	msk = BIT_ANLG_PHY_G0L_ANALOG_USB20_USB20_VBUSVLDEXT;
	regmap_update_bits(phy->ana_g0l,
			REG_ANLG_PHY_G0L_ANALOG_USB20_USB20_UTMI_CTL1,	msk, 0);
	dev_info(x->dev, "ssphy set vbus invalid!\n");
	return 0;
}

static int sprd_ssphy_probe(struct platform_device *pdev)
{
	struct device_node *regmap_np;
	struct platform_device *regmap_pdev;
	struct sprd_ssphy *phy;
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct sched_param param = { .sched_priority = 1 };
	int ret;
	u32 reg, msk;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	regmap_np = of_find_compatible_node(NULL, NULL, "sprd,ump962x-syscon");
	if (!regmap_np) {
		dev_warn(dev, "unable to get syscon node\n");
	} else {
		regmap_pdev = of_find_device_by_node(regmap_np);
		if (!regmap_pdev) {
			of_node_put(regmap_np);
			dev_warn(dev, "unable to get syscon platform device\n");
			phy->pmic = NULL;
		} else {
			phy->pmic = dev_get_regmap(regmap_pdev->dev.parent, NULL);
			if (!phy->pmic)
				dev_warn(dev, "unable to get pmic regmap device\n");
		}
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "phy_glb_regs");
	if (!res) {
		dev_err(dev, "missing USB PHY registers resource\n");
		return -ENODEV;
	}

	phy->base = devm_ioremap_nocache(dev, res->start, resource_size(res));
	if (IS_ERR(phy->base))
		return PTR_ERR(phy->base);

	phy->aon_apb = syscon_regmap_lookup_by_phandle(dev->of_node,
						       "sprd,syscon-aon-apb");
	if (IS_ERR(phy->aon_apb)) {
		dev_err(dev, "failed to map aon registers (via syscon)\n");
		return PTR_ERR(phy->aon_apb);
	}


	phy->ana_g0l = syscon_regmap_lookup_by_phandle(dev->of_node,
						       "sprd,syscon-ana-g0l");
	if (IS_ERR(phy->ana_g0l)) {
		dev_err(dev, "failed to map aon registers (via syscon)\n");
		return PTR_ERR(phy->ana_g0l);
	}

	phy->ipa_apb = syscon_regmap_lookup_by_phandle(dev->of_node,
						       "sprd,syscon-ipa-apb");
	if (IS_ERR(phy->ipa_apb)) {
		dev_err(dev, "failed to map ipa apb registers (via syscon)\n");
		return PTR_ERR(phy->ipa_apb);
	}

	phy->ipa_dispc1_glb_apb = syscon_regmap_lookup_by_phandle(dev->of_node,
						"sprd,syscon-ipa-dispc1-glb-apb");
	if (IS_ERR(phy->ipa_dispc1_glb_apb)) {
		dev_err(dev, "failed to map ipa glb apb registers (via syscon)\n");
		return PTR_ERR(phy->ipa_dispc1_glb_apb);
	}

	phy->ipa_usb31_dp = syscon_regmap_lookup_by_phandle(dev->of_node,
						 "sprd,syscon-ipa-usb31-dp");
	if (IS_ERR(phy->ipa_usb31_dp)) {
		dev_err(&pdev->dev, "ipa usb31 dp syscon failed!\n");
		return PTR_ERR(phy->ipa_usb31_dp);
	}

	phy->ipa_usb31_dptx = syscon_regmap_lookup_by_phandle(dev->of_node,
						 "sprd,syscon-ipa-usb31-dptx");
	if (IS_ERR(phy->ipa_usb31_dptx)) {
		dev_err(&pdev->dev, "ipa usb31 dptx syscon failed!\n");
		return PTR_ERR(phy->ipa_usb31_dptx);
	}

	ret = of_property_read_u32(dev->of_node, "sprd,vdd-voltage",
				   &phy->vdd_vol);
	if (ret < 0) {
		dev_warn(dev, "unable to read ssphy vdd voltage\n");
		phy->vdd_vol = 3300;
	}

	phy->vdd = devm_regulator_get_optional(dev, "vdd");
	if (IS_ERR(phy->vdd)) {
		dev_warn(dev, "unable to get ssphy vdd supply\n");
		phy->vdd = NULL;
	} else {
		ret = regulator_set_voltage(phy->vdd, phy->vdd_vol,
				 phy->vdd_vol);
		if (ret < 0) {
			dev_warn(dev, "fail to set ssphy vdd voltage:%dmV\n",
				phy->vdd_vol);
		}
	}

	ret = of_property_read_u32(dev->of_node, "sprd,hsphy-device-eye-pattern",
							&phy->device_eye_pattern);
	if (ret < 0) {
		dev_err(dev, "unable to get hsphy-device-eye-pattern node\n");
		phy->device_eye_pattern = DEFAULT_DEVICE_EYE_PATTERN;
	}

	ret = of_property_read_u32(dev->of_node, "sprd,hsphy-host-eye-pattern",
							&phy->host_eye_pattern);
	if (ret < 0) {
		dev_err(dev, "unable to get hsphy-host-eye-pattern node\n");
		phy->host_eye_pattern = DEFAULT_HOST_EYE_PATTERN;
	}

	if (!phy->pmic) {
		/*
		 * USB PHY must init before DWC3 phy setup in haps,
		 * otherwise dwc3 phy setting will be cleared
		 * because IPA_ATH_USB_RESET  reset dwc3 PHY setting.
		 */
		sprd_ssphy_init(&phy->phy);
	}

	/* select the IPA_SYS USB controller */
	reg = msk = MASK_AON_APB_USB20_CTRL_MUX_REG;
	ret |= regmap_update_bits(phy->aon_apb, REG_AON_APB_AON_SOC_USB_CTRL,
			 msk, reg);

	platform_set_drvdata(pdev, phy);
	phy->phy.dev				= dev;
	phy->phy.label				= "sprd-ssphy";
	phy->phy.init				= sprd_ssphy_init;
	phy->phy.shutdown			= sprd_ssphy_shutdown;
	phy->phy.set_vbus			= sprd_ssphy_set_vbus;
	phy->phy.type				= USB_PHY_TYPE_USB3;
	phy->phy.vbus_nb.notifier_call		= sprd_ssphy_vbus_notify;
	phy->phy.charger_detect			= sprd_ssphy_charger_detect;
	/*
	 * notify_connect is used to set vbusvalid
	 * notify_disconnect is used to set vbusinvalid
	 */
	phy->phy.notify_connect			= sprd_ssphy_notify_connect;
	phy->phy.notify_disconnect		= sprd_ssphy_notify_disconnect;
	mutex_init(&phy->lock);
	spin_lock_init(&phy->vbus_event_lock);
	kthread_init_worker(&phy->bc1p2_kworker);
	kthread_init_work(&phy->bc1p2_kwork, sprd_ssphy_get_bc1p2_type_work);
	phy->bc1p2_thread = kthread_run(kthread_worker_fn, &phy->bc1p2_kworker,
					"ssphy_bc1p2_worker");
	if (IS_ERR(phy->bc1p2_thread)) {
		phy->bc1p2_thread = NULL;
		dev_err(dev, "failed to run bc1p2_thread\n");
	} else {
		sched_setscheduler(phy->bc1p2_thread, SCHED_FIFO, &param);
	}

	ret = usb_add_phy_dev(&phy->phy);
	if (ret) {
		dev_err(dev, "fail to add phy\n");
		return ret;
	}

	ret = sysfs_create_groups(&dev->kobj, usb_ssphy_groups);
	if (ret)
		dev_warn(dev, "failed to create usb ssphy attributes\n");

	pm_runtime_enable(dev);

	if (extcon_get_state(phy->phy.edev, EXTCON_USB) > 0)
		sprd_ssphy_usb_changed(phy, USB_CHARGER_PRESENT);

	return 0;
}

static int sprd_ssphy_remove(struct platform_device *pdev)
{
	struct sprd_ssphy *phy = platform_get_drvdata(pdev);

	if (phy->bc1p2_thread) {
		kthread_flush_worker(&phy->bc1p2_kworker);
		kthread_stop(phy->bc1p2_thread);
		phy->bc1p2_thread = NULL;
	}
	sysfs_remove_groups(&pdev->dev.kobj, usb_ssphy_groups);
	usb_remove_phy(&phy->phy);
	if (regulator_is_enabled(phy->vdd))
		regulator_disable(phy->vdd);
	return 0;
}

static const struct of_device_id sprd_ssphy_match[] = {
	{ .compatible = "sprd,qogirn6pro-ssphy" },
	{},
};

MODULE_DEVICE_TABLE(of, sprd_ssphy_match);

static struct platform_driver sprd_ssphy_driver = {
	.probe		= sprd_ssphy_probe,
	.remove		= sprd_ssphy_remove,
	.driver		= {
		.name	= "sprd-ssphy",
		.of_match_table = sprd_ssphy_match,
	},
};

static int __init sprd_ssphy_driver_init(void)
{
	return platform_driver_register(&sprd_ssphy_driver);
}

static void __exit sprd_ssphy_driver_exit(void)
{
	platform_driver_unregister(&sprd_ssphy_driver);
}

late_initcall(sprd_ssphy_driver_init);
module_exit(sprd_ssphy_driver_exit);

MODULE_ALIAS("platform:sprd-ssphy	");
MODULE_AUTHOR("Westbobo Zhou <westbobo.zhou@unisoc.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DesignWare USB3 SPRD PHY Glue Layer");
