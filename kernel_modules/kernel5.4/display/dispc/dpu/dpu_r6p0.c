// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Unisoc Inc.
 */

#include <drm/drm_vblank.h>
#include <linux/backlight.h>
#include <linux/dma-buf.h>
#include <linux/delay.h>
#include <linux/of_address.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/dma-mapping.h>
#include <linux/trusty/smcall.h>
#include "sprd_bl.h"
#include "sprd_dpu.h"
#include "sprd_dvfs_dpu.h"
#include "dpu_enhance_param.h"
#include "sprd_dsi.h"
#include "sprd_crtc.h"
#include "sprd_plane.h"
#include "sprd_dsi_panel.h"
#include <../drivers/trusty/trusty.h>
#include "sprd_dsc.h"

#define XFBC8888_HEADER_SIZE(w, h) (ALIGN((ALIGN((w), 16)) * \
				(ALIGN((h), 16)) / 16, 128))
#define XFBC8888_PAYLOAD_SIZE(w, h) (ALIGN((w), 16) * ALIGN((h), 16) * 4)
#define XFBC8888_BUFFER_SIZE(w, h) (XFBC8888_HEADER_SIZE(w, h) \
				+ XFBC8888_PAYLOAD_SIZE(w, h))

#define SLP_BRIGHTNESS_THRESHOLD 0x20

/* DPU registers size, 4 Bytes(32 Bits) */
#define DPU_REG_SIZE					0x04
/* Layer registers offset */
#define DPU_LAY_REG_OFFSET				0x10

#define DPU_MAX_REG_OFFSET				0x19AC

#define DSC_REG_OFFSET					0x1A00
#define DSC1_REG_OFFSET					0x1B00

#define DPU_REG_RD(reg) readl_relaxed(reg)

#define DPU_REG_WR(reg, mask) writel_relaxed(mask, reg)

#define DPU_REG_SET(reg, mask) \
		writel_relaxed(readl_relaxed(reg) | mask, reg)

#define DPU_REG_CLR(reg, mask) \
		writel_relaxed(readl_relaxed(reg) & ~mask, reg)

#define DPU_LAY_REG(reg, index) \
		(reg + index * DPU_LAY_REG_OFFSET * DPU_REG_SIZE)

#define DPU_LAY_PLANE_ADDR(reg, index, plane) \
		(reg + index * DPU_LAY_REG_OFFSET * DPU_REG_SIZE + plane * DPU_REG_SIZE)

#define DSC_REG(reg) (reg + DSC_REG_OFFSET)

#define DSC1_REG(reg) (reg + DSC1_REG_OFFSET)

/* DSC_PicW_PicH_SliceW_SliceH  */
#define DSC_1440_2560_720_2560	0
#define DSC_1080_2408_540_8	1
#define DSC_720_2560_720_8	2
#define DSC_1080_2400_540_2400	3

/*Global control registers */
#define REG_DPU_CTRL					0x08
#define REG_DPU_CFG0					0x0C
#define REG_DPU_CFG1					0x10
#define REG_PANEL_SIZE					0x18
#define REG_BLEND_SIZE					0x1C
#define REG_SCL_EN					0x20
#define REG_BG_COLOR					0x24

/* DPU Secure reg */
#define REG_DPU_SECURE					0x14

/* Layer enable */
#define REG_LAYER_ENABLE				0x2c

/* Layer0 control registers */
#define REG_LAY_BASE_ADDR				0x30
#define REG_LAY_CTRL					0x40
#define REG_LAY_DES_SIZE				0x44
#define REG_LAY_SRC_SIZE				0x48
#define REG_LAY_PITCH					0x4C
#define REG_LAY_POS					0x50
#define REG_LAY_ALPHA					0x54
#define REG_LAY_CK					0x58
#define REG_LAY_PALLETE					0x5C
#define REG_LAY_CROP_START				0x60

/* Write back config registers */
#define REG_WB_BASE_ADDR				0x230
#define REG_WB_CTRL					0x234
#define REG_WB_CFG					0x238
#define REG_WB_PITCH					0x23C

/* Interrupt control registers */
#define REG_DPU_INT_EN					0x250
#define REG_DPU_INT_CLR					0x254
#define REG_DPU_INT_STS					0x258
#define REG_DPU_INT_RAW					0x25C

/* DPI control registers */
#define REG_DPI_CTRL					0x260
#define REG_DPI_H_TIMING				0x264
#define REG_DPI_V_TIMING				0x268

/* DPU STS */
#define REG_DPU_STS_20					0x750
#define REG_DPU_STS_21					0x754
#define REG_DPU_STS_22					0x758

#define REG_DPU_MMU0_UPDATE				0x1808
#define REG_DPU_MODE					0x04

/* DPU SCL */
#define REG_DPU_SCL_EN					0x20

/* DSC REG */
#define REG_DSC_CTRL					0x00
#define REG_DSC_PIC_SIZE				0x04
#define REG_DSC_GRP_SIZE				0x08
#define REG_DSC_SLICE_SIZE				0x0c
#define REG_DSC_H_TIMING				0x10
#define REG_DSC_V_TIMING				0x14
#define REG_DSC_CFG0					0x18
#define REG_DSC_CFG1					0x1c
#define REG_DSC_CFG2					0x20
#define REG_DSC_CFG3					0x24
#define REG_DSC_CFG4					0x28
#define REG_DSC_CFG5					0x2c
#define REG_DSC_CFG6					0x30
#define REG_DSC_CFG7					0x34
#define REG_DSC_CFG8					0x38
#define REG_DSC_CFG9					0x3c
#define REG_DSC_CFG10					0x40
#define REG_DSC_CFG11					0x44
#define REG_DSC_CFG12					0x48
#define REG_DSC_CFG13					0x4c
#define REG_DSC_CFG14					0x50
#define REG_DSC_CFG15					0x54
#define REG_DSC_CFG16					0x58
#define REG_DSC_STS0					0x5c
#define REG_DSC_STS1					0x60
#define REG_DSC_VERSION					0x64

/* PQ Enhance config registers */
#define REG_DPU_ENHANCE_CFG				0x500
#define REG_ENHANCE_UPDATE				0x504
#define REG_SLP_LUT_BASE_ADDR				0x510
#define REG_THREED_LUT_BASE_ADDR			0x514
#define REG_HSV_LUT_BASE_ADDR				0x518
#define REG_GAMMA_LUT_BASE_ADDR				0x51C
#define REG_EPF_EPSILON					0x520
#define REG_EPF_GAIN0_3					0x524
#define REG_EPF_GAIN4_7					0x528
#define REG_EPF_DIFF					0x52C
#define REG_CM_COEF01_00				0x530
#define REG_CM_COEF03_02				0x534
#define REG_CM_COEF11_10				0x538
#define REG_CM_COEF13_12				0x53C
#define REG_CM_COEF21_20				0x540
#define REG_CM_COEF23_22				0x544
#define REG_SLP_CFG0					0x550
#define REG_SLP_CFG1					0x554
#define REG_SLP_CFG2					0x558
#define REG_SLP_CFG3					0x55C
#define REG_SLP_CFG4					0x560
#define REG_SLP_CFG5					0x564
#define REG_SLP_CFG6					0x568
#define REG_SLP_CFG7					0x56C
#define REG_SLP_CFG8					0x570
#define REG_SLP_CFG9					0x574
#define REG_SLP_CFG10					0x578
#define REG_HSV_CFG					0x580
#define REG_CABC_CFG0					0x590
#define REG_CABC_CFG1					0x594
#define REG_CABC_CFG2					0x598
#define REG_CABC_CFG3					0x59C
#define REG_CABC_CFG4					0x5A0
#define REG_CABC_CFG5					0x5A4
#define REG_UD_CFG0					0x5B0
#define REG_UD_CFG1					0x5B4
#define REG_CABC_HIST0					0x600
#define REG_GAMMA_LUT_ADDR				0x780
#define REG_GAMMA_LUT_RDATA				0x784
#define REG_SLP_LUT_ADDR				0x798
#define REG_SLP_LUT_RDATA				0x79C
#define REG_HSV_LUT0_ADDR				0x7A0
#define REG_HSV_LUT0_RDATA				0x7A4
#define REG_THREED_LUT0_ADDR				0x7C0
#define REG_THREED_LUT0_RDATA				0x7C4
#define REG_DPU_MMU_EN					0x1804
#define REG_DPU_MMU_INV_ADDR_RD				0x185C
#define REG_DPU_MMU_INV_ADDR_WR				0x1860
#define REG_DPU_MMU_UNS_ADDR_RD				0x1864
#define REG_DPU_MMU_UNS_ADDR_WR				0x1868
#define REG_DPU_MMU_INT_EN				0x18A0
#define REG_DPU_MMU_INT_CLR				0x18A4
#define REG_DPU_MMU_INT_STS				0x18A8
#define REG_DPU_MMU_INT_RAW				0x18AC

#define REG_DPU_MMU1_EN					0x1904
#define REG_DPU_MMU1_INV_ADDR_RD			0x195C
#define REG_DPU_MMU1_INV_ADDR_WR			0x1960
#define REG_DPU_MMU1_UNS_ADDR_RD			0x1964
#define REG_DPU_MMU1_UNS_ADDR_WR			0x1968
#define REG_DPU_MMU1_INT_EN				0x19A0
#define REG_DPU_MMU1_INT_CLR				0x19A4
#define REG_DPU_MMU1_INT_STS				0x19A8
#define REG_DPU_MMU1_INT_RAW				0x19AC

/* Global control bits */
#define BIT_DPU_RUN					BIT(0)
#define BIT_DPU_STOP					BIT(1)
#define BIT_DPU_ALL_UPDATE				BIT(2)
#define BIT_DPU_REG_UPDATE				BIT(3)
#define BIT_LAY_REG_UPDATE				BIT(4)
#define BIT_DPU_IF_EDPI					BIT(0)

/* scaling config bits */
#define BIT_DPU_SCALING_EN				BIT(0)

/* Layer control bits */
// #define BIT_DPU_LAY_EN				BIT(0)
#define BIT_DPU_LAY_LAYER_ALPHA				(0x01 << 2)
#define BIT_DPU_LAY_COMBO_ALPHA				(0x01 << 3)
#define BIT_DPU_LAY_FORMAT_YUV422_2PLANE		(0x00 << 4)
#define BIT_DPU_LAY_FORMAT_YUV420_2PLANE		(0x01 << 4)
#define BIT_DPU_LAY_FORMAT_YUV420_3PLANE		(0x02 << 4)
#define BIT_DPU_LAY_FORMAT_ARGB8888			(0x03 << 4)
#define BIT_DPU_LAY_FORMAT_RGB565			(0x04 << 4)
#define BIT_DPU_LAY_FORMAT_XFBC_ARGB8888		(0x08 << 4)
#define BIT_DPU_LAY_FORMAT_XFBC_RGB565			(0x09 << 4)
#define BIT_DPU_LAY_FORMAT_XFBC_YUV420			(0x0A << 4)
#define BIT_DPU_LAY_DATA_ENDIAN_B0B1B2B3		(0x00 << 8)
#define BIT_DPU_LAY_DATA_ENDIAN_B3B2B1B0		(0x01 << 8)
#define BIT_DPU_LAY_DATA_ENDIAN_B2B3B0B1		(0x02 << 8)
#define BIT_DPU_LAY_DATA_ENDIAN_B1B0B3B2		(0x03 << 8)
#define BIT_DPU_LAY_NO_SWITCH				(0x00 << 10)
#define BIT_DPU_LAY_RGB888_RB_SWITCH			(0x01 << 10)
#define BIT_DPU_LAY_RGB565_RB_SWITCH			(0x01 << 12)
#define BIT_DPU_LAY_PALLETE_EN				(0x01 << 13)
#define BIT_DPU_LAY_MODE_BLEND_NORMAL			(0x01 << 16)
#define BIT_DPU_LAY_MODE_BLEND_PREMULT			(0x01 << 16)

/*Interrupt control & status bits */
#define BIT_DPU_INT_DONE				BIT(0)
#define BIT_DPU_INT_TE					BIT(1)
#define BIT_DPU_INT_ERR					BIT(2)
#define BIT_DPU_INT_VSYNC				BIT(4)
#define BIT_DPU_INT_WB_DONE_EN				BIT(5)
#define BIT_DPU_INT_WB_ERR_EN				BIT(6)
#define BIT_DPU_INT_FBC_PLD_ERR				BIT(7)
#define BIT_DPU_INT_FBC_HDR_ERR				BIT(8)
#define BIT_DPU_INT_DPU_ALL_UPDATE_DONE			BIT(16)
#define BIT_DPU_INT_DPU_REG_UPDATE_DONE			BIT(17)
#define BIT_DPU_INT_LAY_REG_UPDATE_DONE			BIT(18)
#define BIT_DPU_INT_PQ_REG_UPDATE_DONE			BIT(19)
#define BIT_DPU_INT_PQ_LUT_UPDATE_DONE			BIT(20)

/* DPI control bits */
#define BIT_DPU_EDPI_TE_EN				BIT(8)
#define BIT_DPU_EDPI_FROM_EXTERNAL_PAD			BIT(10)
#define BIT_DPU_DPI_HALT_EN				BIT(16)
#define BIT_DPU_STS_RCH_DPU_BUSY			BIT(15)

/* MMU Interrupt bits */
#define BIT_DPU_INT_MMU_VAOR_RD_MASK			BIT(0)
#define BIT_DPU_INT_MMU_VAOR_WR_MASK			BIT(1)
#define BIT_DPU_INT_MMU_INV_RD_MASK			BIT(2)
#define BIT_DPU_INT_MMU_INV_WR_MASK			BIT(3)
#define BIT_DPU_INT_MMU_UNS_RD_MASK			BIT(4)
#define BIT_DPU_INT_MMU_UNS_WR_MASK			BIT(5)
#define BIT_DPU_INT_MMU_PAOR_RD_MASK			BIT(6)
#define BIT_DPU_INT_MMU_PAOR_WR_MASK			BIT(7)

/* enhance config bits */
#define BIT_DPU_ENHANCE_EN		BIT(0)
#define GAMMA_LUT_MODE			2
#define HSV_LUT_MODE			3
#define LUT3D_MODE			5
#define LUTS_SIZE_4K			(GAMMA_LUT_MODE + HSV_LUT_MODE + LUT3D_MODE * 6)
#define LUTS_COPY_TIME			((LUTS_SIZE_4K) * 2)
#define DPU_LUTS_SIZE			(LUTS_SIZE_4K * 4096)
#define DPU_LUTS_SLP_OFFSET		0
#define DPU_LUTS_GAMMA_OFFSET		4096
#define DPU_LUTS_HSV_OFFSET		(4096 * 3)
#define DPU_LUTS_LUT3D_OFFSET		(4096 * 6)
#define CABC_BL_COEF			1020
#define CABC_MODE_UI			BIT(2)
#define CABC_MODE_GAME			BIT(3)
#define CABC_MODE_VIDEO			BIT(4)
#define CABC_MODE_IMAGE			BIT(5)
#define CABC_MODE_CAMERA		BIT(6)
#define CABC_MODE_FULL_FRAME		BIT(7)

struct layer_info {
	u16 dst_x;
	u16 dst_y;
	u16 dst_w;
	u16 dst_h;
};

enum sprd_fw_attr {
	FW_ATTR_NON_SECURE = 0,
	FW_ATTR_SECURE,
	FW_ATTR_PROTECTED,
};

struct wb_region {
	u32 index;
	u16 pos_x;
	u16 pos_y;
	u16 size_w;
	u16 size_h;
};

struct layer_reg {
	u32 addr[4];
	u32 ctrl;
	u32 dst_size;
	u32 src_size;
	u32 pitch;
	u32 pos;
	u32 alpha;
	u32 ck;
	u32 pallete;
	u32 crop_start;
	u32 reserved[3];
};

struct enhance_module {
	u32 epf_en: 1;
	u32 hsv_en: 1;
	u32 cm_en: 1;
	u32 gamma_en: 1;
	u32 lut3d_en: 1;
	u32 dither_en: 1;
	u32 slp_en: 1;
	u32 ltm_en: 1;
	u32 slp_mask_en: 1;
	u32 cabc_en: 1;
	u32 ud_en: 1;
	u32 ud_local_en: 1;
	u32 ud_mask_en: 1;
	u32 scl_en: 1;
};

struct luts_typeindex {
	u16 type;
	u16 index;
};

struct scale_cfg {
	u32 in_w;
	u32 in_h;
};

struct epf_cfg {
	u16 e0;
	u16 e1;
	u8  e2;
	u8  e3;
	u8  e4;
	u8  e5;
	u8  e6;
	u8  e7;
	u8  e8;
	u8  e9;
	u8  e10;
	u8  e11;
};

struct cm_cfg {
	u16 c00;
	u16 c01;
	u16 c02;
	u16 c03;
	u16 c10;
	u16 c11;
	u16 c12;
	u16 c13;
	u16 c20;
	u16 c21;
	u16 c22;
	u16 c23;
};

struct slp_cfg {
	u16 s0;
	u16 s1;
	u8  s2;
	u8  s3;
	u8  s4;
	u8  s5;
	u8  s6;
	u8  s7;
	u8  s8;
	u8  s9;
	u8  s10;
	u8  s11;
	u16 s12;
	u16 s13;
	u8  s14;
	u8  s15;
	u8  s16;
	u8  s17;
	u8  s18;
	u8  s19;
	u8  s20;
	u8  s21;
	u8  s22;
	u8  s23;
	u16 s24;
	u16 s25;
	u8  s26;
	u8  s27;
	u8  s28;
	u8  s29;
	u8  s30;
	u8  s31;
	u8  s32;
	u8  s33;
	u8  s34;
	u8  s35;
	u8  s36;
	u8  s37;
	u8  s38;
};

struct hsv_params {
	short h0;
	short h1;
	short h2;
};

struct ud_cfg {
	short u0;
	short u1;
	short u2;
	short u3;
	short u4;
	short u5;
};

struct hsv_lut_table {
	u16 s_g[64];
	u16 h_o[64];
};

struct hsv_luts {
	struct hsv_lut_table hsv_lut[4];
};

