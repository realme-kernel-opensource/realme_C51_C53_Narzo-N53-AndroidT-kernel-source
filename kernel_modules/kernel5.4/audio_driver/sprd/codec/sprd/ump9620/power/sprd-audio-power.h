/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * sound/soc/sprd/codec/sprd/sc2721/sprd-audio-power.h
 *
 * SPRD-AUDIO-POWER -- SpreadTrum intergrated audio power supply.
 *
 * Copyright (C) 2015 SpreadTrum Ltd.
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

#ifndef __SPRD_AUDIO_POWER_H
#define __SPRD_AUDIO_POWER_H

#include "sprd-codec.h"

#define SPRD_AUDIO_POWER_VREG		1
#define SPRD_AUDIO_POWER_VB		2
#define SPRD_AUDIO_POWER_BG		3
#define SPRD_AUDIO_POWER_BIAS		4
#define SPRD_AUDIO_POWER_MICBIAS1	5
#define SPRD_AUDIO_POWER_MICBIAS2	6
#define SPRD_AUDIO_POWER_MICBIAS3	7
#define SPRD_AUDIO_POWER_HEADMICBIAS	8
#define SPRD_AUDIO_POWER_DCL			9
#define SPRD_AUDIO_POWER_DIG_CLK_INTC	10
#define SPRD_AUDIO_POWER_DIG_CLK_HID	11
#define SPRD_AUDIO_POWER_CLK_DCL_32K	12
#define SPRD_AUDIO_POWER_DAHPL_CHN	13

struct sprd_audio_power_info {
	int id;
	/* enable/disable register information */
	int en_reg;
	int en_bit;
	int (*power_enable)(struct sprd_audio_power_info *info);
	int (*power_disable)(struct sprd_audio_power_info *info);
	/* configure deepsleep cut off or not */
	int (*sleep_ctrl)(void *data, int en);
	/* voltage level register information */
	int v_reg;
	int v_mask;
	int v_shift;
	int v_table_len;
	const u16 *v_table;
	int min_uV;
	int max_uV;

	/* unit: us */
	int on_delay;
	int off_delay;

	/* used by regulator core */
	struct regulator_desc desc;

	void *data;
};

#endif
