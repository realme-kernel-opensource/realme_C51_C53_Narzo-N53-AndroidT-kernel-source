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

#include "mm_dvfs.h"
#include "mmsys_dvfs_comm.h"
struct ip_dvfs_map_cfg  cpp_dvfs_config_table[8] = {
	{0, REG_MM_DVFS_AHB_CPP_INDEX0_MAP, VOLT70, CPP_CLK768,
		CPP_CLK_INDEX_768, 0, 0, 0, MTX_CLK_INDEX_768, "0.7v"},
	{1, REG_MM_DVFS_AHB_CPP_INDEX1_MAP, VOLT70, CPP_CLK1280,
		CPP_CLK_INDEX_1280, 0, 0, 0, MTX_CLK_INDEX_1280, "0.7v"},
	{2, REG_MM_DVFS_AHB_CPP_INDEX2_MAP, VOLT70, CPP_CLK2560,
		CPP_CLK_INDEX_2560, 0, 0, 0, MTX_CLK_INDEX_2560, "0.7v"},
	{3, REG_MM_DVFS_AHB_CPP_INDEX3_MAP, VOLT75, CPP_CLK3840,
		CPP_CLK_INDEX_3840, 0, 0, 0, MTX_CLK_INDEX_3072, "0.75v"},
	{4, REG_MM_DVFS_AHB_CPP_INDEX4_MAP, VOLT75, CPP_CLK3840,
		CPP_CLK_INDEX_3840, 0, 0, 0, MTX_CLK_INDEX_3840, "0.75v"},
	{5, REG_MM_DVFS_AHB_CPP_INDEX5_MAP, VOLT75, CPP_CLK3840,
		CPP_CLK_INDEX_3840, 0, 0, 0, MTX_CLK_INDEX_4680, "0.75v"},
	{6, REG_MM_DVFS_AHB_CPP_INDEX6_MAP, VOLT80, CPP_CLK3840,
		CPP_CLK_INDEX_3840, 0, 0, 0, MTX_CLK_INDEX_4680, "0.8v"},
	{7, REG_MM_DVFS_AHB_CPP_INDEX7_MAP, VOLT80, CPP_CLK3840,
		CPP_CLK_INDEX_3840, 0, 0, 0, MTX_CLK_INDEX_4680, "0.8v"},
};

struct ip_dvfs_map_cfg dcamaxi_dvfs_config_table[] = {
	{0, REG_MM_DVFS_AHB_DCAM_AXI_INDEX0_MAP, VOLT70,
		DCAMAXI_CLK2560, DCAMAXI_CLK_INDEX_2560, 0, 0, 0, 0, "0.7v"},
	{1, REG_MM_DVFS_AHB_DCAM_AXI_INDEX1_MAP, VOLT70,
		DCAMAXI_CLK3072, DCAMAXI_CLK_INDEX_3072, 0, 0, 0, 0, "0.7v"},
	{2, REG_MM_DVFS_AHB_DCAM_AXI_INDEX2_MAP, VOLT75,
		DCAMAXI_CLK3840, DCAMAXI_CLK_INDEX_3840, 0, 0, 0, 0, "0.75v"},
	{3, REG_MM_DVFS_AHB_DCAM_AXI_INDEX3_MAP, VOLT75,
		DCAMAXI_CLK4680, DCAMAXI_CLK_INDEX_4680, 0, 0, 0, 0, "0.75v"},
	{4, REG_MM_DVFS_AHB_DCAM_AXI_INDEX4_MAP, VOLT80,
		DCAMAXI_CLK4680, DCAMAXI_CLK_INDEX_4680, 0, 0, 0, 0, "0.8v"},
	{5, REG_MM_DVFS_AHB_DCAM_AXI_INDEX5_MAP, VOLT80,
		DCAMAXI_CLK4680, DCAMAXI_CLK_INDEX_4680, 0, 0, 0, 0, "0.8v"},
	{6, REG_MM_DVFS_AHB_DCAM_AXI_INDEX6_MAP, VOLT80,
		DCAMAXI_CLK4680, DCAMAXI_CLK_INDEX_4680, 0, 0, 0, 0, "0.8v"},
	{7, REG_MM_DVFS_AHB_DCAM_AXI_INDEX7_MAP, VOLT80,
		DCAMAXI_CLK4680, DCAMAXI_CLK_INDEX_4680, 0, 0, 0, 0, "0.8v"},
};