struct gamma_entry {
	u16 r;
	u16 g;
	u16 b;
};

struct gamma_lut {
	u16 r[256];
	u16 g[256];
	u16 b[256];
};

struct threed_lut {
	uint16_t r[729];
	uint16_t g[729];
	uint16_t b[729];
};

struct rgb_integrate_arr {
	uint32_t rgb_value[729];
};

struct lut_base_addrs {
	u64 lut_gamma_addr;
	u32 lut_hsv_addr;
	u32 lut_lut3d_addr;
};

enum {
	LUTS_GAMMA_TYPE,
	LUTS_HSV_TYPE,
	LUTS_LUT3D_TYPE,
	LUTS_ALL
};

enum {
	CM_CTM,
	CM_PQ,
};

enum {
	CABC_DISABLED,
	CABC_STOPPING,
	CABC_WORKING
};

struct cabc_para {
	u32 cabc_hist[64];
	u32 cfg0;
	u32 cfg1;
	u32 cfg2;
	u32 cfg3;
	u32 cfg4;
	u16 bl_fix;
	u16 cur_bl;
	u8 video_mode;
};

static void dpu_sr_config(struct dpu_context *ctx);
static void dpu_clean_all(struct dpu_context *ctx);
static void dpu_layer(struct dpu_context *ctx,
		    struct sprd_layer_state *hwlayer);

struct dpu_enhance {
	u32 enhance_en;
	u32 hsv_cfg_copy;
	int frame_no;
	bool first_frame;
	bool cabc_bl_set;
	int cabc_bl_set_delay;
	bool mode_changed;
	bool need_scale;
	u8 skip_layer_index;
	u32 dpu_luts_paddr;
	u8 *dpu_luts_vaddr;
	u32 *lut_slp_vaddr;
	u32 *lut_gamma_vaddr;
	u32 *lut_hsv_vaddr;
	u32 *lut_lut3d_vaddr;
	u8 gamma_lut_index;
	u8 hsv_lut_index;
	u8 lut3d_index;
	int cabc_state;
	bool ctm_set;
	struct cm_cfg cm_copy;
	struct cm_cfg ctm_copy;
	struct slp_cfg slp_copy;
	struct gamma_lut gamma_copy;
	struct epf_cfg epf_copy;
	struct rgb_integrate_arr rgb_arr_copy;
	struct hsv_params hsv_offset_copy;
	struct hsv_luts hsv_lut_copy;
	struct threed_lut lut3d_copy;
	struct ud_cfg ud_copy;
	struct luts_typeindex typeindex_cpy;
	struct lut_base_addrs lut_addrs_cpy;
	struct cabc_para cabc_para;
	struct backlight_device *bl_dev;
	struct device_node *g_np;
};

static void dpu_clean_all(struct dpu_context *ctx);
static void dpu_layer(struct dpu_context *ctx,
		    struct sprd_layer_state *hwlayer);
static void dpu_enhance_reload(struct dpu_context *ctx);
static int dpu_cabc_trigger(struct dpu_context *ctx);

static void dpu_version(struct dpu_context *ctx)
{
	ctx->version = "dpu-r6p0";
}

static void dpu_dump(struct dpu_context *ctx)
{
	u32 *reg = (u32 *)ctx->base;
	int i;

	pr_info("      0          4          8          C\n");
	for (i = 0; i < 256; i += 4) {
		pr_info("%04x: 0x%08x 0x%08x 0x%08x 0x%08x\n",
			i * 4, reg[i], reg[i + 1], reg[i + 2], reg[i + 3]);
	}
}

static u32 check_mmu_isr(struct dpu_context *ctx, u32 reg_val)
{
	u32 mmu_mask = BIT_DPU_INT_MMU_VAOR_RD_MASK |
			BIT_DPU_INT_MMU_VAOR_WR_MASK |
			BIT_DPU_INT_MMU_INV_RD_MASK |
			BIT_DPU_INT_MMU_INV_WR_MASK |
			BIT_DPU_INT_MMU_UNS_RD_MASK |
			BIT_DPU_INT_MMU_UNS_WR_MASK |
			BIT_DPU_INT_MMU_PAOR_RD_MASK |
			BIT_DPU_INT_MMU_PAOR_WR_MASK;
	u32 val = reg_val & mmu_mask;

	if (val) {
		pr_err("--- iommu interrupt err: 0x%04x ---\n", val);

		pr_err("iommu0 invalid read error, addr: 0x%08x\n",
			DPU_REG_RD(ctx->base + REG_DPU_MMU_INV_ADDR_RD));
		pr_err("iommu1 invalid read error, addr: 0x%08x\n",
			DPU_REG_RD(ctx->base + REG_DPU_MMU1_INV_ADDR_RD));
		pr_err("iommu0 invalid write error, addr: 0x%08x\n",
			DPU_REG_RD(ctx->base + REG_DPU_MMU_INV_ADDR_WR));
		pr_err("iommu1 invalid write error, addr: 0x%08x\n",
			DPU_REG_RD(ctx->base + REG_DPU_MMU1_INV_ADDR_WR));
		pr_err("iommu0 unsecurity read error, addr: 0x%08x\n",
			DPU_REG_RD(ctx->base + REG_DPU_MMU_UNS_ADDR_RD));
		pr_err("iommu1 unsecurity read error, addr: 0x%08x\n",
			DPU_REG_RD(ctx->base + REG_DPU_MMU1_UNS_ADDR_RD));
		pr_err("iommu0 unsecurity  write error, addr: 0x%08x\n",
			DPU_REG_RD(ctx->base + REG_DPU_MMU_UNS_ADDR_WR));
		pr_err("iommu1 unsecurity  write error, addr: 0x%08x\n",
			DPU_REG_RD(ctx->base + REG_DPU_MMU1_UNS_ADDR_WR));

		pr_err("BUG: iommu failure at %s:%d/%s()!\n",
			__FILE__, __LINE__, __func__);

		dpu_dump(ctx);

		/* panic("iommu panic\n"); */
	}

	return val;
}

static u32 dpu_isr(struct dpu_context *ctx)
{
	struct dpu_enhance *enhance = ctx->enhance;
	struct sprd_dpu *dpu =
		(struct sprd_dpu *)container_of(ctx, struct sprd_dpu, ctx);
	u32 reg_val, int_mask = 0;
	u32 mmu_reg_val, mmu_int_mask = 0;
	u32 mmu1_reg_val, mmu1_int_mask = 0;
	static int64_t gap;
	struct timespec64 ts;
	int64_t time;

	reg_val = DPU_REG_RD(ctx->base + REG_DPU_INT_STS);
	mmu_reg_val = DPU_REG_RD(ctx->base + REG_DPU_MMU_INT_STS);
	mmu1_reg_val = DPU_REG_RD(ctx->base + REG_DPU_MMU1_INT_STS);

	ktime_get_real_ts64(&ts);
	time = ts.tv_sec * 1000000 + ts.tv_nsec * 1000 / 1000000;
	gap = time - gap;

	if (gap < 10)
		pr_err("int value check : reg int sts value is 0x%x, gap value is %d\n",
				reg_val, gap);

	int_mask = reg_val & (BIT_DPU_INT_FBC_PLD_ERR |
				BIT_DPU_INT_FBC_HDR_ERR | BIT_DPU_INT_ERR);
	DPU_REG_WR(ctx->base + REG_DPU_INT_CLR, reg_val);
	DPU_REG_CLR(ctx->base + REG_DPU_INT_EN, int_mask);

	/* clear & disable mmu0 int */
	mmu_int_mask |= check_mmu_isr(ctx, mmu_reg_val);
	DPU_REG_WR(ctx->base + REG_DPU_MMU_INT_CLR, mmu_reg_val);
	DPU_REG_CLR(ctx->base + REG_DPU_MMU_INT_EN, mmu_int_mask);

	/* clear & disable mmu1 int */
	mmu1_int_mask |= check_mmu_isr(ctx, mmu1_reg_val);
	DPU_REG_WR(ctx->base + REG_DPU_MMU1_INT_CLR, mmu1_reg_val);
	DPU_REG_CLR(ctx->base + REG_DPU_MMU1_INT_EN, mmu1_int_mask);

	/* dpu vsync isr */
	if (reg_val & BIT_DPU_INT_VSYNC) {
		drm_crtc_handle_vblank(&dpu->crtc->base);

		/* write back feature */
		if ((ctx->vsync_count == ctx->max_vsync_count) && ctx->wb_en)
			schedule_work(&ctx->wb_work);

		/* cabc update backlight */
		if (enhance->cabc_bl_set)
			schedule_work(&ctx->cabc_bl_update);

		ctx->vsync_count++;
	}

	/* dpu update done isr */
	if (reg_val & BIT_DPU_INT_LAY_REG_UPDATE_DONE) {
		/* dpu dvfs feature */
		tasklet_schedule(&ctx->dvfs_task);

		ctx->evt_update = true;
		wake_up_interruptible_all(&ctx->wait_queue);
	}

	if (reg_val & BIT_DPU_INT_DPU_REG_UPDATE_DONE) {
		ctx->evt_all_regs_update = true;
		wake_up_interruptible_all(&ctx->wait_queue);
	}

	if (reg_val & BIT_DPU_INT_DPU_ALL_UPDATE_DONE) {
		ctx->evt_all_update = true;
		wake_up_interruptible_all(&ctx->wait_queue);
	}

	if (reg_val & BIT_DPU_INT_PQ_REG_UPDATE_DONE) {
		ctx->evt_pq_update = true;
		wake_up_interruptible_all(&ctx->wait_queue);
	}

	if (reg_val & BIT_DPU_INT_PQ_LUT_UPDATE_DONE) {
		ctx->evt_pq_lut_update = true;
		wake_up_interruptible_all(&ctx->wait_queue);
	}

	/* dpu stop done isr */
	if (reg_val & BIT_DPU_INT_DONE) {
		ctx->evt_stop = true;
		wake_up_interruptible_all(&ctx->wait_queue);
	}

	/* dpu write back done isr */
	if (reg_val & BIT_DPU_INT_WB_DONE_EN) {
		/*
		 * The write back is a time-consuming operation. If there is a
		 * flip occurs before write back done, the write back buffer is
		 * no need to display. Otherwise the new frame will be covered
		 * by the write back buffer, which is not what we wanted.
		 */
		if (ctx->wb_en && (ctx->vsync_count > ctx->max_vsync_count)) {
			ctx->wb_en = false;
			schedule_work(&ctx->wb_work);
			/*reg_val |= DPU_INT_FENCE_SIGNAL_REQUEST;*/
		}

		pr_debug("wb done\n");
	}

	/* dpu write back error isr */
	if (reg_val & BIT_DPU_INT_WB_ERR_EN) {
		pr_err("dpu write back fail\n");
		/*give a new chance to write back*/
		// if (ctx->max_vsync_count > 0) {
		ctx->wb_en = true;
		ctx->vsync_count = 0;
		// }
	}

	/* dpu afbc payload error isr */
	if (reg_val & BIT_DPU_INT_FBC_PLD_ERR)
		pr_err("dpu afbc payload error\n");

	/* dpu afbc header error isr */
	if (reg_val & BIT_DPU_INT_FBC_HDR_ERR)
		pr_err("dpu afbc header error\n");

	if (gap < 10) {
		reg_val = DPU_REG_RD(ctx->base + REG_DPU_INT_STS);
		pr_err("int value check : clr value is 0x%x, mmu0 int value is 0x%x, mmu1 int value is 0x%x\n",
				reg_val, mmu_int_mask, mmu1_int_mask);
	}

	gap = time;

	return reg_val;
}

static int dpu_wait_stop_done(struct dpu_context *ctx)
{
	int rc, i;
	u32 dpu_sts_21, dpu_sts_22;
	struct sprd_dpu *dpu =
		(struct sprd_dpu *)container_of(ctx, struct sprd_dpu, ctx);

	if (ctx->stopped)
		return 0;

	/* wait for stop done interrupt */
	rc = wait_event_interruptible_timeout(ctx->wait_queue, ctx->evt_stop,
					       msecs_to_jiffies(500));
	ctx->evt_stop = false;

	ctx->stopped = true;

	if (!rc)
		/* time out */
		pr_err("dpu wait for stop done time out!\n");

	for (i = 1; i <= 3000; i++) {
		dpu_sts_21 = DPU_REG_RD(ctx->base + REG_DPU_STS_21);
		dpu_sts_22 = DPU_REG_RD(ctx->base + REG_DPU_STS_22);
		if ((dpu_sts_21 & BIT(15)) ||
		  (dpu_sts_22 & BIT(15)))
			mdelay(1);
		else {
			pr_info("dpu is idle now\n");
			break;
		}

		if (i == 3000) {
			pr_err("wait for dpu read idle 3s timeout need to reset dpu\n");
			dpu->glb->reset(ctx);
			break;
		}
	}

	return 0;
}

static int dpu_wait_update_done(struct dpu_context *ctx)
{
	int rc;

	ctx->evt_update = false;
	if (!ctx->stopped)
		DPU_REG_SET(ctx->base + REG_DPU_CTRL, BIT(4));
	else
		DPU_REG_SET(ctx->base + REG_DPU_CTRL, BIT(0) | BIT(4));

	/* wait for reg update done interrupt */
	rc = wait_event_interruptible_timeout(ctx->wait_queue, ctx->evt_update,
					       msecs_to_jiffies(500));

	if (!rc) {
		/* time out */
		pr_err("dpu wait for reg update done time out!\n");
		return -1;
	}

	return 0;
}

static int dpu_wait_all_regs_update_done(struct dpu_context *ctx)
{
	int rc;

	/* clear the event flag before wait */
	ctx->evt_all_regs_update = false;

	/* wait for reg update done interrupt */
	rc = wait_event_interruptible_timeout(ctx->wait_queue,
			ctx->evt_all_regs_update, msecs_to_jiffies(500));

	if (!rc) {
		/* time out */
		pr_err("dpu wait for all regs update done time out!\n");
		return -1;
	}

	return 0;
}

static int dpu_wait_pq_lut_reg_update_done(struct dpu_context *ctx)
{
	int rc;

	/* clear the event flag before wait */
	ctx->evt_pq_lut_update = false;
	ctx->evt_pq_update = false;

	/* wait for reg update done interrupt */
	rc = wait_event_interruptible_timeout(ctx->wait_queue,
			ctx->evt_pq_lut_update && ctx->evt_pq_update,
			msecs_to_jiffies(500));
	if (!rc) {
		/* time out */
		pr_err("dpu wait for all luts update done time out!\n");
		return -1;
	}

	return 0;
}

static int dpu_wait_pq_update_done(struct dpu_context *ctx)
{
	int rc;

	/* clear the event flag before wait */
	ctx->evt_pq_update = false;

	/* wait for pq reg update done interrupt */
	rc = wait_event_interruptible_timeout(ctx->wait_queue, ctx->evt_pq_update,
						msecs_to_jiffies(500));

	if (!rc) {
		/* time out */
		pr_err("dpu wait for pq reg update done time out!\n");
		return -1;
	}

	return 0;

}

static int dpu_wait_all_update_done(struct dpu_context *ctx)
{
	int rc;

	/* clear the event flag before wait */
	ctx->evt_all_update = false;

	/* wait for reg update done interrupt */
	rc = wait_event_interruptible_timeout(ctx->wait_queue, ctx->evt_all_update,
					       msecs_to_jiffies(500));

	if (!rc) {
		/* time out */
		pr_err("dpu wait for reg update done time out!\n");
		return -1;
	}

	return 0;
}

static void dpu_stop(struct dpu_context *ctx)
{
	if (ctx->if_type == SPRD_DPU_IF_DPI)
		DPU_REG_SET(ctx->base + REG_DPU_CTRL, BIT_DPU_STOP);

	dpu_wait_stop_done(ctx);
	ctx->evt_update = false;
	pr_info("dpu stop\n");
}

static void dpu_run(struct dpu_context *ctx)
{
	DPU_REG_SET(ctx->base + REG_DPU_CTRL, BIT(4) | BIT(0));
	ctx->stopped = false;

	pr_info("dpu run\n");

	if (ctx->if_type == SPRD_DPU_IF_EDPI) {
		/*
		 * If the panel read GRAM speed faster than
		 * DSI write GRAM speed, it will display some
		 * mass on screen when backlight on. So wait
		 * a TE period after flush the GRAM.
		 */
		if (!ctx->panel_ready) {
			dpu_wait_stop_done(ctx);
			/* wait for TE again */
			mdelay(20);
			ctx->panel_ready = true;
		}
	}
}

static void dpu_cabc_work_func(struct work_struct *data)
{
	struct dpu_context *ctx =
		container_of(data, struct dpu_context, cabc_work);

	down(&ctx->cabc_lock);
	if (ctx->enabled) {
		dpu_cabc_trigger(ctx);
		DPU_REG_WR(ctx->base + REG_ENHANCE_UPDATE, BIT(0));
		//dpu_wait_pq_update_done(ctx);
	}
	up(&ctx->cabc_lock);
}

static void dpu_cabc_bl_update_func(struct work_struct *data)
{
	struct dpu_context *ctx =
		container_of(data, struct dpu_context, cabc_bl_update);
	struct dpu_enhance *enhance = ctx->enhance;
	struct sprd_backlight *bl = bl_get_data(enhance->bl_dev);

	if (enhance->bl_dev) {
		if (enhance->cabc_state == CABC_WORKING) {
			sprd_backlight_normalize_map(enhance->bl_dev, &enhance->cabc_para.cur_bl);
			bl->cabc_en = true;
			bl->cabc_level = enhance->cabc_para.bl_fix *
					enhance->cabc_para.cur_bl / CABC_BL_COEF;
			bl->cabc_refer_level = enhance->cabc_para.cur_bl;
			sprd_cabc_backlight_update(enhance->bl_dev);
		} else {
			bl->cabc_en = false;
			backlight_update_status(enhance->bl_dev);
		}
	}

	enhance->cabc_bl_set = false;
}

