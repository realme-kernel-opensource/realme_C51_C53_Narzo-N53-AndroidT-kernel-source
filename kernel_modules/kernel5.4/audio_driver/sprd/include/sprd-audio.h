/*
 * Copyright (C) 2015 Spreadtrum Communications Inc.
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

#ifndef __SPRD_AUDIO_H
#define __SPRD_AUDIO_H

#include <linux/delay.h>
#include <linux/dma/sprd-dma.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

enum {
	PLATFORM_SHARKL2 = 0,
	PLATFORM_WHALE2 = 1,
};

/* aon apb global registers operating interfaces
 */
extern struct regmap *arch_audio_get_aon_apb_gpr(void);
int aon_apb_gpr_null_check(void);

#define aon_apb_reg_update(reg, msk, val) \
	regmap_update_bits(arch_audio_get_aon_apb_gpr(), (reg), (msk), (val))
#define aon_apb_reg_set(reg, bit) \
	regmap_update_bits(arch_audio_get_aon_apb_gpr(), (reg), (bit), (bit))
#define aon_apb_reg_clr(reg, bit) \
	regmap_update_bits(arch_audio_get_aon_apb_gpr(), (reg), (bit), (0))
#define aon_apb_reg_read(reg, val) \
	regmap_read(arch_audio_get_aon_apb_gpr(), (reg), (val))

#define aon_apb_reg_hw_set(reg, bit) \
	regmap_hwlock_update_bits(arch_audio_get_aon_apb_gpr(), (reg), (bit), (bit))
#define aon_apb_reg_hw_clr(reg, bit) \
	regmap_hwlock_update_bits(arch_audio_get_aon_apb_gpr(), (reg), (bit), (0))

/*
 * Returns zero for success, a negative number on error.
 */
static inline int aud_aon_bit_raw_set(u32 reg, u32 bit)
{
	int ret = 0;
	u32 val = 0;

	aon_apb_gpr_null_check();

	ret = aon_apb_reg_set(reg, bit);
	pr_debug("bits set reg(%#x): %#lx\n", reg,
		 (aon_apb_reg_read(reg, &val) < 0) ? ~0ul : val);

	return ret;
}

static inline int aud_aon_bit_raw_clear(u32 reg, u32 bit)
{
	int ret = 0;
	u32 val = 0;

	aon_apb_gpr_null_check();

	ret = aon_apb_reg_clr(reg, bit);

	pr_debug("bits clear reg(%#x): %#lx\n", reg,
		 (aon_apb_reg_read(reg, &val) < 0) ? ~0ul : val);

	return ret;
}

void arch_audio_set_aon_apb_gpr(struct regmap *gpr);

/*
 * agcp ahb global registers operating interfaces
 */
extern u32 get_agcp_ahb_set_offset(void);
extern u32 get_agcp_ahb_clr_offset(void);
extern struct regmap *arch_audio_get_agcp_ahb_gpr(void);
void set_agcp_ahb_offset(u32 set_ahb_offset, u32 clr_ahb_offset);
int agcp_ahb_gpr_null_check(void);

#define agcp_ahb_reg_update(reg, msk, val) \
	regmap_update_bits(arch_audio_get_agcp_ahb_gpr(), (reg), (msk), (val))
#define agcp_ahb_reg_set(reg, bit) \
	regmap_write(arch_audio_get_agcp_ahb_gpr(), (reg + get_agcp_ahb_set_offset()), (bit))
#define agcp_ahb_reg_clr(reg, bit) \
	regmap_write(arch_audio_get_agcp_ahb_gpr(), (reg + get_agcp_ahb_clr_offset()), (bit))
#define agcp_ahb_reg_read(reg, val) \
	regmap_read(arch_audio_get_agcp_ahb_gpr(), (reg), (val))

void arch_audio_set_agcp_ahb_gpr(struct regmap *gpr);

/*
 * pmu apb global registers operating interfaces
 */
extern struct regmap *arch_audio_get_pmu_apb_gpr(void);
int pmu_apb_gpr_null_check(void);

#define pmu_apb_reg_update(reg, msk, val) \
	regmap_update_bits(arch_audio_get_pmu_apb_gpr(), (reg), (msk), (val))
#define pmu_apb_reg_set(reg, bit) \
	regmap_update_bits(arch_audio_get_pmu_apb_gpr(), (reg), (bit), (bit))
#define pmu_apb_reg_clr(reg, bit) \
	regmap_update_bits(arch_audio_get_pmu_apb_gpr(), (reg), (bit), (0))
#define pmu_apb_reg_read(reg, val) \
	regmap_read(arch_audio_get_pmu_apb_gpr(), (reg), (val))

void arch_audio_set_pmu_apb_gpr(struct regmap *gpr);

/*
 * pmu completement apb global registers operating interfaces
 */
extern struct regmap *arch_audio_get_pmu_com_apb_gpr(void);
int pmu_com_apb_gpr_null_check(void);