struct ip_dvfs_map_cfg dcam_dvfs_config_table[8] = {
	{0, REG_MM_DVFS_AHB_DCAM_IF_INDEX0_MAP,
		VOLT70, DCAM_CLK1920, DCAM_CLK_INDEX_1920,
		3, 0, DCAMAXI_CLK_INDEX_2560, 0, "0.7v"},
	{1, REG_MM_DVFS_AHB_DCAM_IF_INDEX1_MAP,
		VOLT70, DCAM_CLK1920, DCAM_CLK_INDEX_1920,
		0, 0, DCAMAXI_CLK_INDEX_2560, 0, "0.7v"},
	{2, REG_MM_DVFS_AHB_DCAM_IF_INDEX2_MAP,
		VOLT70, DCAM_CLK2560, DCAM_CLK_INDEX_2560,
		0, 0, DCAMAXI_CLK_INDEX_2560, 0, "0.7v"},
	{3, REG_MM_DVFS_AHB_DCAM_IF_INDEX3_MAP,
		VOLT70, DCAM_CLK2560, DCAM_CLK_INDEX_2560,
		0, 0, DCAMAXI_CLK_INDEX_3072, 0, "0.7v"},
	{4, REG_MM_DVFS_AHB_DCAM_IF_INDEX4_MAP,
		VOLT70, DCAM_CLK3072, DCAM_CLK_INDEX_3072,
		0, 0, DCAMAXI_CLK_INDEX_3072, 0, "0.7v"},
	{5, REG_MM_DVFS_AHB_DCAM_IF_INDEX5_MAP,
		VOLT70, DCAM_CLK3072, DCAM_CLK_INDEX_3072,
		0, 0, DCAMAXI_CLK_INDEX_3072, 0, "0.7v"},
	{6, REG_MM_DVFS_AHB_DCAM_IF_INDEX6_MAP,
		VOLT75, DCAM_CLK3840, DCAM_CLK_INDEX_3840,
		0, 0, DCAMAXI_CLK_INDEX_3840, 0, "0.75v"},
	{7, REG_MM_DVFS_AHB_DCAM_IF_INDEX7_MAP,
		VOLT75, DCAM_CLK4680, DCAM_CLK_INDEX_4680,
		0, 0, DCAMAXI_CLK_INDEX_4680, 0, "0.75v"},
};

struct ip_dvfs_map_cfg fd_dvfs_config_table[8] = {
	{0, REG_MM_DVFS_AHB_FD_INDEX0_MAP, VOLT70,
		FD_CLK768, FD_CLK_INDEX_768, 0, 0, 0, MTX_CLK_INDEX_768, "0.7v"},
	{1, REG_MM_DVFS_AHB_FD_INDEX1_MAP, VOLT70,
		FD_CLK1920, FD_CLK_INDEX_1920, 0, 0, 0, MTX_CLK_INDEX_1280, "0.7v"},
	{2, REG_MM_DVFS_AHB_FD_INDEX2_MAP, VOLT70,
		FD_CLK3072, FD_CLK_INDEX_3072, 0, 0, 0, MTX_CLK_INDEX_2560, "0.7v"},
	{3, REG_MM_DVFS_AHB_FD_INDEX3_MAP, VOLT75,
		FD_CLK3840, FD_CLK_INDEX_3840, 0, 0, 0, MTX_CLK_INDEX_3840, "0.75v"},
	{4, REG_MM_DVFS_AHB_FD_INDEX4_MAP, VOLT75,
		FD_CLK3840, FD_CLK_INDEX_3840, 0, 0, 0, MTX_CLK_INDEX_4680, "0.75v"},
	{5, REG_MM_DVFS_AHB_FD_INDEX5_MAP, VOLT80,
		FD_CLK3840, FD_CLK_INDEX_3840, 0, 0, 0, MTX_CLK_INDEX_5120, "0.8v"},
	{6, REG_MM_DVFS_AHB_FD_INDEX6_MAP, VOLT80,
		FD_CLK3840, FD_CLK_INDEX_3840, 0, 0, 0, MTX_CLK_INDEX_5120, "0.8v"},
	{7, REG_MM_DVFS_AHB_FD_INDEX7_MAP, VOLT80,
		FD_CLK3840, FD_CLK_INDEX_3840, 0, 0, 0, MTX_CLK_INDEX_5120, "0.8v"},
};

struct ip_dvfs_map_cfg isp_dvfs_config_table[8] = {
	{0, REG_MM_DVFS_AHB_ISP_INDEX0_MAP, VOLT70,
		ISP_CLK2560, ISP_CLK_INDEX_2560, 0, 0, 0, MTX_CLK_INDEX_2560, "0.7v"},
	{1, REG_MM_DVFS_AHB_ISP_INDEX1_MAP, VOLT70,
		ISP_CLK3072, ISP_CLK_INDEX_3072, 0, 0, 0, MTX_CLK_INDEX_3072, "0.7v"},
	{2, REG_MM_DVFS_AHB_ISP_INDEX2_MAP, VOLT75,
		ISP_CLK3840, ISP_CLK_INDEX_3840, 0, 0, 0, MTX_CLK_INDEX_3840, "0.75v"},
	{3, REG_MM_DVFS_AHB_ISP_INDEX3_MAP, VOLT75,
		ISP_CLK4680, ISP_CLK_INDEX_4680, 0, 0, 0, MTX_CLK_INDEX_4680, "0.75v"},
	{4, REG_MM_DVFS_AHB_ISP_INDEX4_MAP, VOLT80,
		ISP_CLK5120, ISP_CLK_INDEX_5120, 0, 0, 0, MTX_CLK_INDEX_5120, "0.8v"},
	{5, REG_MM_DVFS_AHB_ISP_INDEX5_MAP, VOLT80,
		ISP_CLK5120, ISP_CLK_INDEX_5120, 0, 0, 0, MTX_CLK_INDEX_5120, "0.8v"},
	{6, REG_MM_DVFS_AHB_ISP_INDEX6_MAP, VOLT80,
		ISP_CLK5120, ISP_CLK_INDEX_5120, 0, 0, 0, MTX_CLK_INDEX_5120, "0.8v"},
	{7, REG_MM_DVFS_AHB_ISP_INDEX7_MAP, VOLT80,
		ISP_CLK5120, ISP_CLK_INDEX_5120, 0, 0, 0, MTX_CLK_INDEX_5120, "0.8v"},
};