static void dpu_wb_trigger(struct dpu_context *ctx, u8 count, bool debug)
{
	int mode_width  = DPU_REG_RD(ctx->base + REG_BLEND_SIZE) & 0xFFFF;
	int mode_height = DPU_REG_RD(ctx->base + REG_BLEND_SIZE) >> 16;

	if (ctx->wb_size_changed) {
		ctx->wb_layer.dst_w = mode_width;
		ctx->wb_layer.dst_h = mode_height;
		ctx->wb_layer.src_w = mode_width;
		ctx->wb_layer.src_h = mode_height;
		ctx->wb_layer.pitch[0] = ALIGN(mode_width, 16) * 4;
		ctx->wb_layer.fbc_hsize_r = XFBC8888_HEADER_SIZE(mode_width,
							mode_height) / 128;
		DPU_REG_WR(ctx->base + REG_WB_PITCH, ALIGN((mode_width), 16));
		if (ctx->wb_xfbc_en) {
			DPU_REG_WR(ctx->base + REG_WB_CFG, (ctx->wb_layer.fbc_hsize_r << 16) | BIT(0));
		} else {
			DPU_REG_WR(ctx->base + REG_WB_CFG, 0);
		}
	}

	DPU_REG_WR(ctx->base + REG_WB_PITCH, ctx->vm.hactive);

	if (debug) {
		/* writeback debug trigger */
		mode_width  = DPU_REG_RD(ctx->base + REG_BLEND_SIZE) & 0xFFFF;
		DPU_REG_WR(ctx->base + REG_WB_PITCH, ALIGN((mode_width), 16));
		DPU_REG_WR(ctx->base + REG_WB_CFG, 0);
	}

	if (debug || ctx->wb_size_changed) {
		dpu_wait_update_done(ctx);
		ctx->wb_size_changed = false;
	}

	if (debug)
		DPU_REG_WR(ctx->base + REG_WB_CTRL, BIT(1));
	else
		DPU_REG_SET(ctx->base + REG_WB_CTRL, BIT(0));

	pr_debug("write back trigger\n");
}

static void dpu_wb_flip(struct dpu_context *ctx)
{
	dpu_clean_all(ctx);
	dpu_layer(ctx, &ctx->wb_layer);

	dpu_wait_update_done(ctx);
	pr_debug("write back flip\n");
}

static void dpu_wb_work_func(struct work_struct *data)
{
	struct dpu_context *ctx =
		container_of(data, struct dpu_context, wb_work);

	down(&ctx->lock);

	if (!ctx->enabled) {
		up(&ctx->lock);
		pr_err("dpu is not initialized\n");
		return;
	}

	if (ctx->flip_pending) {
		up(&ctx->lock);
		pr_warn("dpu flip is disabled\n");
		return;
	}

	if (ctx->wb_pending) {
		up(&ctx->lock);
		pr_warn("display mode is going on changing\n");
		return;
	}

	if ((ctx->wb_en && (ctx->vsync_count > ctx->max_vsync_count)) || ctx->wb_size_changed)
		dpu_wb_trigger(ctx, 1, false);
	else if (!ctx->wb_en)
		dpu_wb_flip(ctx);

	up(&ctx->lock);
}

static int dpu_write_back_config(struct dpu_context *ctx)
{
	struct sprd_dpu *dpu =
		(struct sprd_dpu *)container_of(ctx, struct sprd_dpu, ctx);
	struct drm_device *drm = dpu->crtc->base.dev;
	int mode_width  = DPU_REG_RD(ctx->base + REG_BLEND_SIZE) & 0xFFFF;

	if (ctx->fastcall_en) {
		pr_info("widevine use fastcall, not support write back\n");
		return 0;
	}

	if (ctx->wb_configed) {
		DPU_REG_WR(ctx->base + REG_WB_BASE_ADDR, ctx->wb_addr_p);
		DPU_REG_WR(ctx->base + REG_WB_PITCH, ALIGN((mode_width), 16));
		if (ctx->wb_xfbc_en)
			DPU_REG_WR(ctx->base + REG_WB_CFG, ((ctx->wb_layer.fbc_hsize_r << 16) | BIT(0)));
		else
			DPU_REG_WR(ctx->base + REG_WB_CFG, 0);
		pr_debug("write back has configed\n");
		return 0;
	}

	ctx->wb_buf_size = XFBC8888_BUFFER_SIZE(ctx->vm.hactive,
						ctx->vm.vactive);
	pr_info("use wb_reserved memory for writeback, size:0x%zx\n", ctx->wb_buf_size);
	ctx->wb_addr_v = dma_alloc_wc(drm->dev, ctx->wb_buf_size, &ctx->wb_addr_p, GFP_KERNEL);
	if (!ctx->wb_addr_p) {
		ctx->max_vsync_count = 0;
		return -ENOMEM;
	}

	ctx->wb_xfbc_en = 1;
	ctx->wb_layer.index = 7;
	ctx->wb_layer.planes = 1;
	ctx->wb_layer.alpha = 0xff;
	ctx->wb_layer.format = DRM_FORMAT_ABGR8888;
	ctx->wb_layer.addr[0] = ctx->wb_addr_p;
	DPU_REG_WR(ctx->base + REG_WB_BASE_ADDR, ctx->wb_addr_p);
	DPU_REG_WR(ctx->base + REG_WB_PITCH, ALIGN((mode_width), 16));
	if (ctx->wb_xfbc_en) {
		ctx->wb_layer.xfbc = ctx->wb_xfbc_en;
		DPU_REG_WR(ctx->base + REG_WB_CFG, ((ctx->wb_layer.fbc_hsize_r << 16) | BIT(0)));
	}

	ctx->max_vsync_count = 0;
	ctx->wb_configed = true;

	INIT_WORK(&ctx->wb_work, dpu_wb_work_func);

	return 0;
}

static int dpu_config_dsc_param(struct dpu_context *ctx)
{
	u32 reg_val;
	struct sprd_dpu *dpu =
		(struct sprd_dpu *)container_of(ctx, struct sprd_dpu, ctx);
	struct sprd_panel *panel =
		(struct sprd_panel *)container_of(dpu->dsi->panel, struct sprd_panel, base);
	struct videomode vm;

	if (dpu->crtc->fps_mode_changed && (dpu->mode.type == DRM_MODE_TYPE_DRIVER)) {
		drm_display_mode_to_videomode(&panel->info.mode, &vm);
	} else if (dpu->mode.type & DRM_MODE_TYPE_USERDEF) {
		drm_display_mode_to_videomode(&panel->info.mode, &vm);
	} else {
		drm_display_mode_to_videomode(&dpu->actual_mode, &vm);
	}

	if (panel->info.dual_dsi_en) {
		reg_val = (vm.vactive << 16) |
			((vm.hactive >> 1)  << 0);
		DPU_REG_WR(ctx->base + DSC_REG(REG_DSC_PIC_SIZE), reg_val);
	} else {
		reg_val = (vm.vactive << 16) |
			(vm.hactive << 0);
		DPU_REG_WR(ctx->base + DSC_REG(REG_DSC_PIC_SIZE), reg_val);
	}
	if (panel->info.dual_dsi_en) {
		reg_val = ((vm.hsync_len >> 1) << 0) |
			((vm.hback_porch  >> 1) << 8) |
			((vm.hfront_porch >> 1) << 20);
		DPU_REG_WR(ctx->base + DSC_REG(REG_DSC_H_TIMING), reg_val);
	} else {
		reg_val = (vm.hsync_len << 0) |
			(vm.hback_porch  << 8) |
			(vm.hfront_porch << 20);
		DPU_REG_WR(ctx->base + DSC_REG(REG_DSC_H_TIMING), reg_val);
	}
	reg_val = (vm.vsync_len << 0) |
			(vm.vback_porch  << 8) |
			(vm.vfront_porch << 20);
	DPU_REG_WR(ctx->base + DSC_REG(REG_DSC_V_TIMING), reg_val);

	DPU_REG_WR(ctx->base + DSC_REG(REG_DSC_GRP_SIZE), ctx->dsc_cfg.reg.dsc_grp_size);
	DPU_REG_WR(ctx->base + DSC_REG(REG_DSC_SLICE_SIZE), ctx->dsc_cfg.reg.dsc_slice_size);

	DPU_REG_WR(ctx->base + DSC_REG(REG_DSC_CFG0), ctx->dsc_cfg.reg.dsc_cfg0);
	DPU_REG_WR(ctx->base + DSC_REG(REG_DSC_CFG1), ctx->dsc_cfg.reg.dsc_cfg1);
	DPU_REG_WR(ctx->base + DSC_REG(REG_DSC_CFG2), ctx->dsc_cfg.reg.dsc_cfg2);
	DPU_REG_WR(ctx->base + DSC_REG(REG_DSC_CFG3), ctx->dsc_cfg.reg.dsc_cfg3);
	DPU_REG_WR(ctx->base + DSC_REG(REG_DSC_CFG4), ctx->dsc_cfg.reg.dsc_cfg4);
	DPU_REG_WR(ctx->base + DSC_REG(REG_DSC_CFG5), ctx->dsc_cfg.reg.dsc_cfg5);
	DPU_REG_WR(ctx->base + DSC_REG(REG_DSC_CFG6), ctx->dsc_cfg.reg.dsc_cfg6);
	DPU_REG_WR(ctx->base + DSC_REG(REG_DSC_CFG7), ctx->dsc_cfg.reg.dsc_cfg7);
	DPU_REG_WR(ctx->base + DSC_REG(REG_DSC_CFG8), ctx->dsc_cfg.reg.dsc_cfg8);
	DPU_REG_WR(ctx->base + DSC_REG(REG_DSC_CFG9), ctx->dsc_cfg.reg.dsc_cfg9);
	DPU_REG_WR(ctx->base + DSC_REG(REG_DSC_CFG10), ctx->dsc_cfg.reg.dsc_cfg10);
	DPU_REG_WR(ctx->base + DSC_REG(REG_DSC_CFG11), ctx->dsc_cfg.reg.dsc_cfg11);
	DPU_REG_WR(ctx->base + DSC_REG(REG_DSC_CFG12), ctx->dsc_cfg.reg.dsc_cfg12);
	DPU_REG_WR(ctx->base + DSC_REG(REG_DSC_CFG13), ctx->dsc_cfg.reg.dsc_cfg13);
	DPU_REG_WR(ctx->base + DSC_REG(REG_DSC_CFG14), ctx->dsc_cfg.reg.dsc_cfg14);
	DPU_REG_WR(ctx->base + DSC_REG(REG_DSC_CFG15), ctx->dsc_cfg.reg.dsc_cfg15);
	DPU_REG_WR(ctx->base + DSC_REG(REG_DSC_CFG16), ctx->dsc_cfg.reg.dsc_cfg16);

	if (panel->info.dual_dsi_en) {
		reg_val = (vm.vactive << 16) |
			((vm.hactive >> 1) << 0);
		DPU_REG_WR(ctx->base + DSC1_REG(REG_DSC_PIC_SIZE), reg_val);
		reg_val = ((vm.hsync_len >> 1) << 0) |
			((vm.hback_porch  >> 1) << 8) |
			((vm.hfront_porch >> 1) << 20);
		DPU_REG_WR(ctx->base + DSC1_REG(REG_DSC_H_TIMING), reg_val);
		reg_val = (vm.vsync_len << 0) |
			(vm.vback_porch  << 8) |
			(vm.vfront_porch << 20);
		DPU_REG_WR(ctx->base + DSC1_REG(REG_DSC_V_TIMING), reg_val);
		DPU_REG_WR(ctx->base + DSC1_REG(REG_DSC_GRP_SIZE), ctx->dsc_cfg.reg.dsc_grp_size);
		DPU_REG_WR(ctx->base + DSC1_REG(REG_DSC_SLICE_SIZE),
							ctx->dsc_cfg.reg.dsc_slice_size);
		DPU_REG_WR(ctx->base + DSC1_REG(REG_DSC_CFG0), ctx->dsc_cfg.reg.dsc_cfg0);
		DPU_REG_WR(ctx->base + DSC1_REG(REG_DSC_CFG1), ctx->dsc_cfg.reg.dsc_cfg1);
		DPU_REG_WR(ctx->base + DSC1_REG(REG_DSC_CFG2), ctx->dsc_cfg.reg.dsc_cfg2);
		DPU_REG_WR(ctx->base + DSC1_REG(REG_DSC_CFG3), ctx->dsc_cfg.reg.dsc_cfg3);
		DPU_REG_WR(ctx->base + DSC1_REG(REG_DSC_CFG4), ctx->dsc_cfg.reg.dsc_cfg4);
		DPU_REG_WR(ctx->base + DSC1_REG(REG_DSC_CFG5), ctx->dsc_cfg.reg.dsc_cfg5);
		DPU_REG_WR(ctx->base + DSC1_REG(REG_DSC_CFG6), ctx->dsc_cfg.reg.dsc_cfg6);
		DPU_REG_WR(ctx->base + DSC1_REG(REG_DSC_CFG7), ctx->dsc_cfg.reg.dsc_cfg7);
		DPU_REG_WR(ctx->base + DSC1_REG(REG_DSC_CFG8), ctx->dsc_cfg.reg.dsc_cfg8);
		DPU_REG_WR(ctx->base + DSC1_REG(REG_DSC_CFG9), ctx->dsc_cfg.reg.dsc_cfg9);
		DPU_REG_WR(ctx->base + DSC1_REG(REG_DSC_CFG10), ctx->dsc_cfg.reg.dsc_cfg10);
		DPU_REG_WR(ctx->base + DSC1_REG(REG_DSC_CFG11), ctx->dsc_cfg.reg.dsc_cfg11);
		DPU_REG_WR(ctx->base + DSC1_REG(REG_DSC_CFG12), ctx->dsc_cfg.reg.dsc_cfg12);
		DPU_REG_WR(ctx->base + DSC1_REG(REG_DSC_CFG13), ctx->dsc_cfg.reg.dsc_cfg13);
		DPU_REG_WR(ctx->base + DSC1_REG(REG_DSC_CFG14), ctx->dsc_cfg.reg.dsc_cfg14);
		DPU_REG_WR(ctx->base + DSC1_REG(REG_DSC_CFG15), ctx->dsc_cfg.reg.dsc_cfg15);
		DPU_REG_WR(ctx->base + DSC1_REG(REG_DSC_CFG16), ctx->dsc_cfg.reg.dsc_cfg16);
		if (dpu->dsi->ctx.work_mode == DSI_MODE_CMD)
			DPU_REG_WR(ctx->base + DSC1_REG(REG_DSC_CTRL), 0x2000010b);
		else
			DPU_REG_WR(ctx->base + DSC1_REG(REG_DSC_CTRL), 0x2000000b);
	}

	if (dpu->dsi->ctx.work_mode == DSI_MODE_CMD)
		DPU_REG_WR(ctx->base + DSC_REG(REG_DSC_CTRL), 0x2000010b);
	else
		DPU_REG_WR(ctx->base + DSC_REG(REG_DSC_CTRL), 0x2000000b);

	return 0;
}

static int dpu_luts_alloc(struct dpu_context *ctx)
{
	static bool is_configed;
	unsigned long *vaddr;
	struct sprd_dpu *dpu = (struct sprd_dpu *)container_of(ctx, struct sprd_dpu, ctx);
	struct drm_device *drm = dpu->crtc->base.dev;
	struct dpu_enhance *enhance = ctx->enhance;
	dma_addr_t paddr;

	if (is_configed) {
		pr_debug("dpu luts buffer has been configed\n");
		return 0;
	}

	vaddr = dma_alloc_wc(drm->dev, DPU_LUTS_SIZE+DPU_LUTS_GAMMA_OFFSET, &paddr,
						 GFP_KERNEL | __GFP_NOWARN);
	if (!vaddr) {
		pr_err("failed to allocate buffer with size %u\n",
			DPU_LUTS_SIZE+DPU_LUTS_GAMMA_OFFSET);
		return -ENOMEM;
	}

	enhance->dpu_luts_paddr = (u32)paddr;
	enhance->dpu_luts_vaddr = (u8 *)vaddr;
	enhance->lut_slp_vaddr = (u32 *)(enhance->dpu_luts_vaddr + DPU_LUTS_SLP_OFFSET);
	enhance->lut_gamma_vaddr = (u32 *)(enhance->dpu_luts_vaddr + DPU_LUTS_GAMMA_OFFSET);
	enhance->lut_hsv_vaddr = (u32 *)(enhance->dpu_luts_vaddr + DPU_LUTS_HSV_OFFSET);
	enhance->lut_lut3d_vaddr = (u32 *)(enhance->dpu_luts_vaddr + DPU_LUTS_LUT3D_OFFSET);

	is_configed = true;

	return 0;
}

static void dpu_dvfs_task_func(unsigned long data)
{
	struct dpu_context *ctx = (struct dpu_context *)data;
	struct layer_info layer, layers[8];
	int i, j, max_x, max_y, min_x, min_y;
	int layer_en, max, maxs[8], count = 0;
	u32 dvfs_freq, reg_val;

	if (!ctx->enabled) {
		pr_err("dpu is not initialized\n");
		return;
	}

	/*
	 * Count the current total number of active layers
	 * and the corresponding pos_x, pos_y, size_x and size_y.
	 */
	for (i = 0; i < 8; i++) {
		layer_en = DPU_REG_RD(ctx->base + REG_LAYER_ENABLE) & BIT(i);
		if (layer_en) {
			reg_val = DPU_REG_RD(ctx->base + DPU_LAY_REG(REG_LAY_POS, i));
			layers[count].dst_x = reg_val & 0xffff;
			layers[count].dst_y = reg_val >> 16;

			reg_val = DPU_REG_RD(ctx->base + DPU_LAY_REG(REG_LAY_DES_SIZE, i));
			layers[count].dst_w = reg_val & 0xffff;
			layers[count].dst_h = reg_val >> 16;
			count++;
		}
	}

	/*
	 * Calculate the number of overlaps between each
	 * layer with other layers, not include itself.
	 */
	for (i = 0; i < count; i++) {
		layer.dst_x = layers[i].dst_x;
		layer.dst_y = layers[i].dst_y;
		layer.dst_w = layers[i].dst_w;
		layer.dst_h = layers[i].dst_h;
		maxs[i] = 1;

		for (j = 0; j < count; j++) {
			if (layer.dst_x + layer.dst_w > layers[j].dst_x &&
				layers[j].dst_x + layers[j].dst_w > layer.dst_x &&
				layer.dst_y + layer.dst_h > layers[j].dst_y &&
				layers[j].dst_y + layers[j].dst_h > layer.dst_y &&
				i != j) {
				max_x = max(layers[i].dst_x, layers[j].dst_x);
				max_y = max(layers[i].dst_y, layers[j].dst_y);
				min_x = min(layers[i].dst_x + layers[i].dst_w,
					layers[j].dst_x + layers[j].dst_w);
				min_y = min(layers[i].dst_y + layers[i].dst_h,
					layers[j].dst_y + layers[j].dst_h);

				layer.dst_x = max_x;
				layer.dst_y = max_y;
				layer.dst_w = min_x - max_x;
				layer.dst_h = min_y - max_y;

				maxs[i]++;
			}
		}
	}

	/* take the maximum number of overlaps */
	max = maxs[0];
	for (i = 1; i < count; i++) {
		if (maxs[i] > max)
			max = maxs[i];
	}

	/*
	 * Determine which frequency to use based on the
	 * maximum number of overlaps.
	 * Every IP here may be different, so need to modify it
	 * according to the actual dpu core clock.
	 */
	if (max <= 2)
		dvfs_freq = 409600000;
	else if (max == 3)
		dvfs_freq = 512000000;
	else if (max == 4)
		dvfs_freq = 614400000;
	else
		dvfs_freq = 614400000;

	if(ctx->vrr_enabled)
		dvfs_freq = 614400000;

	dpu_dvfs_notifier_call_chain(&dvfs_freq);
}

