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

#include <linux/uaccess.h>
#include <sprd_mm.h>
#include "sprd_isp_r6p91.h"
#include "isp_reg.h"

static int32_t isp_k_post_cdn_block(struct isp_io_param *param)
{
	int32_t ret = 0;
	struct isp_dev_post_cdn_info post_cdn_info;
	uint32_t val = 0;
	uint32_t i = 0;

	memset(&post_cdn_info, 0x00, sizeof(post_cdn_info));

	ret = copy_from_user((void *)&post_cdn_info,
		param->property_param, sizeof(post_cdn_info));
	if (ret != 0) {
		pr_info("copy_from_user error, ret = 0x%x\n", (uint32_t)ret);
		return -1;
	}


	ISP_REG_WR(ISP_POSTCDN_ADPT_THR, post_cdn_info.adapt_med_thr & 0x3FFF);

	ISP_REG_MWR(ISP_POSTCDN_COMMON_CTRL, 0xE0,
		post_cdn_info.median_mode << 5);

	if (post_cdn_info.uvjoint)
		ISP_REG_OWR(ISP_POSTCDN_COMMON_CTRL, BIT_4);
	else
		ISP_REG_MWR(ISP_POSTCDN_COMMON_CTRL, BIT_4, 0);

	if (post_cdn_info.writeback_en)
		ISP_REG_OWR(ISP_POSTCDN_COMMON_CTRL, BIT_3);
	else
		ISP_REG_MWR(ISP_POSTCDN_COMMON_CTRL, BIT_3, 0);

	if (post_cdn_info.mode)
		ISP_REG_OWR(ISP_POSTCDN_COMMON_CTRL, BIT_2);
	else
		ISP_REG_MWR(ISP_POSTCDN_COMMON_CTRL, BIT_2, 0);

	val = (post_cdn_info.uvthr0 & 0x1FF)
		| ((post_cdn_info.uvthr1 & 0x1FF) << 16);
	ISP_REG_WR(ISP_POSTCDN_UVTHR, val);
	val = (post_cdn_info.thru0 & 0xFF)
		| ((post_cdn_info.thru1 & 0xFF) << 16);
	ISP_REG_WR(ISP_POSTCDN_THRU, val);
	val = (post_cdn_info.thrv0 & 0xFF)
		| ((post_cdn_info.thrv1 & 0xFF) << 16);
	ISP_REG_WR(ISP_POSTCDN_THRV, val);

	for (i = 0; i < 3; i++) {
		val = (post_cdn_info.r_segu[0][i*2] & 0xFF)
			| ((post_cdn_info.r_segu[1][i*2] & 0x7) << 8)
			| ((post_cdn_info.r_segu[0][i*2+1] & 0xFF) << 16)
			| ((post_cdn_info.r_segu[1][i*2+1] & 0x7) << 24);
		ISP_REG_WR(ISP_POSTCDN_RSEGU01 + i * 4, val);
	}
	val = (post_cdn_info.r_segu[0][6] & 0xFF)
		| ((post_cdn_info.r_segu[1][6] & 0x7) << 8);
	ISP_REG_WR(ISP_POSTCDN_RSEGU6, val);
	for (i = 0; i < 3; i++) {
		val = (post_cdn_info.r_segv[0][i*2] & 0xFF)
			| ((post_cdn_info.r_segv[1][i*2] & 0x7) << 8)
			| ((post_cdn_info.r_segv[0][i*2+1] & 0xFF) << 16)
			| ((post_cdn_info.r_segv[1][i*2+1] & 0x7) << 24);
		ISP_REG_WR(ISP_POSTCDN_RSEGV01 + i * 4, val);
	}
	val = (post_cdn_info.r_segv[0][6] & 0xFF)
		| ((post_cdn_info.r_segv[1][6] & 0x7) << 8);
	ISP_REG_WR(ISP_POSTCDN_RSEGV6, val);


	for (i = 0; i < 15; i++) {
		val = (post_cdn_info.r_distw[i][0] & 0x7)
			| ((post_cdn_info.r_distw[i][1] & 0x7) << 3)
			| ((post_cdn_info.r_distw[i][2] & 0x7) << 6)
			| ((post_cdn_info.r_distw[i][3] & 0x7) << 9)
			| ((post_cdn_info.r_distw[i][4] & 0x7) << 12);
		ISP_REG_WR(ISP_POSTCDN_R_DISTW0 + i * 4, val);
	}

	ISP_REG_WR(ISP_POSTCDN_START_ROW_MOD4,
		post_cdn_info.start_row_mod4 & 0x3);

	if (post_cdn_info.downsample_bypass)
		ISP_REG_OWR(ISP_POSTCDN_COMMON_CTRL, BIT_1);
	else
		ISP_REG_MWR(ISP_POSTCDN_COMMON_CTRL, BIT_1, 0);

	if (post_cdn_info.bypass)
		ISP_REG_OWR(ISP_POSTCDN_COMMON_CTRL, BIT_0);
	else
		ISP_REG_MWR(ISP_POSTCDN_COMMON_CTRL, BIT_0, 0);


	return ret;
}

static int32_t isp_k_post_cdn_slice_size(struct isp_io_param *param)
{
	int32_t ret = 0;
	uint32_t start_row_mod4 = 0;

	ret = copy_from_user((void *)&start_row_mod4,
		param->property_param, sizeof(start_row_mod4));
	if (ret != 0) {
		pr_info("copy_from_user error, ret = 0x%x\n", (uint32_t)ret);
		return -1;
	}

	ISP_REG_WR(ISP_POSTCDN_START_ROW_MOD4, (start_row_mod4 & 0x3));

	return ret;
}

int32_t isp_k_cfg_post_cdn(struct isp_io_param *param)
{
	int32_t ret = 0;

	if (!param) {
		pr_info("isp_k_cfg_post_cdn: param is null error.\n");
		return -1;
	}

	if (param->property_param == NULL) {
		pr_info("isp_k_cfg_post_cdn: property_param is null error.\n");
		return -1;
	}

	switch (param->property) {
	case ISP_PRO_POST_CDN_BLOCK:
		ret = isp_k_post_cdn_block(param);
		break;
	case ISP_PRO_POST_CDN_SLICE_SIZE:
		ret = isp_k_post_cdn_slice_size(param);
		break;
	default:
		pr_info("fail cmd id:%d, not supported.\n", param->property);
		break;
	}

	return ret;
}
