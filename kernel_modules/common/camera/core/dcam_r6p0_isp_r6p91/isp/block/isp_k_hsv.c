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

static int32_t isp_k_hsv_block(struct isp_io_param *param)
{
	int32_t ret = 0;
	int32_t buf_size = 0;
	unsigned long dst_addr = 0;
	struct isp_dev_hsv_info hsv_info;
	void *data_ptr = NULL;

	memset(&hsv_info, 0x00, sizeof(hsv_info));

	ret = copy_from_user((void *)&hsv_info,
		param->property_param, sizeof(hsv_info));
	if (ret != 0) {
		pr_info("copy error, ret=0x%x\n", (uint32_t)ret);
		return -1;
	}

	data_ptr = (void *)(hsv_info.data_ptr);

	buf_size = ISP_HSV_ITEM * 4;
	if (data_ptr == NULL || hsv_info.size > buf_size) {
		pr_info("isp_k_hsv_block : memory error %p %x\n",
			data_ptr, hsv_info.size);
		return -1;
	}

	if (hsv_info.buf_sel << 1 & BIT_1)
		dst_addr = ISP_BASE_ADDR + ISP_HSV_BUF1_CH0;
	else
		dst_addr = ISP_BASE_ADDR + ISP_HSV_BUF0_CH0;

	ret = copy_from_user((void *)dst_addr, data_ptr, hsv_info.size);
	if (ret != 0) {
		pr_info("isp_k_hsv_block: copy error 0x%x\n", (uint32_t)ret);
		return -1;
	}

	ISP_REG_MWR(ISP_HSV_PARAM, BIT_1, hsv_info.buf_sel << 1);

	ISP_REG_MWR(ISP_HSV_PARAM, BIT_0, hsv_info.bypass);

	return ret;

}


int32_t isp_k_cfg_hsv(struct isp_io_param *param)
{
	int32_t ret = 0;

	if (!param) {
		pr_info("isp_k_cfg_hsv: param is null error.\n");
		return -1;
	}

	if (param->property_param == NULL) {
		pr_info("isp_k_cfg_hsv: property_param is null error.\n");
		return -1;
	}

	switch (param->property) {
	case ISP_PRO_HSV_BLOCK:
		ret = isp_k_hsv_block(param);
		break;
	default:
		pr_info("fail cmd id:%d, not supported.\n", param->property);
		break;
	}

	return ret;
}