static void dpu_dvfs_task_init(struct dpu_context *ctx)
{
	static int need_config = 1;

	if (!need_config)
		return;

	need_config = 0;
	tasklet_init(&ctx->dvfs_task, dpu_dvfs_task_func,
			(unsigned long)ctx);
}

static int dpu_init(struct dpu_context *ctx)
{
	u32 reg_val, size;
	u32 dvfs_freq;
	int ret;
	struct sprd_dpu *dpu = (struct sprd_dpu *)container_of(ctx, struct sprd_dpu, ctx);
	struct sprd_panel *panel =
		(struct sprd_panel *)container_of(dpu->dsi->panel, struct sprd_panel, base);

	struct dpu_enhance *enhance = ctx->enhance;

	calc_dsc_params(&ctx->dsc_init);

	if (panel->info.dual_dsi_en)
		DPU_REG_WR(ctx->base + REG_DPU_MODE, BIT(0));

	if (panel->info.dsc_en)
		dpu_config_dsc_param(ctx);

	/* set bg color */
	DPU_REG_WR(ctx->base + REG_BG_COLOR, 0x00);

	/* set dpu output size */
	size = (ctx->vm.vactive << 16) | ctx->vm.hactive;
	DPU_REG_WR(ctx->base + REG_PANEL_SIZE, size);
	DPU_REG_WR(ctx->base + REG_BLEND_SIZE, size);

	DPU_REG_WR(ctx->base + REG_DPU_CFG0, 0x00);
	if ((dpu->dsi->ctx.work_mode == DSI_MODE_CMD) && panel->info.dsc_en) {
		DPU_REG_SET(ctx->base + REG_DPU_CFG0, BIT(1));
		ctx->is_single_run = true;
	}

	reg_val = (ctx->qos_cfg.awqos_high << 12) |
		(ctx->qos_cfg.awqos_low << 8) |
		(ctx->qos_cfg.arqos_high << 4) |
		(ctx->qos_cfg.arqos_low) | BIT(18) | BIT(22) | BIT(23);
	DPU_REG_WR(ctx->base + REG_DPU_CFG1, reg_val);;
	if (ctx->stopped)
		dpu_clean_all(ctx);

	DPU_REG_WR(ctx->base + REG_DPU_INT_CLR, 0xffff);

	ret = dpu_luts_alloc(ctx);
	if (ret)
		pr_err("DPU lUTS table alloc buffer fail\n");
	if (!ret)
		dpu_enhance_reload(ctx);

	dpu_write_back_config(ctx);

	dpu_dvfs_task_init(ctx);

	enhance->frame_no = 0;

	if (ctx->fastcall_en) {
		ret = trusty_fast_call32(NULL, SMC_FC_DPU_FW_SET_SECURITY, FW_ATTR_SECURE, 0, 0);
		if (ret)
			pr_err("Trusty fastcall set firewall failed, ret = %d\n", ret);
	}

	if(ctx->vrr_enabled){
		dvfs_freq = 614400000;
		dpu_dvfs_notifier_call_chain(&dvfs_freq);
	}

	return 0;
}

static void dpu_fini(struct dpu_context *ctx)
{
	int ret;

	DPU_REG_WR(ctx->base + REG_DPU_INT_EN, 0x00);
	DPU_REG_WR(ctx->base + REG_DPU_INT_CLR, 0xff);

	if (ctx->fastcall_en) {
		ret = trusty_fast_call32(NULL, SMC_FC_DPU_FW_SET_SECURITY, FW_ATTR_NON_SECURE, 0, 0);
		if (ret)
			pr_err("Trusty fastcall clear firewall failed, ret = %d\n", ret);
	}

	ctx->panel_ready = false;
}

enum {
	DPU_LAYER_FORMAT_YUV422_2PLANE,
	DPU_LAYER_FORMAT_YUV420_2PLANE,
	DPU_LAYER_FORMAT_YUV420_3PLANE,
	DPU_LAYER_FORMAT_ARGB8888,
	DPU_LAYER_FORMAT_RGB565,
	DPU_LAYER_FORMAT_XFBC_ARGB8888 = 8,
	DPU_LAYER_FORMAT_XFBC_RGB565,
	DPU_LAYER_FORMAT_XFBC_YUV420,
	DPU_LAYER_FORMAT_MAX_TYPES,
};

enum {
	DPU_LAYER_ROTATION_0,
	DPU_LAYER_ROTATION_90,
	DPU_LAYER_ROTATION_180,
	DPU_LAYER_ROTATION_270,
	DPU_LAYER_ROTATION_0_M,
	DPU_LAYER_ROTATION_90_M,
	DPU_LAYER_ROTATION_180_M,
	DPU_LAYER_ROTATION_270_M,
};

static u32 to_dpu_rotation(u32 angle)
{
	u32 rot = DPU_LAYER_ROTATION_0;

	switch (angle) {
	case 0:
	case DRM_MODE_ROTATE_0:
		rot = DPU_LAYER_ROTATION_0;
		break;
	case DRM_MODE_ROTATE_90:
		rot = DPU_LAYER_ROTATION_90;
		break;
	case DRM_MODE_ROTATE_180:
		rot = DPU_LAYER_ROTATION_180;
		break;
	case DRM_MODE_ROTATE_270:
		rot = DPU_LAYER_ROTATION_270;
		break;
	case DRM_MODE_REFLECT_Y:
		rot = DPU_LAYER_ROTATION_180_M;
		break;
	case (DRM_MODE_REFLECT_Y | DRM_MODE_ROTATE_90):
		rot = DPU_LAYER_ROTATION_90_M;
		break;
	case DRM_MODE_REFLECT_X:
		rot = DPU_LAYER_ROTATION_0_M;
		break;
	case (DRM_MODE_REFLECT_X | DRM_MODE_ROTATE_90):
		rot = DPU_LAYER_ROTATION_270_M;
		break;
	default:
		pr_err("rotation convert unsupport angle (drm)= 0x%x\n", angle);
		break;
	}

	return rot;
}

static u32 dpu_img_ctrl(u32 format, u32 blending, u32 compression, u32 y2r_coef,
		u32 rotation)
{
	int reg_val = 0;
	/* layer enable */
	// reg_val |= BIT_DPU_LAY_EN;

	switch (format) {
	case DRM_FORMAT_BGRA8888:
		/* BGRA8888 -> ARGB8888 */
		reg_val |= BIT_DPU_LAY_DATA_ENDIAN_B3B2B1B0;
		if (compression)
			/* XFBC-ARGB8888 */
			reg_val |= (BIT_DPU_LAY_FORMAT_XFBC_ARGB8888);
		else
			reg_val |= (BIT_DPU_LAY_FORMAT_ARGB8888);
		break;
	case DRM_FORMAT_RGBX8888:
	case DRM_FORMAT_RGBA8888:
		/* RGBA8888 -> ABGR8888 */
		reg_val |= BIT_DPU_LAY_DATA_ENDIAN_B3B2B1B0;
		/* FALLTHRU */
	case DRM_FORMAT_ABGR8888:
		/* rb switch */
		reg_val |= BIT_DPU_LAY_RGB888_RB_SWITCH;
		/* FALLTHRU */
	case DRM_FORMAT_ARGB8888:
		if (compression)
			/* XFBC-ARGB8888 */
			reg_val |= (BIT_DPU_LAY_FORMAT_XFBC_ARGB8888);
		else
			reg_val |= (BIT_DPU_LAY_FORMAT_ARGB8888);
		break;
	case DRM_FORMAT_XBGR8888:
		/* rb switch */
		reg_val |= BIT_DPU_LAY_RGB888_RB_SWITCH;
		/* FALLTHRU */
	case DRM_FORMAT_XRGB8888:
		if (compression)
			/* XFBC-ARGB8888 */
			reg_val |= (BIT_DPU_LAY_FORMAT_XFBC_ARGB8888);
		else
			reg_val |= (BIT_DPU_LAY_FORMAT_ARGB8888);
		break;
	case DRM_FORMAT_BGR565:
		/* rb switch */
		reg_val |= BIT_DPU_LAY_RGB565_RB_SWITCH;
		/* FALLTHRU */
	case DRM_FORMAT_RGB565:
		if (compression)
			/* XFBC-RGB565 */
			reg_val |= (BIT_DPU_LAY_FORMAT_XFBC_RGB565);
		else
			reg_val |= (BIT_DPU_LAY_FORMAT_RGB565);
		break;
	case DRM_FORMAT_NV12:
		if (compression)
			/*2-Lane: Yuv420 */
			reg_val |= BIT_DPU_LAY_FORMAT_XFBC_YUV420;
		else
			reg_val |= BIT_DPU_LAY_FORMAT_YUV420_2PLANE;
		/*Y endian */
		reg_val |= BIT_DPU_LAY_DATA_ENDIAN_B0B1B2B3;
		/*UV endian */
		reg_val |= BIT_DPU_LAY_DATA_ENDIAN_B0B1B2B3;
		break;
	case DRM_FORMAT_NV21:
		if (compression)
			/*2-Lane: Yuv420 */
			reg_val |= BIT_DPU_LAY_FORMAT_XFBC_YUV420;
		else
			reg_val |= BIT_DPU_LAY_FORMAT_YUV420_2PLANE;
		/*Y endian */
		reg_val |= BIT_DPU_LAY_DATA_ENDIAN_B0B1B2B3;
		/*UV endian */
		reg_val |= BIT_DPU_LAY_DATA_ENDIAN_B3B2B1B0 << 2;
		break;
	case DRM_FORMAT_NV16:
		/*2-Lane: Yuv422 */
		reg_val |= BIT_DPU_LAY_FORMAT_YUV422_2PLANE;
		/*Y endian */
		reg_val |= BIT_DPU_LAY_DATA_ENDIAN_B3B2B1B0;
		/*UV endian */
		reg_val |= BIT_DPU_LAY_DATA_ENDIAN_B3B2B1B0 << 2;
		break;
	case DRM_FORMAT_NV61:
		/*2-Lane: Yuv422 */
		reg_val |= BIT_DPU_LAY_FORMAT_YUV422_2PLANE;
		/*Y endian */
		reg_val |= BIT_DPU_LAY_DATA_ENDIAN_B0B1B2B3;
		/*UV endian */
		reg_val |= BIT_DPU_LAY_DATA_ENDIAN_B0B1B2B3;
		break;
	case DRM_FORMAT_YUV420:
		reg_val |= BIT_DPU_LAY_FORMAT_YUV420_3PLANE;
		/*Y endian */
		reg_val |= BIT_DPU_LAY_DATA_ENDIAN_B0B1B2B3;
		/*UV endian */
		reg_val |= BIT_DPU_LAY_DATA_ENDIAN_B0B1B2B3;
		break;
	case DRM_FORMAT_YVU420:
                reg_val |= BIT_DPU_LAY_FORMAT_YUV420_3PLANE;
                /*Y endian */
                reg_val |= BIT_DPU_LAY_DATA_ENDIAN_B0B1B2B3;
                /*UV switch for YUV or RB switch for RGB888*/
                reg_val |= BIT_DPU_LAY_RGB888_RB_SWITCH;
                break;
	default:
		pr_err("error: invalid format %c%c%c%c\n", format,
						format >> 8,
						format >> 16,
						format >> 24);
		break;
	}

	switch (blending) {
	case DRM_MODE_BLEND_PIXEL_NONE:
		/* don't do blending, maybe RGBX */
		/* alpha mode select - layer alpha */
		reg_val |= BIT_DPU_LAY_LAYER_ALPHA;
		break;
	case DRM_MODE_BLEND_COVERAGE:
		/* alpha mode select - combo alpha */
		reg_val |= BIT_DPU_LAY_COMBO_ALPHA;
		/* blending mode select - normal mode */
		reg_val &= (~BIT_DPU_LAY_MODE_BLEND_NORMAL);
		break;
	case DRM_MODE_BLEND_PREMULTI:
		/* alpha mode select - combo alpha */
		reg_val |= BIT_DPU_LAY_COMBO_ALPHA;
		/* blending mode select - pre-mult mode */
		reg_val |= BIT_DPU_LAY_MODE_BLEND_PREMULT;
		break;
	default:
		/* alpha mode select - layer alpha */
		reg_val |= BIT_DPU_LAY_LAYER_ALPHA;
		break;
	}

	reg_val |= y2r_coef << 28;
	rotation = to_dpu_rotation(rotation);
	reg_val |= (rotation & 0x7) << 20;

	return reg_val;
}

static void dpu_clean_all(struct dpu_context *ctx)
{
	int i;

	for (i = 0; i < 8; i++)
		DPU_REG_WR(ctx->base + DPU_LAY_REG(REG_LAY_CTRL, i), 0x00);

	DPU_REG_WR(ctx->base + REG_LAYER_ENABLE, 0);
}

static void dpu_bgcolor(struct dpu_context *ctx, u32 color)
{

	if (ctx->if_type == SPRD_DPU_IF_EDPI)
		dpu_wait_stop_done(ctx);

	DPU_REG_WR(ctx->base + REG_BG_COLOR, color);

	dpu_clean_all(ctx);

	if (ctx->is_single_run) {
		DPU_REG_SET(ctx->base + REG_DPU_CTRL, BIT(4));
		DPU_REG_SET(ctx->base + REG_DPU_CTRL, BIT(0));
	} else if (ctx->if_type == SPRD_DPU_IF_EDPI) {
		DPU_REG_SET(ctx->base + REG_DPU_CTRL, BIT_DPU_RUN);
		ctx->stopped = false;
	} else if ((ctx->if_type == SPRD_DPU_IF_DPI) && !ctx->stopped) {
		dpu_wait_update_done(ctx);
	}
}

static void dpu_dma_request(struct dpu_context *ctx)
{
	DPU_REG_WR(ctx->base + REG_CABC_CFG5, 1);
}

static void dpu_layer(struct dpu_context *ctx,
		    struct sprd_layer_state *hwlayer)
{
	const struct drm_format_info *info;
	struct layer_reg tmp = {};
	u32 dst_size, src_size, offset, wd, rot, secure_val;
	int i;

	/* for secure displaying, just use layer 7 as secure layer */
	if ((hwlayer->secure_en || ctx->secure_debug) && ctx->fastcall_en)
		hwlayer->index = 7;

	offset = (hwlayer->dst_x & 0xffff) | ((hwlayer->dst_y) << 16);
	src_size = (hwlayer->src_w & 0xffff) | ((hwlayer->src_h) << 16);
	dst_size = (hwlayer->dst_w & 0xffff) | ((hwlayer->dst_h) << 16);

	if (hwlayer->pallete_en) {
		tmp.pos = offset;
		tmp.src_size = src_size;
		tmp.dst_size = dst_size;
		tmp.alpha = hwlayer->alpha;
		tmp.pallete = hwlayer->pallete_color;

		/* pallete layer enable */
		tmp.ctrl = 0x2004;
		pr_debug("dst_x = %d, dst_y = %d, dst_w = %d, dst_h = %d, pallete:%d\n",
				hwlayer->dst_x, hwlayer->dst_y,
				hwlayer->dst_w, hwlayer->dst_h, tmp.pallete);
	} else {
		if (src_size != dst_size) {
			rot = to_dpu_rotation(hwlayer->rotation);
			if ((rot == DPU_LAYER_ROTATION_90) || (rot == DPU_LAYER_ROTATION_270) ||
				(rot == DPU_LAYER_ROTATION_90_M) || (rot == DPU_LAYER_ROTATION_270_M))
				dst_size = (hwlayer->dst_h & 0xffff) | ((hwlayer->dst_w) << 16);
		}

		/*
		 * FIXME:
		 * Bypass dst and src same size scaling to avoid causing performance problem.
		 * Dst and src frame in same size will still be scaling in current version.
		 * It will be bypassed by digital ip in next version.
		 */
		if (!(hwlayer->secure_en || ctx->secure_debug)) {
			if (src_size != dst_size)
				tmp.ctrl = BIT(24);
			else
				tmp.ctrl = 0;
		}

		for (i = 0; i < hwlayer->planes; i++) {
			if (hwlayer->addr[i] % 16)
				pr_err("layer addr[%d] is not 16 bytes align, it's 0x%08x\n",
						i, hwlayer->addr[i]);
			tmp.addr[i] = hwlayer->addr[i];
		}

		tmp.pos = offset;
		tmp.src_size = src_size;
		tmp.dst_size = dst_size;
		tmp.crop_start = (hwlayer->src_y << 16) | hwlayer->src_x;
		tmp.alpha = hwlayer->alpha;

		info = drm_format_info(hwlayer->format);
		wd = info->cpp[0];
		if (wd == 0) {
			pr_err("layer[%d] bytes per pixel is invalid\n", hwlayer->index);
			return;
		}

		if (hwlayer->planes == 3)
			/* UV pitch is 1/2 of Y pitch*/
			tmp.pitch = (hwlayer->pitch[0] / wd) |
				(hwlayer->pitch[0] / wd << 15);
		else
			tmp.pitch = hwlayer->pitch[0] / wd;

		tmp.ctrl |= dpu_img_ctrl(hwlayer->format, hwlayer->blending,
				hwlayer->xfbc, hwlayer->y2r_coef, hwlayer->rotation);
	}