#define pmu_com_apb_reg_update(reg, msk, val) \
	regmap_update_bits(arch_audio_get_pmu_com_apb_gpr(), (reg), (msk), (val))
#define pmu_com_apb_reg_set(reg, bit) \
	regmap_update_bits(arch_audio_get_pmu_com_apb_gpr(), (reg), (bit), (bit))
#define pmu_com_apb_reg_clr(reg, bit) \
	regmap_update_bits(arch_audio_get_pmu_com_apb_gpr(), (reg), (bit), (0))
#define pmu_com_apb_reg_read(reg, val) \
	regmap_read(arch_audio_get_pmu_com_apb_gpr(), (reg), (val))

void arch_audio_set_pmu_com_apb_gpr(struct regmap *gpr);

/*
 * ap apb global registers operating interfaces
 * ap_apb_gpr will be set by i2s.c in its probe func.
 */
extern struct regmap *arch_audio_get_ap_apb_gpr(void);
int ap_apb_gpr_null_check(void);

#define ap_apb_reg_update(reg, msk, val) \
	regmap_update_bits(arch_audio_get_ap_apb_gpr(), (reg), (msk), (val))
#define ap_apb_reg_set(reg, bit) \
	regmap_update_bits(arch_audio_get_ap_apb_gpr(), (reg), (bit), (bit))
#define ap_apb_reg_clr(reg, bit) \
	regmap_update_bits(arch_audio_get_ap_apb_gpr(), (reg), (bit), (0))
#define ap_apb_reg_read(reg, val) \
	regmap_read(arch_audio_get_ap_apb_gpr(), (reg), (val))

void arch_audio_set_ap_apb_gpr(struct regmap *gpr);

/* anlg_phy_g_controller */
extern struct regmap *arch_audio_get_anlg_phy_g(void);
int anlg_phy_g_null_check(void);

#define anlg_phy_g_reg_update(reg, msk, val) \
	regmap_update_bits(arch_audio_get_anlg_phy_g(), (reg), (msk), (val))
#define anlg_phy_g_reg_set(reg, bit) \
	regmap_update_bits(arch_audio_get_anlg_phy_g(), (reg), (bit), (bit))
#define anlg_phy_g_reg_clr(reg, bit) \
	regmap_update_bits(arch_audio_get_anlg_phy_g(), (reg), (bit), (0))
#define anlg_phy_g_reg_read(reg, val) \
	regmap_read(arch_audio_get_anlg_phy_g(), (reg), (val))

void arch_audio_set_anlg_phy_g(struct regmap *gpr);

#if (defined(CONFIG_SOC_IWHALE2) || defined(CONFIG_SOC_WHALE2))
/* iwhale2 will include sprd-audio-whale2.h */
#include "sprd-audio-whale2.h"
#elif defined(CONFIG_SC9833)
/* sharkl2 */
#include "sprd-audio-sharkl2.h"
#elif defined(CONFIG_SOC_ISHARKL2)
/* isharkl2 */
#include "sprd-audio-isharkl2.h"
#elif defined(CONFIG_SOC_SHARKLJ1)
/* sharklj1 */
#include "sprd-audio-sharklj1.h"
#elif (defined(CONFIG_SND_SOC_UNISOC_SHARKLE) || defined(CONFIG_SND_SOC_UNISOC_SHARKLE_MODULE))
#include "sprd-audio-sharkle.h"
/* pike2 */
#elif (defined(CONFIG_SND_SOC_UNISOC_PIKE2) || defined(CONFIG_SND_SOC_UNISOC_PIKE2_MODULE))
#include "sprd-audio-pike2.h"
/* sharkl3 */
#elif (defined(CONFIG_SND_SOC_UNISOC_SHARKL3) || defined(CONFIG_SND_SOC_UNISOC_SHARKL3_MODULE))
#include "sprd-audio-sharkl3.h"
#else
#include "sprd-audio-agcp.h"
#endif

struct glb_reg_dump {
	char *reg_name;
	unsigned long reg;
	int count;
	int (*func)(void *);
};

#define REG_PAIR(t, r, c, f) \
	{t->reg_name = #r; t->reg = r; t->count = c; t->func = f; t++; }

#ifndef AUDIO_VBC_REG_DUMP_LIST
#define AUDIO_VBC_REG_DUMP_LIST(t)
#endif
#ifndef AUDIO_CODEC_REG_DUMP_LIST
#define AUDIO_CODEC_REG_DUMP_LIST(t)
#endif
#ifndef AUDIO_IIS_REG_DUMP_LIST
#define AUDIO_IIS_REG_DUMP_LIST(t)
#endif

#define AUDIO_GLB_REG_DUMP_LIST(t) { \
	AUDIO_VBC_REG_DUMP_LIST(t) \
	AUDIO_CODEC_REG_DUMP_LIST(t) \
	AUDIO_IIS_REG_DUMP_LIST(t) \
}

#endif/* __SPRD_AUDIO_H */
