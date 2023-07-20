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

#ifndef _IMG_SCALE_H_
#define _IMG_SCALE_H_

#include "scale_drv.h"

#include <asm/ioctl.h>
#include <linux/types.h>

struct scale_k_private {
	struct completion start_com;
	void *coeff_addr;
	struct platform_device *pdev;
};

enum scale_k_status {
	SCALE_K_IDLE,
	SCALE_K_RUNNING,
	SCALE_K_DONE,
	SCALE_K_MAX
};

struct scale_k_file {
	struct scale_k_private *scale_private;

	struct completion scale_done_com;

	struct scale_drv_private drv_private;

	struct device_node *dn;
};

int scale_k_init(void);
void scale_k_exit(void);

#endif