	if (!ctx->fastcall_en) {
		secure_val = DPU_REG_RD(ctx->base + REG_DPU_SECURE);
		if (hwlayer->secure_en || ctx->secure_debug) {
			if (!secure_val) {
				disp_ca_connect();
				mdelay(5);
			}
			ctx->tos_msg->cmd = TA_FIREWALL_SET;
			ctx->tos_msg->version = DPU_R6P0;
			disp_ca_write(ctx->tos_msg, sizeof(*ctx->tos_msg));
			disp_ca_wait_response();

			memcpy(ctx->tos_msg + 1, &tmp, sizeof(tmp));

			ctx->tos_msg->cmd = TA_REG_SET;
			ctx->tos_msg->version = DPU_R6P0;
			disp_ca_write(ctx->tos_msg, sizeof(*ctx->tos_msg) + sizeof(tmp));
			disp_ca_wait_response();
			return;
		} else if (secure_val) {
			ctx->tos_msg->cmd = TA_REG_CLR;
			ctx->tos_msg->version = DPU_R6P0;
			disp_ca_write(ctx->tos_msg, sizeof(*ctx->tos_msg));
			disp_ca_wait_response();
		}
	}

	for (i = 0; i < hwlayer->planes; i++)
		DPU_REG_WR(ctx->base + DPU_LAY_PLANE_ADDR(REG_LAY_BASE_ADDR,
					hwlayer->index, i), tmp.addr[i]);

	DPU_REG_WR(ctx->base + DPU_LAY_REG(REG_LAY_POS,
			hwlayer->index), tmp.pos);
	DPU_REG_WR(ctx->base + DPU_LAY_REG(REG_LAY_SRC_SIZE,
			hwlayer->index), tmp.src_size);
	DPU_REG_WR(ctx->base + DPU_LAY_REG(REG_LAY_DES_SIZE,
			hwlayer->index), tmp.dst_size);
	DPU_REG_WR(ctx->base + DPU_LAY_REG(REG_LAY_CROP_START,
			hwlayer->index), tmp.crop_start);
	DPU_REG_WR(ctx->base + DPU_LAY_REG(REG_LAY_ALPHA,
			hwlayer->index), tmp.alpha);
	DPU_REG_WR(ctx->base + DPU_LAY_REG(REG_LAY_PITCH,
			hwlayer->index), tmp.pitch);
	DPU_REG_WR(ctx->base + DPU_LAY_REG(REG_LAY_CTRL,
			hwlayer->index), tmp.ctrl);
	DPU_REG_SET(ctx->base + REG_LAYER_ENABLE,
			(1 << hwlayer->index));
	// DPU_REG_WR(ctx->base + DPU_LAY_REG(REG_LAY_PALLETE,
				// hwlayer->index), tmp.pallete);

	pr_debug("dst_x = %d, dst_y = %d, dst_w = %d, dst_h = %d\n",
				hwlayer->dst_x, hwlayer->dst_y,
				hwlayer->dst_w, hwlayer->dst_h);
	pr_debug("start_x = %d, start_y = %d, start_w = %d, start_h = %d\n",
				hwlayer->src_x, hwlayer->src_y,
				hwlayer->src_w, hwlayer->src_h);
}

static int dpu_vrr(struct dpu_context *ctx)
{
	struct sprd_dpu *dpu = (struct sprd_dpu *)container_of(ctx,
			struct sprd_dpu, ctx);
	struct sprd_panel *panel =
		(struct sprd_panel *)container_of(dpu->dsi->panel, struct sprd_panel, base);
	u32 reg_val;

	dpu_stop(ctx);
	reg_val = (ctx->vm.vsync_len << 0) |
		(ctx->vm.vback_porch << 8) |
		(ctx->vm.vfront_porch << 20);
	DPU_REG_WR(ctx->base + REG_DPI_V_TIMING, reg_val);

	reg_val = (ctx->vm.hsync_len << 0) |
		(ctx->vm.hback_porch << 8) |
		(ctx->vm.hfront_porch << 20);
	DPU_REG_WR(ctx->base + REG_DPI_H_TIMING, reg_val);

	if (panel->info.dsc_en) {
		reg_val = (ctx->vm.vsync_len << 0) |
			(ctx->vm.vback_porch  << 8) |
			(ctx->vm.vfront_porch << 20);
		DPU_REG_WR(ctx->base + DSC_REG(REG_DSC_V_TIMING), reg_val);

		reg_val = (ctx->vm.hsync_len << 0) |
			(ctx->vm.hback_porch << 8) |
			(ctx->vm.hfront_porch << 20);
		DPU_REG_WR(ctx->base + DSC_REG(REG_DSC_H_TIMING), reg_val);
	}
	sprd_dsi_vrr_timing(dpu->dsi);
	dpu_wait_update_done(ctx);
	ctx->stopped = false;
	DPU_REG_WR(ctx->base + REG_DPU_MMU0_UPDATE, 1);
	dpu->crtc->fps_mode_changed = false;

	return 0;
}

static void dpu_scaling(struct dpu_context *ctx,
		struct sprd_plane planes[], u8 count)
{
	int i;
	u16 src_w;
	u16 src_h;
	u32 reg_val;
	struct sprd_layer_state *layer_state;
	struct sprd_plane_state *plane_state;
	struct scale_config_param *scale_cfg = &ctx->scale_cfg;
	struct sprd_dpu *dpu = container_of(ctx, struct sprd_dpu, ctx);

	if (dpu->crtc->sr_mode_changed) {
		pr_debug("------------------------------------\n");
		for (i = 0; i < count; i++) {
			plane_state = to_sprd_plane_state(planes[i].base.state);
			layer_state = &plane_state->layer;
			pr_debug("layer[%d] : %dx%d --- (%d)\n", i,
					layer_state->dst_w, layer_state->dst_h,
					scale_cfg->in_w);
			if (layer_state->dst_w != scale_cfg->in_w) {
				scale_cfg->skip_layer_index = i;
				break;
			}
		}

		plane_state = to_sprd_plane_state(planes[count - 1].base.state);
		layer_state = &plane_state->layer;
		if  (layer_state->dst_w <= scale_cfg->in_w) {
			dpu_sr_config(ctx);
			dpu->crtc->sr_mode_changed = false;
			pr_info("do scaling enhance, bottom layer(%dx%d)\n",
					layer_state->dst_w, layer_state->dst_h);
		}
	} else {
		if (count == 1) {
			plane_state = to_sprd_plane_state(planes[count - 1].base.state);
			layer_state = &plane_state->layer;
			if (layer_state->pallete_en)
				/*single dim layer cannot get into scaling process*/
				return;
			// btm_layer = &layers[count - 1];
			if (layer_state->rotation & (DRM_MODE_ROTATE_90 |
						DRM_MODE_ROTATE_270)) {
				src_w = layer_state->src_h;
				src_h = layer_state->src_w;
			} else {
				src_w = layer_state->src_w;
				src_h = layer_state->src_h;
			}
			if ((src_w == layer_state->dst_w) && (src_h == layer_state->dst_h)) {
				reg_val = (scale_cfg->in_h << 16) |
					scale_cfg->in_w;
				DPU_REG_WR(ctx->base + REG_BLEND_SIZE, reg_val);
				if (!scale_cfg->need_scale) {
					DPU_REG_CLR(ctx->base + REG_DPU_SCL_EN, BIT_DPU_SCALING_EN);
				} else {
					DPU_REG_SET(ctx->base + REG_DPU_SCL_EN, BIT_DPU_SCALING_EN);
				}
			} else {
				/*
				 * When the layer src size is not euqal to the
				 * dst size, screened by dpu hal,the single
				 * layer need to scaling-up. Regardless of
				 * whether the SR function is turned on, dpu
				 * blend size should be set to the layer src
				 * size.
				 */
				reg_val = (src_h << 16) | src_w;
				DPU_REG_WR(ctx->base + REG_BLEND_SIZE, reg_val);
				/*
				 * When the layer src size is equal to panel
				 * size, close dpu scaling-up function.
				 */
				if (src_h == ctx->vm.vactive &&
						src_w == ctx->vm.hactive) {
					DPU_REG_CLR(ctx->base + REG_DPU_SCL_EN, BIT_DPU_SCALING_EN);
				} else {
					DPU_REG_SET(ctx->base + REG_DPU_SCL_EN, BIT_DPU_SCALING_EN);
					layer_state->dst_w = layer_state->src_w;
					layer_state->dst_h = layer_state->src_h;
				}
			}
		} else {
			reg_val = (scale_cfg->in_h << 16) |
				scale_cfg->in_w;
			DPU_REG_WR(ctx->base + REG_BLEND_SIZE, reg_val);
			if (!scale_cfg->need_scale)
				DPU_REG_CLR(ctx->base + REG_DPU_SCL_EN, BIT_DPU_SCALING_EN);
			else
				DPU_REG_SET(ctx->base + REG_DPU_SCL_EN, BIT_DPU_SCALING_EN);
		}
	}
}

static void cm_multi(struct cm_cfg* cm_final, struct cm_cfg* cm_pq, struct cm_cfg* cm_ctm)
{
	int16_t *cm_final_p, *cm_pq_p, *cm_ctm_p;
	cm_final_p = (int16_t *)cm_final;
	cm_pq_p = (int16_t *)cm_pq;
	cm_ctm_p = (int16_t *)cm_ctm;

	colorMatrix_multi(cm_final_p, cm_pq_p, cm_ctm_p);
}

static void dpu_cm_set(struct dpu_context *ctx, bool cm_status)
{
	struct dpu_enhance *enhance = ctx->enhance;
	struct sprd_dpu *dpu = container_of(ctx, struct sprd_dpu, ctx);
	struct cm_cfg cm_final;

	if (cm_status == CM_PQ) {
		if (enhance->ctm_set) {
			cm_multi(&cm_final, &(enhance->cm_copy), &(enhance->ctm_copy));
		} else {
			memcpy(&cm_final, &enhance->cm_copy, sizeof(struct cm_cfg));
		}
	} else if (cm_status == CM_CTM) {
		bool ret;
		int16_t ctm_final[12] = {0};
		struct drm_color_ctm *ctm;

		if (!dpu->crtc->base.state->ctm)
			return;

		ctm = (struct drm_color_ctm *)dpu->crtc->base.state->ctm->data;
		ret = parse_ctm(ctm_final, ctm);
		if (ret)
			return;

		pr_info("ctm changed!\n");
		memcpy(&(enhance->ctm_copy), ctm_final, sizeof(struct cm_cfg));
		cm_multi(&cm_final, &(enhance->cm_copy), &(enhance->ctm_copy));
		enhance->ctm_set = 1;
	}

	DPU_REG_WR(ctx->base + REG_CM_COEF01_00, (cm_final.c01 << 16) | cm_final.c00);
	DPU_REG_WR(ctx->base + REG_CM_COEF03_02, (cm_final.c03 << 16) | cm_final.c02);
	DPU_REG_WR(ctx->base + REG_CM_COEF11_10, (cm_final.c11 << 16) | cm_final.c10);
	DPU_REG_WR(ctx->base + REG_CM_COEF13_12, (cm_final.c13 << 16) | cm_final.c12);
	DPU_REG_WR(ctx->base + REG_CM_COEF21_20, (cm_final.c21 << 16) | cm_final.c20);
	DPU_REG_WR(ctx->base + REG_CM_COEF23_22, (cm_final.c23 << 16) | cm_final.c22);
	DPU_REG_SET(ctx->base + REG_DPU_ENHANCE_CFG, BIT(2));

	if (cm_status == CM_CTM)
		DPU_REG_SET(ctx->base + REG_ENHANCE_UPDATE, BIT(0));
}

static void dpu_flip(struct dpu_context *ctx,
		     struct sprd_plane planes[], u8 count)
{
	int i;
	u32 reg_val, secure_val;
	struct sprd_plane_state *state;
	struct sprd_layer_state *layer;
	struct sprd_dpu *dpu = container_of(ctx, struct sprd_dpu, ctx);
	struct dpu_enhance *enhance = ctx->enhance;

	ctx->vsync_count = 0;
	if (ctx->max_vsync_count > 0 && count > 1)
		ctx->wb_en = true;
	/*
	 * Make sure the dpu is in stop status. DPU_r6p0 has no shadow
	 * registers in EDPI mode. So the config registers can only be
	 * updated in the rising edge of DPU_RUN bit.
	 */
	if (ctx->if_type == SPRD_DPU_IF_EDPI)
		dpu_wait_stop_done(ctx);

	/* reset the bgcolor to black */
	DPU_REG_WR(ctx->base + REG_BG_COLOR, 0x00);

	 /* to check if dpu need change the frame rate */
	if (dpu->crtc->fps_mode_changed)
		dpu_vrr(ctx);

	/* disable all the layers */
	dpu_clean_all(ctx);

	/* to check if dpu need scaling the frame for SR */
	if (!dpu->dsi->ctx.surface_mode)
		dpu_scaling(ctx, planes, count);

	/* start configure dpu layers */
	for (i = 0; i < count; i++) {
		state = to_sprd_plane_state(planes[i].base.state);
		dpu_layer(ctx, &state->layer);
	}

	dpu_cm_set(ctx, CM_CTM);

	/* update trigger and wait */
	if (ctx->is_single_run) {
		DPU_REG_SET(ctx->base + REG_DPU_CTRL, BIT(4));
		DPU_REG_SET(ctx->base + REG_DPU_CTRL, BIT(0));
	} else if (ctx->if_type == SPRD_DPU_IF_EDPI) {
		DPU_REG_SET(ctx->base + REG_DPU_CTRL, BIT_DPU_RUN);
		ctx->stopped = false;
	} else if (ctx->if_type == SPRD_DPU_IF_DPI) {
		if (!ctx->stopped) {
			secure_val = DPU_REG_RD(ctx->base + REG_DPU_SECURE);
			state = to_sprd_plane_state(planes[0].base.state);
			layer = &state->layer;

			if (enhance->first_frame == true) {
				DPU_REG_SET(ctx->base + REG_DPU_CTRL, BIT_DPU_ALL_UPDATE);
				dpu_wait_all_update_done(ctx);
				enhance->first_frame = false;
			} else {
				if ((!layer->secure_en) && secure_val && (!ctx->fastcall_en)) {
					dpu_wait_update_done(ctx);
					ctx->tos_msg->cmd = TA_FIREWALL_CLR;
					disp_ca_write(&(ctx->tos_msg), sizeof(ctx->tos_msg));
					disp_ca_wait_response();
				} else
					dpu_wait_update_done(ctx);
			}
		}

		DPU_REG_SET(ctx->base + REG_DPU_INT_EN, BIT_DPU_INT_ERR);
	}

	/*
	 * If the following interrupt was disabled in isr,
	 * re-enable it.
	 */
	reg_val = BIT_DPU_INT_FBC_PLD_ERR |
	BIT_DPU_INT_FBC_HDR_ERR;
	DPU_REG_SET(ctx->base + REG_DPU_INT_EN, reg_val);
}

static void dpu_epf_set(struct dpu_context *ctx, struct epf_cfg *epf)
{
	DPU_REG_WR(ctx->base + REG_EPF_EPSILON, (epf->e1 << 16) | epf->e0);
	DPU_REG_WR(ctx->base + REG_EPF_GAIN0_3,
		(epf->e5 << 24) | (epf->e4 << 16) | (epf->e3 << 8) | epf->e2);
	DPU_REG_WR(ctx->base + REG_EPF_GAIN4_7,
		(epf->e9 << 24) | (epf->e8 << 16) | (epf->e7 << 8) | epf->e6);
	DPU_REG_WR(ctx->base + REG_EPF_DIFF, (epf->e11 << 8) | epf->e10);
};

