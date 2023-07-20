 /*
 * Copyright (C) 2015-2022 Unisoc Communications Inc.
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

#ifndef _UFS_SPRD_H_
#define _UFS_SPRD_H_
#include "ufshpb-sprd.h"

struct syscon_ufs {
	struct regmap *regmap;
	u32 reg;
	u32 mask;
};

struct ufs_sprd_host {
	struct ufs_hba *hba;
	struct scsi_device *sdev_ufs_rpmb;
	void __iomem *ufshci_reg;
	void __iomem *ufsutp_reg;
	void __iomem *unipro_reg;
	void __iomem *ufs_ao_reg;
	void __iomem *mphy_reg;
	void __iomem *rus;
	struct regulator *vdd_mphy;
	struct syscon_ufs phy_sram_ext_ld_done;
	struct syscon_ufs phy_sram_bypass;
	struct syscon_ufs phy_sram_init_done;
	struct syscon_ufs aon_apb_ufs_clk_en;
	struct syscon_ufs ufsdev_refclk_en;
	struct syscon_ufs usb31pllv_ref2mphy_en;

	struct clk *hclk_source;
	struct clk *rco_100M;
	struct clk *hclk;
	struct reset_control *ap_ahb_ufs_rst;
	struct reset_control *aon_apb_ufs_rst;
	uint32_t ufs_lane_calib_data0;
	uint32_t ufs_lane_calib_data1;
	bool wlun_dev_add;
	void __iomem *syssel_reg;
};

/*ufs hclk register*/
#define REG_HCLKDIV	0xFC

/* Set auto h8 ilde time to 10ms */
#define AUTO_H8_IDLE_TIME_10MS 0x1001

/* UFS host controller vendor specific registers */
#define REG_SW_RST	0xb0
#define HCI_RST		(1 << 12)
#define HCI_CLOD_RST	(1 << 28)

/* UFS unipro registers */
#define REG_PA_7	0x1c
#define REDESKEW_MASK	(1 << 21)

#define REG_PA_15		0x3c
#define RMMI_TX_L0_RST		(1 << 24)
#define RMMI_TX_L1_RST		(1 << 25)
#define RMMI_RX_L0_RST		(1 << 26)
#define RMMI_RX_L1_RST		(1 << 27)
#define RMMI_CB_RST		(1 << 28)
#define RMMI_RST		(1 << 29)

#define REG_PA_27		0x148
#define RMMI_TX_DIRDY_SEL	(1 << 0)

#define REG_DL_0	0x40
#define DL_RST		(1 << 0)

#define REG_N_1		0x84
#define N_RST		(1 << 1)

#define REG_T_9		0xc0
#define T_RST		(1 << 4)

#define REG_DME_0	0xd0
#define DME_RST		(1 << 2)

/* UFS utp registers */
#define REG_UTP_MISC	0x100
#define TX_RSTZ		(1 << 0)
#define RX_RSTZ		(1 << 1)

/* UFS ao registers */
#define REG_AO_SW_RST	0x1c
#define XTAL_RST	(1 << 1)

/* UFS mphy registers */
#define REG_DIG_CFG7		0x1c
#define CDR_MONITOR_BYPASS	(1 << 24)

#define REG_DIG_CFG35	0x8c
#define TX_FIFOMODE	(1 << 15)

/*
 * Synopsys common M-PHY Attributes
 */
#define CBRATESEL				0x8114
#define CBCREGADDRLSB				0x8116
#define CBCREGADDRMSB				0x8117
#define CBCREGWRLSB				0x8118
#define CBCREGWRMSB				0x8119
#define CBCREGRDWRSEL				0x811C
#define CBCRCTRL				0x811F
#define CBREFCLKCTRL2				0x8132

/*
 *Synopsys RX implementation specific M-PHY Attributes
 */
#define RXSQCONTROL				0x8009

#define VS_MPHYDISABLE		0xD0C1

#define UFSHCI_VERSION_30	0x00000300 /* 3.0 */

/* Define debug bus register */
#define REG_DEBUG_BUS_SYSSEL	0x7800A100

#endif/* _UFS_SPRD_H_ */
