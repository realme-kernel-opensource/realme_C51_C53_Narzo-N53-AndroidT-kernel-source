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

#ifndef _CSI_DRIVER_H_
#define _CSI_DRIVER_H_

#include <linux/of.h>
#include <linux/spinlock.h>

#define CSI_MAX_COUNT				1
#define REG_CPHY_TEST_CTRL	(0x404a0000)
#define BIT_CPHY_DBG_TEST_CLR	BIT(1)
#define REG_AON_APB_EB1		(0x0004)

#define BIT_AON_APB_FORCE_CSI_PHY_SHUTDOWNZ BIT(1)
#define BIT_AON_APB_MIPI_PHY_PS_PD_L	BIT(12)
#define BIT_AON_APB_MIPI_PHY_PS_PD_S	BIT(13)

#define REG_AON_APB_2P2L_PHY_CTRL	(0x0084)
#define BIT_AON_APB_4LANE_PHY_MODE	(0x0001)

#define REG_MM_AHB_EB	(0x0000)
#define BIT_MM_AHB_CSI_EB	BIT(3)

#define REG_MM_AHB_RST	(0x0004)
#define BIT_MM_AHB_CSI0_SOFT_RST	BIT(9)

#define BIT_MM_AHB_MIPI_CPHY_SEL	BIT(0)
#define BIT_AON_APB_MIPI_CSI_RESERVE0	BIT(0)

#define CSI_BASE(idx)			(s_csi_regbase[idx])

/* csi_register_t just for sharkl2 */
enum csi_registers_t {
	IP_REVISION = 0x00,
	LANE_NUMBER = 0x04,
	PHY_PD_N = 0x08,
	RST_DPHY_N = 0x0C,
	RST_CSI2_N = 0x10,
	MODE_CFG = 0x14,
	PHY_STATE = 0x18,
	ERR0 = 0x20,
	ERR1 = 0x24,
	MASK0 = 0x28,
	MASK1 = 0x2C,
	ERR0_CLR = 0x30,
	ERR1_CLR = 0x34,
	CAL_DONE = 0x38,
	CAL_FAILED = 0x3C,
	MSK_CAL_DONE = 0x40,
	MSK_CAL_FAILED = 0x44,
	PHY_TEST_CRTL0 = 0x48,
	PHY_TEST_CRTL1 = 0x4c,
	IPG_RAW10_CFG0 = 0x50,
	IPG_RAW10_CFG1 = 0x54,
	IPG_RAW10_CFG2 = 0x58,
	IPG_RAW10_CFG3 = 0x5C,
	IPG_YUV422_8_CFG0 = 0x60,
	IPG_YUV422_8_CFG1 = 0x64,
	IPG_YUV422_8_CFG2 = 0x68,
	IPG_OTHER_CFG0 = 0x70,
};

void csi_phy_power_down(struct csi_phy_info *phy, unsigned int sensor_id,
			int is_eb);
void csi_controller_enable(struct csi_dt_node_info *dt_info, int32_t idx);
void dphy_init(struct csi_phy_info *phy, int32_t idx);
void csi_set_on_lanes(uint8_t lanes, int32_t idx);
void csi_shut_down_phy(uint8_t shutdown, int32_t idx);
void csi_close(int32_t idx);
void csi_reset_controller(int32_t idx);
void csi_reset_phy(int32_t idx);
void csi_event_enable(int32_t idx);
int csi_ahb_reset(struct csi_phy_info *phy,
		  unsigned int csi_id);
void csi_reg_trace(unsigned int idx);
void csi_set_mode_size(uint32_t width, uint32_t height);
void csi_ipg_mode_cfg(uint32_t idx);

#define CSI_REG_WR(idx, reg, val)  (REG_WR(CSI_BASE(idx)+reg, val))
#define CSI_REG_RD(idx, reg)  (REG_RD(CSI_BASE(idx)+reg))
#define CSI_REG_MWR(idx, reg, msk, val)  CSI_REG_WR(idx, reg, \
	((val) & (msk)) | (CSI_REG_RD(idx, reg) & (~(msk))))
#define IPG_REG_MWR(reg, msk, val)  REG_WR(reg, \
		((val) & (msk)) | (REG_RD(reg) & (~(msk))))

#endif