static void dpu_dpi_init(struct dpu_context *ctx)
{
	struct sprd_dpu *dpu = container_of(ctx, struct sprd_dpu, ctx);
	struct sprd_dsi *dsi = dpu->dsi;
	struct sprd_panel *panel = container_of(dsi->panel, struct sprd_panel, base);
	struct videomode vm;
	u32 int_mask = 0;
	u32 reg_val;

	if (dpu->crtc->fps_mode_changed && (dpu->mode.type == DRM_MODE_TYPE_DRIVER)) {
		drm_display_mode_to_videomode(&panel->info.mode, &vm);
	} else if (dpu->mode.type & DRM_MODE_TYPE_USERDEF) {
		drm_display_mode_to_videomode(&panel->info.mode, &vm);
	} else{
		drm_display_mode_to_videomode(&dpu->actual_mode, &vm);
	}

	if (ctx->if_type == SPRD_DPU_IF_DPI) {
		/* use dpi as interface */
		DPU_REG_CLR(ctx->base + REG_DPU_CFG0, BIT_DPU_IF_EDPI);

		/* enable Halt function for SPRD DSI */
		DPU_REG_SET(ctx->base + REG_DPI_CTRL, BIT_DPU_DPI_HALT_EN);

		if (ctx->is_single_run)
			DPU_REG_SET(ctx->base + REG_DPU_CTRL, BIT(0));

		/* set dpi timing */
		reg_val = vm.hsync_len << 0 |
			  vm.hback_porch << 8 |
			  vm.hfront_porch << 20;
		DPU_REG_WR(ctx->base + REG_DPI_H_TIMING, reg_val);

		reg_val = vm.vsync_len << 0 |
			  vm.vback_porch << 8 |
			  vm.vfront_porch << 20;
		DPU_REG_WR(ctx->base + REG_DPI_V_TIMING, reg_val);

		if (vm.vsync_len + vm.vback_porch < 32)
			pr_warn("Warning: (vsync + vbp) < 32, "
				"underflow risk!\n");

		/* enable dpu update done INT */
		int_mask |= BIT_DPU_INT_DPU_ALL_UPDATE_DONE;
		int_mask |= BIT_DPU_INT_DPU_REG_UPDATE_DONE;
		int_mask |= BIT_DPU_INT_PQ_LUT_UPDATE_DONE;
		int_mask |= BIT_DPU_INT_LAY_REG_UPDATE_DONE;
		int_mask |= BIT_DPU_INT_PQ_REG_UPDATE_DONE;
		/* enable dpu DONE  INT */
		int_mask |= BIT_DPU_INT_DONE;
		/* enable dpu dpi vsync */
		int_mask |= BIT_DPU_INT_VSYNC;
		/* enable dpu TE INT */
		if (panel->info.esd_check_mode == ESD_MODE_TE_CHECK)
			int_mask |= BIT_DPU_INT_TE;
		/* enable underflow err INT */
		int_mask |= BIT_DPU_INT_ERR;
		/* enable write back done INT */
		int_mask |= BIT_DPU_INT_WB_DONE_EN;
		/* enable write back fail INT */
		int_mask |= BIT_DPU_INT_WB_ERR_EN;

	} else if (ctx->if_type == SPRD_DPU_IF_EDPI) {
		/* use edpi as interface */
		DPU_REG_SET(ctx->base + REG_DPU_CFG0, BIT_DPU_IF_EDPI);

		/* use external te */
		DPU_REG_SET(ctx->base + REG_DPI_CTRL, BIT_DPU_EDPI_FROM_EXTERNAL_PAD);;

		/* enable te */
		DPU_REG_SET(ctx->base + REG_DPI_CTRL, BIT_DPU_EDPI_TE_EN);

		/* enable stop DONE INT */
		int_mask |= BIT_DPU_INT_DONE;
		/* enable TE INT */
		int_mask |= BIT_DPU_INT_TE;
	}

	/* enable ifbc payload error INT */
	int_mask |= BIT_DPU_INT_FBC_PLD_ERR;
	/* enable ifbc header error INT */
	int_mask |= BIT_DPU_INT_FBC_HDR_ERR;

	DPU_REG_WR(ctx->base + REG_DPU_INT_EN, int_mask);
}

static void enable_vsync(struct dpu_context *ctx)
{
	DPU_REG_SET(ctx->base + REG_DPU_INT_EN, BIT_DPU_INT_VSYNC);
}

static void disable_vsync(struct dpu_context *ctx)
{
	//DPU_REG_CLR(ctx->base + REG_DPU_INT_EN, BIT_DPU_INT_VSYNC);
}

static int dpu_context_init(struct dpu_context *ctx, struct device_node *np)
{
	struct device_node *qos_np;
	struct device_node *bl_np;
	struct dpu_enhance *enhance;
	int ret = 0;

	enhance = kzalloc(sizeof(*enhance), GFP_KERNEL);
	if (!enhance) {
		pr_err("%s() enhance kzalloc failed!\n", __func__);
		return -ENOMEM;
	}

	bl_np = of_parse_phandle(np, "sprd,backlight", 0);
	if (bl_np) {
		enhance->bl_dev = of_find_backlight_by_node(bl_np);
		of_node_put(bl_np);
		if (IS_ERR_OR_NULL(enhance->bl_dev)) {
			DRM_WARN("backlight is not ready, dpu probe deferred\n");
			kfree(enhance);
			return -EPROBE_DEFER;
		}
	} else {
		pr_warn("dpu backlight node not found\n");
	}

	if (of_property_read_bool(np, "sprd,widevine-use-fastcall")) {
		ctx->fastcall_en = true;
		pr_info("read widevine-use-fastcall success, fastcall_en = true\n");
	} else {
		ctx->fastcall_en = false;
		pr_info("read widevine-use-fastcall failed, fastcall_en = false\n");
	}

	qos_np = of_parse_phandle(np, "sprd,qos", 0);
	if (!qos_np)
		pr_warn("can't find dpu qos cfg node\n");

	ret = of_property_read_u8(qos_np, "arqos-low",
					&ctx->qos_cfg.arqos_low);
	if (ret) {
		pr_warn("read arqos-low failed, use default\n");
		ctx->qos_cfg.arqos_low = 0x0a;
	}

	ret = of_property_read_u8(qos_np, "arqos-high",
					&ctx->qos_cfg.arqos_high);
	if (ret) {
		pr_warn("read arqos-high failed, use default\n");
		ctx->qos_cfg.arqos_high = 0x0c;
	}

	ret = of_property_read_u8(qos_np, "awqos-low",
					&ctx->qos_cfg.awqos_low);
	if (ret) {
		pr_warn("read awqos_low failed, use default\n");
		ctx->qos_cfg.awqos_low = 0x0a;
	}

	ret = of_property_read_u8(qos_np, "awqos-high",
					&ctx->qos_cfg.awqos_high);
	if (ret) {
		pr_warn("read awqos-high failed, use default\n");
		ctx->qos_cfg.awqos_high = 0x0c;
	}

	ctx->enhance = enhance;
	enhance->cabc_state = CABC_DISABLED;
	INIT_WORK(&ctx->cabc_work, dpu_cabc_work_func);
	INIT_WORK(&ctx->cabc_bl_update, dpu_cabc_bl_update_func);

	ctx->base_offset[0] = 0x0;
	ctx->base_offset[1] = DPU_MAX_REG_OFFSET / 4;

	ctx->wb_configed = false;

	/* Allocate memory for trusty */
	ctx->tos_msg = kmalloc(sizeof(struct disp_message) + sizeof(struct layer_reg), GFP_KERNEL);
	if (!ctx->tos_msg)
		return -ENOMEM;

	return 0;
}

static void dpu_luts_copyfrom_user(u32 *param, struct dpu_enhance *enhance)
{
	static u32 i;
	u8 *luts_tmp_vaddr;

	/* LUTS_COPY_TIME means LUTS_COPY_TIME*2k, all luts size.
	 * each time writes 2048 bytes from
	 * user space.
	 */
	if (i == LUTS_COPY_TIME)
		i = 0;

	luts_tmp_vaddr = (u8 *)enhance->lut_gamma_vaddr;
	luts_tmp_vaddr += 2048 * i;
	memcpy(luts_tmp_vaddr, param, 2048);
	i++;
}

static void dpu_luts_backup(struct dpu_context *ctx, struct dpu_enhance *enhance, void *param)
{
	u32 *p32 = param;
	struct hsv_params *hsv_cfg;
	int ret = 0;

	memcpy(&enhance->typeindex_cpy, param, sizeof(enhance->typeindex_cpy));

	switch (enhance->typeindex_cpy.type) {
	case LUTS_GAMMA_TYPE:
		enhance->lut_addrs_cpy.lut_gamma_addr = enhance->dpu_luts_paddr +
			DPU_LUTS_GAMMA_OFFSET + enhance->typeindex_cpy.index * 4096;
		enhance->enhance_en |= BIT(3) | BIT(5);
		break;
	case LUTS_HSV_TYPE:
		p32++;
		hsv_cfg = (struct hsv_params *) p32;
		enhance->hsv_cfg_copy = (hsv_cfg->h0 & 0xff) |
			((hsv_cfg->h1 & 0x3) << 8) |
			((hsv_cfg->h2 & 0x3) << 12);
		enhance->lut_addrs_cpy.lut_hsv_addr = enhance->dpu_luts_paddr +
			DPU_LUTS_HSV_OFFSET + enhance->typeindex_cpy.index * 4096;
		enhance->enhance_en |= BIT(1);
		break;
	case LUTS_LUT3D_TYPE:
		enhance->lut_addrs_cpy.lut_lut3d_addr = enhance->dpu_luts_paddr +
			DPU_LUTS_LUT3D_OFFSET + enhance->typeindex_cpy.index * 4096 * 6;
		enhance->enhance_en |= BIT(4);
		break;
	case LUTS_ALL:
		ret = dpu_luts_alloc(ctx);
		if (ret) {
			pr_err("DPU lUTS table alloc buffer fail\n");
			break;
		}
		p32++;
		dpu_luts_copyfrom_user(p32, enhance);
		break;
	default:
		pr_err("The type %d is unavaiable\n", enhance->typeindex_cpy.type);
		break;
	}
}

static void dpu_enhance_backup(struct dpu_context *ctx, u32 id, void *param)
{
	struct dpu_enhance *enhance = ctx->enhance;
	u32 *p;

	switch (id) {
	case ENHANCE_CFG_ID_ENABLE:
		p = param;
		enhance->enhance_en |= *p;
		pr_info("enhance module enable backup: 0x%x\n", *p);
		break;
	case ENHANCE_CFG_ID_DISABLE:
		p = param;
		enhance->enhance_en &= ~(*p);
		pr_info("enhance module disable backup: 0x%x\n", *p);
		break;
	case ENHANCE_CFG_ID_HSV:
		memcpy(&enhance->hsv_lut_copy, param, sizeof(enhance->hsv_lut_copy));
		enhance->enhance_en |= BIT(1);
		pr_info("enhance hsv backup\n");
		break;
	case ENHANCE_CFG_ID_CM:
		memcpy(&enhance->cm_copy, param, sizeof(enhance->cm_copy));
		enhance->enhance_en |= BIT(2);
		pr_info("enhance cm backup\n");
		break;
	case ENHANCE_CFG_ID_LTM:
	case ENHANCE_CFG_ID_SLP:
		memcpy(&enhance->slp_copy, param, sizeof(enhance->slp_copy));
		if (!enhance->cabc_state) {
			enhance->slp_copy.s37 = 0;
			enhance->slp_copy.s38 = 255;
		}
		enhance->enhance_en |= BIT(6) | BIT(7);
		pr_info("enhance slp backup\n");
		break;
	case ENHANCE_CFG_ID_GAMMA:
		memcpy(&enhance->gamma_copy, param, sizeof(enhance->gamma_copy));
		enhance->enhance_en |= BIT(3) | BIT(5);
		pr_info("enhance gamma backup\n");
		break;
	case ENHANCE_CFG_ID_EPF:
		memcpy(&enhance->epf_copy, param, sizeof(enhance->epf_copy));
		enhance->enhance_en |= BIT(0);
		pr_info("enhance epf backup\n");
		break;
	case ENHANCE_CFG_ID_LUT3D:
		memcpy(&enhance->lut3d_copy, param, sizeof(enhance->lut3d_copy));
		enhance->enhance_en |= BIT(4);
		pr_info("enhance lut3d backup\n");
		break;
	case ENHANCE_CFG_ID_CABC_STATE:
		p = param;
		enhance->cabc_state = *p;
		return;
	case ENHANCE_CFG_ID_UD:
		memcpy(&enhance->ud_copy, param, sizeof(enhance->ud_copy));
		enhance->enhance_en |= BIT(10) | BIT(11) | BIT(12);
		pr_info("enhance ud backup\n");
		break;
	case ENHANCE_CFG_ID_UPDATE_LUTS:
		p = param;
		dpu_luts_backup(ctx, enhance, param);
		pr_info("enhance ddr luts backup\n");
		break;
	default:
		break;
	}
};

static void dpu_luts_update(struct dpu_context *ctx, void *param)
{
	struct dpu_enhance *enhance = ctx->enhance;
	struct hsv_params *hsv_cfg;
	u32 *p32 = param;
	bool no_update = false;

	memcpy(&enhance->typeindex_cpy, param, sizeof(enhance->typeindex_cpy));

	switch (enhance->typeindex_cpy.type) {
	case LUTS_GAMMA_TYPE:
		DPU_REG_WR(ctx->base + REG_GAMMA_LUT_BASE_ADDR,
			enhance->dpu_luts_paddr + DPU_LUTS_GAMMA_OFFSET +
			enhance->typeindex_cpy.index * 4096);
		enhance->gamma_lut_index = enhance->typeindex_cpy.index;
		enhance->lut_addrs_cpy.lut_gamma_addr =
			DPU_REG_RD(ctx->base + REG_GAMMA_LUT_BASE_ADDR);
		DPU_REG_SET(ctx->base + REG_DPU_ENHANCE_CFG, BIT(3) | BIT(5));
		DPU_REG_SET(ctx->base + REG_ENHANCE_UPDATE, BIT(1) | BIT(0));
		pr_info("LUTS_GAMMA set\n");
		break;
	case LUTS_HSV_TYPE:
		p32++;
		hsv_cfg = (struct hsv_params *) p32;
		DPU_REG_WR(ctx->base + REG_HSV_CFG, (hsv_cfg->h0 & 0xff) |
			((hsv_cfg->h1 & 0x3) << 8) |
			((hsv_cfg->h2 & 0x3) << 12));
		enhance->hsv_cfg_copy = DPU_REG_RD(ctx->base + REG_HSV_CFG);
		DPU_REG_WR(ctx->base + REG_HSV_LUT_BASE_ADDR, enhance->dpu_luts_paddr +
			DPU_LUTS_HSV_OFFSET + enhance->typeindex_cpy.index * 4096);
		enhance->hsv_lut_index = enhance->typeindex_cpy.index;
		enhance->lut_addrs_cpy.lut_hsv_addr =
			DPU_REG_RD(ctx->base + REG_HSV_LUT_BASE_ADDR);
		DPU_REG_SET(ctx->base + REG_DPU_ENHANCE_CFG, BIT(1));
		DPU_REG_SET(ctx->base + REG_ENHANCE_UPDATE, BIT(2) | BIT(0));
		pr_info("LUTS_HSV set\n");
		break;
	case LUTS_LUT3D_TYPE:
		DPU_REG_WR(ctx->base + REG_THREED_LUT_BASE_ADDR, enhance->dpu_luts_paddr +
			DPU_LUTS_LUT3D_OFFSET + enhance->typeindex_cpy.index * 4096 * 6);
		enhance->lut3d_index = enhance->typeindex_cpy.index;
		enhance->lut_addrs_cpy.lut_lut3d_addr =
			DPU_REG_RD(ctx->base + REG_THREED_LUT_BASE_ADDR);
		DPU_REG_SET(ctx->base + REG_DPU_ENHANCE_CFG, BIT(4));
		DPU_REG_SET(ctx->base + REG_ENHANCE_UPDATE, BIT(3) | BIT(0));
		pr_info("LUTS_LUT3D set\n");
		break;
	case LUTS_ALL:
		p32++;
		dpu_luts_copyfrom_user(p32, enhance);
		no_update = true;
		break;
	default:
		no_update = true;
		pr_err("The type %d is unavaiable\n", enhance->typeindex_cpy.type);
		break;
	}
	if (!no_update)
		dpu_wait_pq_lut_reg_update_done(ctx);
}