struct ip_dvfs_map_cfg jpg_dvfs_config_table[8] = {
	{0, REG_MM_DVFS_AHB_JPG_INDEX0_MAP, VOLT70,
		JPG_CLK768, JPG_CLK_INDEX_768, 0, 0, 0, MTX_CLK_INDEX_768, "0.7v"},
	{1, REG_MM_DVFS_AHB_JPG_INDEX1_MAP, VOLT70,
		JPG_CLK1280, JPG_CLK_INDEX_1280, 0, 0, 0, MTX_CLK_INDEX_1280, "0.7v"},
	{2, REG_MM_DVFS_AHB_JPG_INDEX2_MAP, VOLT70,
		JPG_CLK2560, JPG_CLK_INDEX_2560, 0, 0, 0, MTX_CLK_INDEX_2560, "0.7v"},
	{3, REG_MM_DVFS_AHB_JPG_INDEX3_MAP, VOLT75,
		JPG_CLK3840, JPG_CLK_INDEX_3840, 0, 0, 0, MTX_CLK_INDEX_2560, "0.75v"},
	{4, REG_MM_DVFS_AHB_JPG_INDEX4_MAP, VOLT75,
		JPG_CLK3840, JPG_CLK_INDEX_3840, 0, 0, 0, MTX_CLK_INDEX_3072, "0.75v"},
	{5, REG_MM_DVFS_AHB_JPG_INDEX5_MAP, VOLT75,
		JPG_CLK3840, JPG_CLK_INDEX_3840, 0, 0, 0, MTX_CLK_INDEX_3840, "0.75v"},
	{6, REG_MM_DVFS_AHB_JPG_INDEX6_MAP, VOLT75,
		JPG_CLK3840, JPG_CLK_INDEX_3840, 0, 0, 0, MTX_CLK_INDEX_5120, "0.8v"},
	{7, REG_MM_DVFS_AHB_JPG_INDEX7_MAP, VOLT75,
		JPG_CLK3840, JPG_CLK_INDEX_3840, 0, 0, 0, MTX_CLK_INDEX_5120, "0.8v"},
};

struct ip_dvfs_map_cfg mtx_dvfs_config_table[] = {
	{0, REG_MM_DVFS_AHB_MM_MTX_INDEX0_MAP, VOLT70,
		MTX_CLK768, MTX_CLK_INDEX_768,   0, 0, 0, 0, "0.7v"},
	{1, REG_MM_DVFS_AHB_MM_MTX_INDEX1_MAP, VOLT70,
		MTX_CLK1280, MTX_CLK_INDEX_1280, 0, 0, 0, 0, "0.7v"},
	{2, REG_MM_DVFS_AHB_MM_MTX_INDEX2_MAP, VOLT70,
		MTX_CLK2560, MTX_CLK_INDEX_2560, 0, 0, 0, 0, "0.7v"},
	{3, REG_MM_DVFS_AHB_MM_MTX_INDEX3_MAP, VOLT70,
		MTX_CLK3072, MTX_CLK_INDEX_3072, 0, 0, 0, 0, "0.7v"},
	{4, REG_MM_DVFS_AHB_MM_MTX_INDEX4_MAP, VOLT75,
		MTX_CLK3840, MTX_CLK_INDEX_3840, 0, 0, 0, 0, "0.75v"},
	{5, REG_MM_DVFS_AHB_MM_MTX_INDEX5_MAP, VOLT75,
		MTX_CLK4680, MTX_CLK_INDEX_4680, 0, 0, 0, 0, "0.75v"},
	{6, REG_MM_DVFS_AHB_MM_MTX_INDEX6_MAP, VOLT80,
		MTX_CLK5120, MTX_CLK_INDEX_5120, 0, 0, 0, 0, "0.8v"},
	{7, REG_MM_DVFS_AHB_MM_MTX_INDEX7_MAP, VOLT80,
		MTX_CLK5120, MTX_CLK_INDEX_5120, 0, 0, 0, 0, "0.8v"},
};