static void dpu_enhance_set(struct dpu_context *ctx, u32 id, void *param)
{
	struct dpu_enhance *enhance = ctx->enhance;
	struct slp_cfg *slp;
	struct gamma_lut *gamma;
	struct rgb_integrate_arr *lut3d;
	struct hsv_params *hsv_cfg;
	struct hsv_luts *hsv_table;
	struct epf_cfg *epf;
	struct ud_cfg *ud;
	struct cabc_para cabc_param;
	static u32 lut3d_table_index;
	u32 *p32, *tmp32;
	int i, j;
	bool no_update = false;

	if (!ctx->enabled) {
		dpu_enhance_backup(ctx, id, param);
		return;
	}

	if (ctx->if_type == SPRD_DPU_IF_EDPI)
		dpu_wait_stop_done(ctx);

	switch (id) {
	case ENHANCE_CFG_ID_ENABLE:
		p32 = param;
		DPU_REG_SET(ctx->base + REG_DPU_ENHANCE_CFG, *p32);
		pr_info("enhance module enable: 0x%x\n", *p32);
		break;
	case ENHANCE_CFG_ID_DISABLE:
		p32 = param;
		DPU_REG_CLR(ctx->base + REG_DPU_ENHANCE_CFG, *p32);
		pr_info("enhance module disable: 0x%x\n", *p32);
		break;
	case ENHANCE_CFG_ID_HSV:
		memcpy(&enhance->hsv_offset_copy, param, sizeof(enhance->hsv_offset_copy));
		hsv_cfg = &enhance->hsv_offset_copy;
		DPU_REG_WR(ctx->base + REG_HSV_CFG,
			hsv_cfg->h0 | (hsv_cfg->h1 << 8) | (hsv_cfg->h2 << 12));
		memcpy(&enhance->hsv_lut_copy, param + sizeof(enhance->hsv_offset_copy),
				sizeof(enhance->hsv_lut_copy));
		hsv_table = &enhance->hsv_lut_copy;
		for (i = 0; i < 4; i++) {
			p32 = enhance->lut_hsv_vaddr + enhance->hsv_lut_index * 1024 + i;
			for (j = 0; j < 64; j++) {
				*p32 = (hsv_table->hsv_lut[i].h_o[j] << 9) |
					hsv_table->hsv_lut[i].s_g[j];
				p32 += 4;
			}
		}
		DPU_REG_WR(ctx->base + REG_HSV_LUT_BASE_ADDR, enhance->dpu_luts_paddr +
			DPU_LUTS_HSV_OFFSET + enhance->hsv_lut_index * 4096);
		DPU_REG_SET(ctx->base + REG_DPU_ENHANCE_CFG, BIT(1));
		DPU_REG_SET(ctx->base + REG_ENHANCE_UPDATE, BIT(2) | BIT(0));
		pr_info("enhance hsv_lut update\n");
		break;
	case ENHANCE_CFG_ID_CM:
		memcpy(&enhance->cm_copy, param, sizeof(enhance->cm_copy));
		dpu_cm_set(ctx, CM_PQ);
		pr_info("enhance cm set\n");
		break;
	case ENHANCE_CFG_ID_LTM:
		DPU_REG_SET(ctx->base + REG_DPU_ENHANCE_CFG, BIT(7));
		pr_info("enhance ltm set\n");
		fallthrough;
	case ENHANCE_CFG_ID_SLP:
		memcpy(&enhance->slp_copy, param, sizeof(enhance->slp_copy));
		if (!enhance->cabc_state) {
			enhance->slp_copy.s37 = 0;
			enhance->slp_copy.s38 = 255;
		}
		slp = &enhance->slp_copy;
		DPU_REG_WR(ctx->base + REG_SLP_CFG0, (slp->s0 << 0) |
			((slp->s1 & 0x7f) << 16));
		DPU_REG_WR(ctx->base + REG_SLP_CFG1, ((slp->s5 & 0x7f) << 21) |
			((slp->s4 & 0x7f) << 14) |
			((slp->s3 & 0x7f) << 7) |
			((slp->s2 & 0x7f) << 0));
		DPU_REG_WR(ctx->base + REG_SLP_CFG2, ((slp->s9 & 0x7f) << 25) |
			((slp->s8 & 0x7f) << 18) |
			((slp->s7 & 0x3) << 16) |
			((slp->s6 & 0x7f) << 9));
		DPU_REG_WR(ctx->base + REG_SLP_CFG3, ((slp->s14 & 0xfff) << 19) |
			((slp->s13 & 0xf) << 15) |
			((slp->s12 & 0xf) << 11) |
			((slp->s11 & 0xf) << 7) |
			((slp->s10 & 0xf) << 3));
		DPU_REG_WR(ctx->base + REG_SLP_CFG4, (slp->s18 << 24) |
			(slp->s17 << 16) | (slp->s16 << 8) |
			(slp->s15 << 0));
		DPU_REG_WR(ctx->base + REG_SLP_CFG5, (slp->s22 << 24) |
			(slp->s21 << 16) | (slp->s20 << 8) |
			(slp->s19 << 0));
		DPU_REG_WR(ctx->base + REG_SLP_CFG6, (slp->s26 << 24) |
			(slp->s25 << 16) | (slp->s24 << 8) |
			(slp->s23 << 0));
		DPU_REG_WR(ctx->base + REG_SLP_CFG7, ((slp->s29 & 0x1ff) << 23) |
			((slp->s28 & 0x1ff) << 14) |
			((slp->s27 & 0x1ff) << 5));
		DPU_REG_WR(ctx->base + REG_SLP_CFG8, ((slp->s32 & 0x1ff) << 23) |
			((slp->s31 & 0x1ff) << 14) |
			((slp->s30 & 0x1ff) << 0));
		DPU_REG_WR(ctx->base + REG_SLP_CFG9, ((slp->s36 & 0x7f) << 25) |
			(slp->s35 << 17) |
			((slp->s34 & 0xf) << 13) |
			((slp->s33 & 0x7f) << 6));
		DPU_REG_WR(ctx->base + REG_SLP_CFG10, (slp->s38 << 8) |
			(slp->s37 << 0));
		DPU_REG_SET(ctx->base + REG_DPU_ENHANCE_CFG, BIT(6));
		pr_info("enhance slp set\n");
		if (enhance->cabc_para.video_mode) {
			enhance->enhance_en = DPU_REG_RD(ctx->base + REG_DPU_ENHANCE_CFG);
			DPU_REG_SET(ctx->base + REG_DPU_CTRL, BIT(2));
			return;
		}
		break;
	case ENHANCE_CFG_ID_GAMMA:
		memcpy(&enhance->gamma_copy, param, sizeof(enhance->gamma_copy));
		gamma = &enhance->gamma_copy;
		p32 = enhance->lut_gamma_vaddr + enhance->gamma_lut_index * 1024;
		for (j = 0; j < 256; j++) {
			*p32 = (gamma->r[j] << 20) |
				(gamma->g[j] << 10) |
				gamma->b[j];
			p32 += 2;
		}
		DPU_REG_WR(ctx->base + REG_GAMMA_LUT_BASE_ADDR,
			enhance->dpu_luts_paddr + DPU_LUTS_GAMMA_OFFSET +
			enhance->gamma_lut_index * 4096);
		DPU_REG_SET(ctx->base + REG_DPU_ENHANCE_CFG, BIT(3) | BIT(5));
		DPU_REG_SET(ctx->base + REG_ENHANCE_UPDATE, BIT(1) | BIT(0));
		pr_info("enhance gamma_lut update\n");
		break;
	case ENHANCE_CFG_ID_EPF:
		memcpy(&enhance->epf_copy, param, sizeof(enhance->epf_copy));
		epf = &enhance->epf_copy;
		dpu_epf_set(ctx, epf);
		DPU_REG_SET(ctx->base + REG_DPU_ENHANCE_CFG, BIT(0));
		pr_info("enhance epf set\n");
		break;
	case ENHANCE_CFG_ID_LUT3D:
		memcpy(&enhance->rgb_arr_copy, param, sizeof(enhance->rgb_arr_copy));
		lut3d = &enhance->rgb_arr_copy;
		p32 = (u32 *)enhance->lut_lut3d_vaddr +
			lut3d_table_index + enhance->lut3d_index * 6 * 1024;
		tmp32 = p32;
		for (j = 0; j < 729; j++) {
			*p32 = lut3d->rgb_value[j];
			p32 += 8;
		}
		lut3d_table_index++;
		/*
		 * In LUT3D mode, the lut tables need to be written to ddr in 8 times.
		 * The REG_THREED_LUT_BASE_ADDR need to be setted when the lut tables are all
		 * written.
		 */
		if (lut3d_table_index == 8) {
			lut3d_table_index = 0;
			DPU_REG_WR(ctx->base + REG_THREED_LUT_BASE_ADDR, enhance->dpu_luts_paddr +
				DPU_LUTS_LUT3D_OFFSET + enhance->lut3d_index * 4096 * 6);
			DPU_REG_SET(ctx->base + REG_DPU_ENHANCE_CFG, BIT(4));
			DPU_REG_SET(ctx->base + REG_ENHANCE_UPDATE, BIT(3) | BIT(0));
		}
		pr_info("enhance lut3d lut update\n");
		enhance->enhance_en = DPU_REG_RD(ctx->base + REG_DPU_ENHANCE_CFG);
		break;
	case ENHANCE_CFG_ID_CABC_MODE:
		p32 = param;
		if (*p32 & CABC_MODE_UI)
			enhance->cabc_para.video_mode = 0;
		else if (*p32 & CABC_MODE_FULL_FRAME)
			enhance->cabc_para.video_mode = 1;
		else if (*p32 & CABC_MODE_VIDEO)
			enhance->cabc_para.video_mode = 2;
		pr_info("enhance CABC mode = 0x%x\n", *p32);
		return;
	case ENHANCE_CFG_ID_CABC_PARAM:
		memcpy(&cabc_param, param, sizeof(cabc_param));
		enhance->cabc_para.bl_fix = cabc_param.bl_fix;
		enhance->cabc_para.cfg0 = cabc_param.cfg0;
		enhance->cabc_para.cfg1 = cabc_param.cfg1;
		enhance->cabc_para.cfg2 = cabc_param.cfg2;
		enhance->cabc_para.cfg3 = cabc_param.cfg3;
		enhance->cabc_para.cfg4 = cabc_param.cfg4;
		return;
	case ENHANCE_CFG_ID_CABC_RUN:
		if (enhance->cabc_state != CABC_DISABLED)
			schedule_work(&ctx->cabc_work);
		return;
	case ENHANCE_CFG_ID_CABC_STATE:
		p32 = param;
		enhance->cabc_state = *p32;
		enhance->frame_no = 0;
		return;
	case ENHANCE_CFG_ID_UD:
		memcpy(&enhance->ud_copy, param, sizeof(enhance->ud_copy));
		ud = param;
		DPU_REG_WR(ctx->base + REG_UD_CFG0, ud->u0 | (ud->u1 << 16) |
			(ud->u2 << 24));
		DPU_REG_WR(ctx->base + REG_UD_CFG1, ud->u3 | (ud->u4 << 16) |
			(ud->u5 << 24));
		DPU_REG_SET(ctx->base + REG_DPU_ENHANCE_CFG, BIT(10) | BIT(11) | BIT(12));
		pr_info("enhance ud set\n");
		break;
	case ENHANCE_CFG_ID_UPDATE_LUTS:
		no_update = true;
		dpu_luts_update(ctx, param);
		break;
	default:
		no_update = true;
		break;
	}

	if ((ctx->if_type == SPRD_DPU_IF_DPI) && !ctx->stopped) {
		if (id == ENHANCE_CFG_ID_SCL) {
			DPU_REG_SET(ctx->base + REG_DPU_CTRL, BIT(3));
			dpu_wait_all_regs_update_done(ctx);
		} else if (!no_update) {
			DPU_REG_SET(ctx->base + REG_ENHANCE_UPDATE, BIT(0));
			dpu_wait_pq_update_done(ctx);
		}
	} else if ((ctx->if_type == SPRD_DPU_IF_EDPI) && ctx->panel_ready) {
		/*
		 * In EDPI mode, we need to wait panel initializatin
		 * completed. Otherwise, the dpu enhance settings may
		 * start before panel initialization.
		 */
		DPU_REG_SET(ctx->base + REG_DPU_CTRL, BIT(0));
		ctx->stopped = false;
	}

	enhance->enhance_en = DPU_REG_RD(ctx->base + REG_DPU_ENHANCE_CFG) |
			(DPU_REG_RD(ctx->base + REG_SCL_EN) << 13);
}

static void dpu_luts_copyto_user(u32 *param, struct dpu_enhance *enhance)
{
	u32 *p32;
	u32 *ptmp;
	u32 i, j;
	u16 r, g, b;
	u16 h_o, s_g;
	u16 mode, mode_index;
	struct luts_typeindex *type_index;

	type_index = (struct luts_typeindex *)param;
	pr_info("%s type =%d, index =%d\n", __func__,
		type_index->type, type_index->index);

	if (type_index->type == LUTS_GAMMA_TYPE) {
		p32 = enhance->lut_gamma_vaddr + 1024 * type_index->index;
		pr_info("gamma:type_index %d\n", type_index->type);
		for (j = 0; j < 256; j++) {
			r = (*p32 >> 20) & 0x3ff;
			g = (*p32 >> 10) & 0x3ff;
			b = *p32 & 0x3ff;
			p32 += 2;
			pr_info("r %d, g %d, b %d\n", r, g, b);
		}
	}

	if (type_index->type == LUTS_HSV_TYPE) {
		p32 = enhance->lut_hsv_vaddr + 1024 * type_index->index;
		ptmp = p32;
		pr_info("hsv_mode%d\n", type_index->type);
		for (i = 0; i < 4; i++) {
			p32 = ptmp;
			pr_info("hsv_lut_index%d\n\n", i);
			for (j = 0; j < 64; j++) {
				h_o =  (*p32 >> 9) & 0x7f;
				s_g = *p32 & 0x1ff;
				pr_info("h_o %d, s_g %d\n", h_o, s_g);
				p32 += 4;
			}
			ptmp++;
		}
	}

	if (type_index->type == LUTS_LUT3D_TYPE) {
		p32 = enhance->lut_lut3d_vaddr;
		mode = type_index->index >> 8;
		mode_index = type_index->index & 0xff;
		pr_info("3dlut: mode%d index%d\n", mode, mode_index);
		p32 += mode * 1024 * 6;
		p32 += mode_index;
		for (i = 0; i < 729; i++) {
			r = (*p32 >> 20) & 0x3ff;
			g = (*p32 >> 10) & 0x3ff;
			b = *p32 & 0x3ff;
			pr_info("r %d, g %d, b %d\n", r, g, b);
			p32 += 8;
		}
	}
}

static void dpu_enhance_get(struct dpu_context *ctx, u32 id, void *param)
{
	struct dpu_enhance *enhance = ctx->enhance;
	struct epf_cfg *epf;
	struct slp_cfg *slp;
	struct cm_cfg *cm;
	struct gamma_lut *gamma;
	struct hsv_luts *hsv_table;
	struct ud_cfg *ud;
	struct hsv_params *hsv_cfg;
	static u32 lut3d_table_index;
	int i, j, val;
	u16 *p16;
	u32 *dpu_lut_addr, *dpu_lut_raddr, *p32;
	int *vsynccount;
	int *frameno;

	switch (id) {
	case ENHANCE_CFG_ID_ENABLE:
		p32 = param;
		*p32 = DPU_REG_RD(ctx->base + REG_DPU_ENHANCE_CFG);
		pr_info("enhance module enable get\n");
		break;
	case ENHANCE_CFG_ID_EPF:
		epf = param;

		val = DPU_REG_RD(ctx->base + REG_EPF_EPSILON);
		epf->e0 = val;
		epf->e1 = val >> 16;

		val = DPU_REG_RD(ctx->base + REG_EPF_GAIN0_3);
		epf->e2 = val;
		epf->e3 = val >> 8;
		epf->e4 = val >> 16;
		epf->e5 = val >> 24;

		val = DPU_REG_RD(ctx->base + REG_EPF_GAIN4_7);
		epf->e6 = val;
		epf->e7 = val >> 8;
		epf->e8 = val >> 16;
		epf->e9 = val >> 24;

		val = DPU_REG_RD(ctx->base + REG_EPF_DIFF);
		epf->e10 = val;
		epf->e11 = val >> 8;
		pr_info("enhance epf get\n");
		break;
	case ENHANCE_CFG_ID_HSV:
		dpu_stop(ctx);
		hsv_cfg = (struct hsv_params *)param;
		hsv_cfg->h0 = DPU_REG_RD(ctx->base + REG_HSV_CFG) & 0xff;
		hsv_cfg->h1 = (DPU_REG_RD(ctx->base + REG_HSV_CFG) >> 8) & 0x3;
		hsv_cfg->h2 = (DPU_REG_RD(ctx->base + REG_HSV_CFG) >> 12) & 0x3;
		hsv_cfg++;
		hsv_table = (struct hsv_luts *)hsv_cfg;
		dpu_lut_addr = (u32 *)(ctx->base + REG_HSV_LUT0_ADDR);
		dpu_lut_raddr = (u32 *)(ctx->base + REG_HSV_LUT0_RDATA);
		for (i = 0; i < 4; i++) {
			for (j = 0; j < 64; j++) {
				*dpu_lut_addr = j;
				udelay(1);
				hsv_table->hsv_lut[i].h_o[j] =
					(*dpu_lut_raddr >> 9)  & 0x7f;
				hsv_table->hsv_lut[i].s_g[j] =
					*dpu_lut_raddr & 0x1ff;
			}
			dpu_lut_addr += 2;
			dpu_lut_raddr += 2;
		}
		dpu_run(ctx);
		pr_info("enhance hsv get\n");
		break;
	case ENHANCE_CFG_ID_CM:
		cm = (struct cm_cfg *)param;
		cm->c00 = DPU_REG_RD(ctx->base + REG_CM_COEF01_00) & 0x3fff;
		cm->c01 = (DPU_REG_RD(ctx->base + REG_CM_COEF01_00) >> 16) & 0x3fff;
		cm->c02 = DPU_REG_RD(ctx->base + REG_CM_COEF03_02) & 0x3fff;
		cm->c03 = (DPU_REG_RD(ctx->base + REG_CM_COEF03_02) >> 16) & 0x3fff;
		cm->c10 = DPU_REG_RD(ctx->base + REG_CM_COEF11_10) & 0x3fff;
		cm->c11 = (DPU_REG_RD(ctx->base + REG_CM_COEF11_10) >> 16) & 0x3fff;
		cm->c12 = DPU_REG_RD(ctx->base + REG_CM_COEF13_12) & 0x3fff;
		cm->c13 = (DPU_REG_RD(ctx->base + REG_CM_COEF13_12) >> 16) & 0x3fff;
		cm->c20 = DPU_REG_RD(ctx->base + REG_CM_COEF21_20) & 0x3fff;
		cm->c21 = (DPU_REG_RD(ctx->base + REG_CM_COEF21_20) >> 16) & 0x3fff;
		cm->c22 = DPU_REG_RD(ctx->base + REG_CM_COEF23_22) & 0x3fff;
		cm->c23 = (DPU_REG_RD(ctx->base + REG_CM_COEF23_22) >> 16) & 0x3fff;

		pr_info("enhance cm get\n");
		break;
	case ENHANCE_CFG_ID_LTM:
	case ENHANCE_CFG_ID_SLP:
		slp = param;
		val = DPU_REG_RD(ctx->base + REG_SLP_CFG0);
		slp->s0 = (val >> 0) & 0xffff;
		slp->s1 = (val >> 16) & 0x7f;

		val = DPU_REG_RD(ctx->base + REG_SLP_CFG1);
		slp->s5 = (val >> 21) & 0x7f;
		slp->s4 = (val >> 14) & 0x7f;
		slp->s3 = (val >> 7) & 0x7f;
		slp->s2 = (val >> 0) & 0x7f;

		val = DPU_REG_RD(ctx->base + REG_SLP_CFG2);
		slp->s9 = (val >> 25) & 0x7f;
		slp->s8 = (val >> 18) & 0x7f;
		slp->s7 = (val >> 16) & 0x3;
		slp->s6 = (val >> 9) & 0x7f;

		val = DPU_REG_RD(ctx->base + REG_SLP_CFG3);
		slp->s14 = (val >> 19) & 0xfff;
		slp->s13 = (val >> 15) & 0xf;
		slp->s12 = (val >> 11) & 0xf;
		slp->s11 = (val >> 7) & 0xf;
		slp->s10 = (val >> 3) & 0xf;

		val = DPU_REG_RD(ctx->base + REG_SLP_CFG4);
		slp->s18 = (val >> 24) & 0xff;
		slp->s17 = (val >> 16) & 0xff;
		slp->s16 = (val >> 8) & 0xff;
		slp->s15 = (val >> 0) & 0xff;

		val = DPU_REG_RD(ctx->base + REG_SLP_CFG5);
		slp->s22 = (val >> 24) & 0xff;
		slp->s21 = (val >> 16) & 0xff;
		slp->s20 = (val >> 8) & 0xff;
		slp->s19 = (val >> 0) & 0xff;

		val = DPU_REG_RD(ctx->base + REG_SLP_CFG6);
		slp->s26 = (val >> 24) & 0xff;
		slp->s25 = (val >> 16) & 0xff;
		slp->s24 = (val >> 8) & 0xff;
		slp->s23 = (val >> 0) & 0xff;

		val = DPU_REG_RD(ctx->base + REG_SLP_CFG7);
		slp->s29 = (val >> 23) & 0x1ff;
		slp->s28 = (val >> 14) & 0x1ff;
		slp->s27 = (val >> 5) & 0x1ff;

		val = DPU_REG_RD(ctx->base + REG_SLP_CFG8);
		slp->s32 = (val >> 23) & 0x1ff;
		slp->s31 = (val >> 14) & 0x1ff;
		slp->s30 = (val >> 0) & 0x1fff;

		val = DPU_REG_RD(ctx->base + REG_SLP_CFG9);
		slp->s36 = (val >> 25) & 0x7f;
		slp->s35 = (val >> 17) & 0xff;
		slp->s34 = (val >> 13) & 0xf;
		slp->s33 = (val >> 6) & 0x7f;

		/* FIXME SLP_CFG10 just for CABC*/
		val = DPU_REG_RD(ctx->base + REG_SLP_CFG10);
		slp->s38 = (val >> 8) & 0xff;
		slp->s37 = (val >> 0) & 0xff;
		pr_info("enhance slp get\n");
		break;
	case ENHANCE_CFG_ID_GAMMA:
		dpu_stop(ctx);
		gamma = param;
		for (i = 0; i < 256; i++) {
			DPU_REG_WR(ctx->base + REG_GAMMA_LUT_ADDR, i);
			udelay(1);
			val = DPU_REG_RD(ctx->base + REG_GAMMA_LUT_RDATA);
			gamma->r[i] = (val >> 20) & 0x3FF;
			gamma->g[i] = (val >> 10) & 0x3FF;
			gamma->b[i] = val & 0x3FF;
			pr_debug("0x%02x: r=%u, g=%u, b=%u\n", i,
				gamma->r[i], gamma->g[i], gamma->b[i]);
		}
		dpu_run(ctx);
		pr_info("enhance gamma get\n");
		break;
	case ENHANCE_CFG_ID_SLP_LUT:
		dpu_stop(ctx);
		p32 = param;
		for (i = 0; i < 256; i++) {
			DPU_REG_WR(ctx->base + REG_SLP_LUT_ADDR, i);
			udelay(1);
			*p32++ = DPU_REG_RD(ctx->base + REG_SLP_LUT_RDATA);
		}
		dpu_run(ctx);
		pr_info("enhance slp lut get\n");
		break;
	case ENHANCE_CFG_ID_LUT3D:
		dpu_stop(ctx);
		p32 = param;
		if (lut3d_table_index == 8)
			lut3d_table_index = 0;
		dpu_lut_addr = (u32 *)(ctx->base + REG_THREED_LUT0_ADDR);
		dpu_lut_raddr = (u32 *)enhance->lut_lut3d_vaddr +
			enhance->lut3d_index * 6 * 1024 + lut3d_table_index;
		for (j = 0; j < 729; j++) {
			*dpu_lut_addr = j;
			udelay(1);
			*p32 = *dpu_lut_raddr;
			p32++;
			dpu_lut_raddr += 8;
		}
		lut3d_table_index++;
		dpu_run(ctx);
		pr_info("enhance lut3d get\n");
		break;
	case ENHANCE_CFG_ID_UD:
		ud = param;
		ud->u0 = DPU_REG_RD(ctx->base + REG_UD_CFG0) & 0xfff;
		ud->u1 = (DPU_REG_RD(ctx->base + REG_UD_CFG0) >> 16) & 0x3f;
		ud->u2 = (DPU_REG_RD(ctx->base + REG_UD_CFG0) >> 24) & 0x3f;
		ud->u3 = DPU_REG_RD(ctx->base + REG_UD_CFG1) & 0xfff;
		ud->u4 = (DPU_REG_RD(ctx->base + REG_UD_CFG1) >> 16) & 0x3f;
		ud->u5 = (DPU_REG_RD(ctx->base + REG_UD_CFG1) >> 24) & 0x3f;
		pr_info("enhance ud get\n");
		break;
	case ENHANCE_CFG_ID_CABC_HIST_V2:
		p32 = param;
		for (i = 0; i < 64; i++) {
			*p32++ = DPU_REG_RD(ctx->base + REG_CABC_HIST0 + i * DPU_REG_SIZE);
			udelay(1);
		}
		break;
	case ENHANCE_CFG_ID_CABC_CUR_BL:
		p16 = param;
		*p16 = enhance->cabc_para.cur_bl;
		break;
	case ENHANCE_CFG_ID_VSYNC_COUNT:
		vsynccount = param;
		*vsynccount = ctx->vsync_count;
		break;
	case ENHANCE_CFG_ID_FRAME_NO:
		frameno = param;
		*frameno = enhance->frame_no;
		break;
	case ENHANCE_CFG_ID_CABC_STATE:
		p32 = param;
		*p32 = enhance->cabc_state;
		break;
	case ENHANCE_CFG_ID_UPDATE_LUTS:
		dpu_luts_copyto_user(param, enhance);
		break;
	default:
		break;
	}
}

static void dpu_enhance_reload(struct dpu_context *ctx)
{
	struct dpu_enhance *enhance = ctx->enhance;
	struct slp_cfg *slp;
	struct epf_cfg *epf;
	struct ud_cfg *ud;
	int i;
	u16 *p16;

	p16 = (u16 *)enhance->lut_slp_vaddr;
	for (i = 0; i < 256; i++)
		*p16++ = slp_lut[i];

	DPU_REG_SET(ctx->base + REG_ENHANCE_UPDATE, BIT(4));

	if (enhance->enhance_en & BIT(0)) {
		epf = &enhance->epf_copy;
		dpu_epf_set(ctx, epf);
		pr_info("enhance epf reload\n");
	}

	if (enhance->enhance_en & BIT(1)) {
		DPU_REG_WR(ctx->base + REG_HSV_CFG, enhance->hsv_cfg_copy);
		DPU_REG_WR(ctx->base + REG_HSV_LUT_BASE_ADDR, enhance->lut_addrs_cpy.lut_hsv_addr);
		DPU_REG_SET(ctx->base + REG_ENHANCE_UPDATE, BIT(2));
		pr_info("enhance hsv reload\n");
	}

	if (enhance->enhance_en & BIT(2)) {
		dpu_cm_set(ctx, CM_PQ);
		pr_info("enhance cm reload\n");
	}

	if (enhance->enhance_en & BIT(6)) {
		slp = &enhance->slp_copy;
		DPU_REG_WR(ctx->base + REG_SLP_CFG0, (slp->s0 << 0) |
			((slp->s1 & 0x7f) << 16));
		DPU_REG_WR(ctx->base + REG_SLP_CFG1, ((slp->s5 & 0x7f) << 21) |
			((slp->s4 & 0x7f) << 14) |
			((slp->s3 & 0x7f) << 7) |
			((slp->s2 & 0x7f) << 0));
		DPU_REG_WR(ctx->base + REG_SLP_CFG2, ((slp->s9 & 0x7f) << 25) |
			((slp->s8 & 0x7f) << 18) |
			((slp->s7 & 0x3) << 16) |
			((slp->s6 & 0x7f) << 9));
		DPU_REG_WR(ctx->base + REG_SLP_CFG3, ((slp->s14 & 0xfff) << 19) |
			((slp->s13 & 0xf) << 15) |
			((slp->s12 & 0xf) << 11) |
			((slp->s11 & 0xf) << 7) |
			((slp->s10 & 0xf) << 3));
		DPU_REG_WR(ctx->base + REG_SLP_CFG4, (slp->s18 << 24) |
			(slp->s17 << 16) | (slp->s16 << 8) |
			(slp->s15 << 0));
		DPU_REG_WR(ctx->base + REG_SLP_CFG5, (slp->s22 << 24) |
			(slp->s21 << 16) | (slp->s20 << 8) |
			(slp->s19 << 0));
		DPU_REG_WR(ctx->base + REG_SLP_CFG6, (slp->s26 << 24) |
			(slp->s25 << 16) | (slp->s24 << 8) |
			(slp->s23 << 0));
		DPU_REG_WR(ctx->base + REG_SLP_CFG7, ((slp->s29 & 0x1ff) << 23) |
			((slp->s28 & 0x1ff) << 14) |
			((slp->s27 & 0x1ff) << 5));
		DPU_REG_WR(ctx->base + REG_SLP_CFG8, ((slp->s32 & 0x1ff) << 23) |
			((slp->s31 & 0x1ff) << 14) |
			((slp->s30 & 0x1fff) << 0));
		DPU_REG_WR(ctx->base + REG_SLP_CFG9, ((slp->s36 & 0x7f) << 25) |
			((slp->s35 & 0xff) << 17) |
			((slp->s34 & 0xf) << 13) |
			((slp->s33 & 0x7f) << 6));
		DPU_REG_WR(ctx->base + REG_SLP_CFG10, (slp->s38 << 8)|
			(slp->s37 << 0));
		pr_info("enhance slp reload\n");
	}

	if (enhance->enhance_en & BIT(3)) {
		DPU_REG_WR(ctx->base + REG_GAMMA_LUT_BASE_ADDR,
			enhance->lut_addrs_cpy.lut_gamma_addr);
		DPU_REG_SET(ctx->base + REG_ENHANCE_UPDATE, BIT(1));
		pr_info("enhance gamma reload\n");
	}

	if (enhance->enhance_en & BIT(7)) {
		slp = &enhance->slp_copy;
		DPU_REG_WR(ctx->base + REG_SLP_CFG8, ((slp->s32 & 0x1ff) << 23) |
			((slp->s31 & 0x1ff) << 14) |
			((slp->s30 & 0x1fff) << 0));
		pr_info("enhance ltm reload\n");
	}

	if (enhance->enhance_en & BIT(4)) {
		DPU_REG_WR(ctx->base + REG_THREED_LUT_BASE_ADDR,
			enhance->lut_addrs_cpy.lut_lut3d_addr);
		DPU_REG_SET(ctx->base + REG_ENHANCE_UPDATE, BIT(3));
		pr_info("enhance lut3d reload\n");
	}

	if (enhance->enhance_en & BIT(9)) {
		DPU_REG_WR(ctx->base + REG_CABC_CFG0, cabc_cfg0);
		DPU_REG_WR(ctx->base + REG_CABC_CFG1, cabc_cfg1);
		DPU_REG_WR(ctx->base + REG_CABC_CFG2, cabc_cfg2);
		DPU_REG_WR(ctx->base + REG_CABC_CFG3, cabc_cfg3);
		DPU_REG_WR(ctx->base + REG_CABC_CFG4, cabc_cfg4);
		pr_info("enhance cabc cfg reload\n");
	}

	if (enhance->enhance_en & BIT(10)) {
		ud = &enhance->ud_copy;
		DPU_REG_WR(ctx->base + REG_UD_CFG0, ud->u0 | (ud->u1 << 16) | (ud->u2 << 24));
		DPU_REG_WR(ctx->base + REG_UD_CFG1, ud->u3 | (ud->u4 << 16) | (ud->u5 << 24));
		pr_info("enhance ud reload\n");
	}

	DPU_REG_WR(ctx->base + REG_DPU_ENHANCE_CFG, enhance->enhance_en);
	enhance->first_frame = true;
}

static int dpu_cabc_trigger(struct dpu_context *ctx)
{
	struct dpu_enhance *enhance = ctx->enhance;
	struct device_node *backlight_node;
	int i;

	/*
	 * add the cabc_hist outliers filtering,
	 * if the 64 elements of cabc_hist are equal to 0, then end the cabc_trigger
	 * process and return 0 directly.
	 */

	if (enhance->frame_no != 0) {
		for (i = 0; i < 64; i++)
			if (DPU_REG_RD(ctx->base + REG_CABC_HIST0 + i * DPU_REG_SIZE) != 0)
				break;

		if (i == 64)
			return 0;
	}

	if (enhance->cabc_state != CABC_WORKING) {
		if ((enhance->cabc_state == CABC_STOPPING) && enhance->bl_dev) {
			memset(&enhance->cabc_para, 0, sizeof(enhance->cabc_para));
			DPU_REG_WR(ctx->base + REG_CABC_CFG0, cabc_cfg0);
			DPU_REG_WR(ctx->base + REG_CABC_CFG1, cabc_cfg1);
			DPU_REG_WR(ctx->base + REG_CABC_CFG2, cabc_cfg2);
			DPU_REG_WR(ctx->base + REG_CABC_CFG3, cabc_cfg3);
			DPU_REG_WR(ctx->base + REG_CABC_CFG4, cabc_cfg4);
			enhance->cabc_bl_set = true;
			enhance->frame_no = 0;
			enhance->cabc_state = CABC_DISABLED;
			enhance->enhance_en = DPU_REG_RD(ctx->base + REG_DPU_ENHANCE_CFG);
			enhance->enhance_en &= ~(BIT(9));
			DPU_REG_WR(ctx->base + REG_DPU_ENHANCE_CFG, enhance->enhance_en);
			pr_info("enhance cabc disable\n");
		}
		return 0;
	}

	if (enhance->frame_no == 0) {
		if (!enhance->bl_dev) {
			backlight_node = of_parse_phandle(enhance->g_np,
						 "sprd,backlight", 0);
			if (backlight_node) {
				enhance->bl_dev =
				of_find_backlight_by_node(backlight_node);
				of_node_put(backlight_node);
			} else {
				pr_warn("dpu backlight node not found\n");
			}
		}
		if (enhance->bl_dev)
			sprd_backlight_normalize_map(enhance->bl_dev, &enhance->cabc_para.cur_bl);

		DPU_REG_WR(ctx->base + REG_CABC_CFG0, cabc_cfg0);
		DPU_REG_WR(ctx->base + REG_CABC_CFG1, cabc_cfg1);
		DPU_REG_WR(ctx->base + REG_CABC_CFG2, cabc_cfg2);
		DPU_REG_WR(ctx->base + REG_CABC_CFG3, cabc_cfg3);
		DPU_REG_WR(ctx->base + REG_CABC_CFG4, cabc_cfg4);
		enhance->enhance_en |= BIT(9);
		DPU_REG_WR(ctx->base + REG_DPU_ENHANCE_CFG, enhance->enhance_en);
		pr_info("enhance cabc working\n");
		enhance->frame_no++;
	} else {
		DPU_REG_WR(ctx->base + REG_CABC_CFG0, enhance->cabc_para.cfg0);
		DPU_REG_WR(ctx->base + REG_CABC_CFG1, enhance->cabc_para.cfg1);
		DPU_REG_WR(ctx->base + REG_CABC_CFG2, enhance->cabc_para.cfg2);
		DPU_REG_WR(ctx->base + REG_CABC_CFG3, enhance->cabc_para.cfg3);
		DPU_REG_WR(ctx->base + REG_CABC_CFG4, enhance->cabc_para.cfg4);

		if (enhance->bl_dev)
			enhance->cabc_bl_set = true;

		if (enhance->frame_no == 1)
			enhance->frame_no++;
	}
	return 0;
}

static void dpu_sr_config(struct dpu_context *ctx)
{
	struct scale_config_param *scale_cfg = &ctx->scale_cfg;
	u32 reg_val;

	reg_val = (scale_cfg->in_h << 16) | scale_cfg->in_w;
	DPU_REG_WR(ctx->base + REG_BLEND_SIZE, reg_val);
	if (scale_cfg->need_scale)
		DPU_REG_SET(ctx->base + REG_DPU_SCL_EN, BIT_DPU_SCALING_EN);
	else
		DPU_REG_CLR(ctx->base + REG_DPU_SCL_EN, BIT_DPU_SCALING_EN);

	ctx->wb_pending = false;
}

static int dpu_modeset(struct dpu_context *ctx,
		struct drm_display_mode *mode)
{
	struct scale_config_param *scale_cfg = &ctx->scale_cfg;
	struct sprd_dpu *dpu = container_of(ctx, struct sprd_dpu, ctx);
	struct sprd_panel *panel =
		(struct sprd_panel *)container_of(dpu->dsi->panel, struct sprd_panel, base);
	struct sprd_crtc_state *state = to_sprd_crtc_state(dpu->crtc->base.state);

	scale_cfg->in_w = mode->hdisplay;
	scale_cfg->in_h = mode->vdisplay;

	if (state->resolution_change) {
		if ((mode->hdisplay != panel->info.mode.hdisplay) || (mode->vdisplay != panel->info.mode.vdisplay))
			scale_cfg->need_scale = true;
		else
			scale_cfg->need_scale = false;
		ctx->wb_pending = true;
		dpu->crtc->sr_mode_changed = state->resolution_change;
		} else if (state->frame_rate_change) {
		dpu->crtc->fps_mode_changed = state->frame_rate_change;
	}

	ctx->wb_size_changed = true;
	pr_info("begin switch to %u x %u\n", mode->hdisplay, mode->vdisplay);

	return 0;
}

static const u32 primary_fmts[] = {
	DRM_FORMAT_XRGB8888, DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ARGB8888, DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGBA8888, DRM_FORMAT_BGRA8888,
	DRM_FORMAT_RGBX8888, DRM_FORMAT_BGRX8888,
	DRM_FORMAT_RGB565, DRM_FORMAT_BGR565,
	DRM_FORMAT_NV12, DRM_FORMAT_NV21,
	DRM_FORMAT_NV16, DRM_FORMAT_NV61,
	DRM_FORMAT_YUV420, DRM_FORMAT_YVU420,
};

static void dpu_capability(struct dpu_context *ctx,
			struct sprd_crtc_capability *cap)
{
	cap->max_layers = 6;
	cap->fmts_ptr = primary_fmts;
	cap->fmts_cnt = ARRAY_SIZE(primary_fmts);
}

const struct dpu_core_ops dpu_r6p0_core_ops = {
	.version = dpu_version,
	.init = dpu_init,
	.fini = dpu_fini,
	.run = dpu_run,
	.stop = dpu_stop,
	.isr = dpu_isr,
	.ifconfig = dpu_dpi_init,
	.capability = dpu_capability,
	.bg_color = dpu_bgcolor,
	.flip = dpu_flip,
	.enable_vsync = enable_vsync,
	.disable_vsync = disable_vsync,
	.enhance_set = dpu_enhance_set,
	.enhance_get = dpu_enhance_get,
	.context_init = dpu_context_init,
	.modeset = dpu_modeset,
	.write_back = dpu_wb_trigger,
	.dma_request = dpu_dma_request,
};
