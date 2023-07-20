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

#ifdef CAM_HW_ADPT_LAYER

#define DCAMX_STOP_TIMEOUT             2000
#define DCAM_AXI_STOP_TIMEOUT          2000
#define DCAM_AXIM_AQOS_MASK            (0x30FFFF)
#define IMG_TYPE_RAW10                 0x2B
#define IMG_TYPE_RAW8                  0x2A
#define IMG_TYPE_YUV                   0x1E

static uint32_t g_ltm_bypass = 1;
static atomic_t clk_users;
static int dcamhw_force_copy(void *handle, void *arg);

static int dcamhw_clk_eb(void *handle, void *arg)
{
	int ret = 0;
	struct cam_hw_info *hw = NULL;
	struct cam_hw_soc_info *soc = NULL;

	pr_debug(", E\n");
	if (atomic_inc_return(&clk_users) != 1) {
		pr_info("clk has enabled, users: %d\n",
			atomic_read(&clk_users));
		return 0;
	}

	if (!handle) {
		pr_err("fail to get invalid handle\n");
		return -EINVAL;
	}

	hw = (struct cam_hw_info *)handle;
	soc = hw->soc_dcam;
	ret = clk_set_parent(soc->clk, soc->clk_parent);
	if (ret) {
		pr_err("fail to set clk parent\n");
		clk_set_parent(soc->clk, soc->clk_default);
		return ret;
	}
	ret = clk_prepare_enable(soc->clk);
	if (ret) {
		pr_err("fail to enable clk\n");
		clk_set_parent(soc->clk, soc->clk_default);
		return ret;
	}
	ret = clk_set_parent(soc->axi_clk, soc->axi_clk_parent);
	if (ret) {
		pr_err("fail to set axi_clk parent\n");
		clk_set_parent(soc->axi_clk, soc->axi_clk_parent);
		return ret;
	}
	ret = clk_prepare_enable(soc->axi_clk);
	if (ret) {
		pr_err(" fail to enable axi_clk\n");
		clk_set_parent(soc->axi_clk, soc->axi_clk_default);
		return ret;
	}
	ret = clk_prepare_enable(soc->core_eb);
	if (ret) {
		pr_err("fail to set eb\n");
		clk_disable_unprepare(soc->clk);
		return ret;
	}
	ret = clk_prepare_enable(soc->axi_eb);
	if (ret) {
		pr_err("fail to set dcam axi clk\n");
		clk_disable_unprepare(soc->clk);
		clk_disable_unprepare(soc->core_eb);
	}

	return ret;
}

static int dcamhw_clk_dis(void *handle, void *arg)
{
	int ret = 0;
	struct cam_hw_info *hw = NULL;
	struct cam_hw_soc_info *soc = NULL;

	pr_debug(", E\n");
	if (atomic_dec_return(&clk_users) != 0) {
		pr_info("Other using, users: %d\n",
			atomic_read(&clk_users));
		return 0;
	}

	if (!handle) {
		pr_err("fail to get invalid handle\n");
		return -EINVAL;
	}

	hw = (struct cam_hw_info *)handle;
	soc = hw->soc_dcam;
	clk_set_parent(soc->axi_clk, soc->axi_clk_default);
	clk_disable_unprepare(soc->axi_clk);
	clk_set_parent(soc->clk, soc->clk_default);
	clk_disable_unprepare(soc->clk);
	clk_disable_unprepare(soc->axi_eb);
	clk_disable_unprepare(soc->core_eb);

	return ret;
}

static int dcamhw_axi_init(void *handle, void *arg)
{
	uint32_t time_out = 0;
	uint32_t idx = 0;
	struct cam_hw_info *hw = NULL;
	struct cam_hw_soc_info *soc = NULL;
	struct cam_hw_ip_info *ip = NULL;

	if (!handle || !arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	hw = (struct cam_hw_info *)handle;
	idx = *(uint32_t *)arg;
	ip = hw->ip_dcam[idx];
	soc = hw->soc_dcam;
	/* firstly, stop AXI writing. */
	DCAM_AXIM_MWR(AXIM_CTRL, BIT_24 | BIT_23, (0x3 << 23));

	/* then wait for AHB busy cleared */
	while (++time_out < DCAM_AXI_STOP_TIMEOUT) {
		if (0 == (DCAM_AXIM_RD(AXIM_DBG_STS) & 0x1F00F))
			break;
		udelay(1000);
	}

	if (time_out >= DCAM_AXI_STOP_TIMEOUT) {
		pr_info("dcam axim timeout status 0x%x\n",
			DCAM_AXIM_RD(AXIM_DBG_STS));
	} else {
		/* reset dcam all (0/1/2/bus) */
		regmap_update_bits(soc->cam_ahb_gpr, ip->syscon.all_rst,
			ip->syscon.all_rst_mask, ip->syscon.all_rst_mask);
		udelay(10);
		regmap_update_bits(soc->cam_ahb_gpr, ip->syscon.all_rst,
			ip->syscon.all_rst_mask, ~(ip->syscon.all_rst_mask));
	}

	hw->dcam_ioctl(hw, DCAM_HW_CFG_SET_QOS, NULL);

	/* the end, enable AXI writing */
	DCAM_AXIM_MWR(AXIM_CTRL, BIT_24 | BIT_23, (0x0 << 23));

	return 0;
}

static int dcamhw_qos_set(void *handle, void *arg)
{
	uint32_t reg_val = 0;
	struct cam_hw_info *hw = NULL;
	struct cam_hw_soc_info *soc = NULL;

	if (!handle) {
		pr_err("fail to get invalid handle\n");
		return -EFAULT;
	}

	hw = (struct cam_hw_info *)handle;
	soc = hw->soc_dcam;
	reg_val = (0x0 << 20) | ((soc->arqos_low & 0xF) << 12) | (0x8 << 8) |
		((soc->awqos_high & 0xF) << 4) | (soc->awqos_low & 0xF);
	REG_MWR(soc->axi_reg_base + AXIM_CTRL, DCAM_AXIM_AQOS_MASK, reg_val);

	return 0;
}

static int dcamhw_start(void *handle, void *arg)
{
	int ret = 0;
	struct dcam_hw_start *parm = NULL;
	struct dcam_hw_force_copy copyarg;
	struct dcam_sw_context *sw_ctx = NULL;
	uint32_t force_ids = DCAM_CTRL_ALL;
	uint32_t reg_val = 0;
	uint32_t image_vc = 0;
	uint32_t image_data_type = IMG_TYPE_RAW10;
	uint32_t image_mode = 1;

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	parm = (struct dcam_hw_start *)arg;
	sw_ctx = parm->dcam_sw_context;

	if (parm->format == DCAM_CAP_MODE_YUV)
		image_data_type = IMG_TYPE_YUV;
	if (parm->format == DCAM_CAP_8_BITS)
		image_data_type = IMG_TYPE_RAW8;
	if (parm->raw_callback == 1)
		image_data_type = 0;

	DCAM_REG_WR(parm->idx, DCAM_INT_CLR, 0xFFFFFFFF);
	/* see DCAM_PREVIEW_SOF in dcam_int.h for details */
	if (parm->raw_callback == 1)
		DCAM_REG_WR(parm->idx, DCAM_INT_EN, DCAMINT_IRQ_LINE_EN_NORMAL | BIT(DCAM_SENSOR_SOF));
	else
		DCAM_REG_WR(parm->idx, DCAM_INT_EN, DCAMINT_IRQ_LINE_EN_NORMAL);

	reg_val = ((image_vc & 0x3) << 16) |
		((image_data_type & 0x3F) << 8) | (image_mode & 0x3);
	DCAM_REG_WR(parm->idx, DCAM_IMAGE_CONTROL, reg_val);

	copyarg.id = force_ids;
	copyarg.idx = sw_ctx->hw_ctx_id;
	copyarg.glb_reg_lock = sw_ctx->glb_reg_lock;
	dcamhw_force_copy(handle, &copyarg);
	/* trigger cap_en*/
	DCAM_REG_MWR(parm->idx, DCAM_MIPI_CAP_CFG, BIT_0, 1);

	return ret;
}

static int dcamhw_stop(void *handle, void *arg)
{
	int ret = 0;
	int time_out = DCAMX_STOP_TIMEOUT;
	struct dcam_sw_context *pctx = NULL;
	uint32_t idx = 0;

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	pctx = (struct dcam_sw_context *)arg;
	idx = pctx->hw_ctx_id;

	/* reset  cap_en*/
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_0, 0);
	DCAM_REG_WR(idx, DCAM_PATH_STOP, 0x2DFF);

	DCAM_REG_WR(idx, DCAM_INT_EN, 0);
	DCAM_REG_WR(idx, DCAM_INT_CLR, 0xFFFFFFFF);

	/* wait for AHB path busy cleared */
	while (time_out) {
		ret = DCAM_REG_RD(idx, DCAM_PATH_BUSY) & 0x2FFF;
		if (!ret)
			break;
		udelay(1000);
		time_out--;
	}

	if (time_out == 0)
		pr_err("fail to normal stop, DCAM%d timeout for 2s\n", idx);

	pr_info("dcam%d stop\n", idx);
	return ret;
}

static int dcamhw_cap_disable(void *handle, void *arg)
{
	int ret = 0;
	uint32_t idx = 0;

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	idx = *(uint32_t *)arg;

	/* stop  cap_en*/
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_0, 0);
	return ret;
}

static int dcamhw_auto_copy(void *handle, void *arg)
{
	struct dcam_hw_auto_copy *copyarg = NULL;
	const uint32_t bitmap[] = {
		BIT_1, BIT_5, BIT_7, BIT_9, BIT_11, BIT_13, BIT_15, BIT_17
	};
	uint32_t mask = 0, j;
	uint32_t id;
	unsigned long flags = 0;

	if (unlikely(!arg)) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	copyarg = (struct dcam_hw_auto_copy *)arg;
	id = copyarg->id;
	if (copyarg->idx < DCAM_ID_MAX) {
		for (j = 0; j < 8; j++) {
			if (id & (1 << j))
				mask |= bitmap[j];
		}
	} else {
		mask = 0;
		pr_err("fail to get dev idx 0x%x exceed DCAM_ID_MAX\n",
			copyarg->idx);
	}

	pr_debug("DCAM%u: auto copy 0x%0x, id 0x%x\n", copyarg->idx, mask, id);
	if (mask == 0)
		return -EFAULT;

	spin_lock_irqsave(&copyarg->glb_reg_lock, flags);
	DCAM_REG_MWR(copyarg->idx, DCAM_CONTROL, mask, mask);
	spin_unlock_irqrestore(&copyarg->glb_reg_lock, flags);

	return 0;
}

static int dcamhw_force_copy(void *handle, void *arg)
{
	struct dcam_hw_force_copy *forcpy = NULL;
	const uint32_t bitmap[] = {
		BIT_0, BIT_4, BIT_6, BIT_8, BIT_10, BIT_12, BIT_14, BIT_16
	};
	uint32_t mask = 0, j;
	unsigned long flags = 0;

	if (unlikely(!arg)) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	forcpy = (struct dcam_hw_force_copy *)arg;
	if (forcpy->idx < DCAM_ID_MAX) {
		for (j = 0; j < 8; j++) {
			if (forcpy->id & (1 << j))
				mask |= bitmap[j];
		}
	} else {
		mask = 0;
		pr_err("fail to get dev idx 0x%x exceed DCAM_ID_MAX\n",
			forcpy->idx);
	}

	pr_debug("DCAM%u: force copy 0x%0x, id 0x%x\n", forcpy->idx, mask, forcpy->id);
	if (mask == 0)
		return -EFAULT;

	spin_lock_irqsave(&forcpy->glb_reg_lock, flags);
	DCAM_REG_MWR(forcpy->idx, DCAM_CONTROL, mask, mask);
	spin_unlock_irqrestore(&forcpy->glb_reg_lock, flags);

	return 0;
}

static int dcamhw_reset(void *handle, void *arg)
{
	int ret = 0;
	enum dcam_id idx = 0;
	struct cam_hw_info *hw = NULL;
	struct cam_hw_soc_info *soc = NULL;
	struct cam_hw_ip_info *ip = NULL;
	uint32_t time_out = 0, flag = 0;
	uint32_t bypass, eb;
	uint32_t reset_bit[DCAM_ID_MAX] = {
		BIT(5),
		BIT(4),
		BIT(3)
	};
	uint32_t sts_bit[DCAM_ID_MAX] = {
		BIT(12), BIT(13), BIT(14)
	};

	if (!handle || !arg) {
		pr_err("fail to get input arg\n");
		return -EFAULT;
	}

	idx = *(uint32_t *)arg;
	hw = (struct cam_hw_info *)handle;
	soc = hw->soc_dcam;
	ip = hw->ip_dcam[idx];

	pr_info("DCAM%d: reset.\n", idx);
	/* then wait for AXIM cleared */
	while (++time_out < DCAM_AXI_STOP_TIMEOUT) {
		if (0 == (DCAM_AXIM_RD(AXIM_DBG_STS) & sts_bit[idx]))
			break;
		udelay(1000);
	}

	if (time_out >= DCAM_AXI_STOP_TIMEOUT) {
		pr_info("DCAM%d: reset timeout, axim status 0x%x\n", idx,
			DCAM_AXIM_RD(AXIM_DBG_STS));
	} else {
		flag = reset_bit[idx];
		pr_debug("DCAM%d, rst=0x%x, rst_mask=0x%x flag=0x%x\n",
			idx, ip->syscon.rst, ip->syscon.rst_mask, flag);
		regmap_update_bits(soc->cam_ahb_gpr,
			ip->syscon.rst, ip->syscon.rst_mask, ip->syscon.rst_mask);
		udelay(10);
		regmap_update_bits(soc->cam_ahb_gpr,
			ip->syscon.rst, ip->syscon.rst_mask, ~(ip->syscon.rst_mask));
	}

	DCAM_REG_MWR(idx, DCAM_INT_CLR,
		DCAMINT_IRQ_LINE_MASK, DCAMINT_IRQ_LINE_MASK);
	DCAM_REG_MWR(idx, DCAM_INT_EN,
		DCAMINT_IRQ_LINE_MASK, DCAMINT_IRQ_LINE_MASK);

	/* disable internal logic access sram */
	DCAM_REG_MWR(idx, DCAM_APB_SRAM_CTRL, BIT_0, 0);

	DCAM_REG_WR(idx, DCAM_MIPI_CAP_CFG, 0); /* disable all path */
	DCAM_REG_WR(idx, DCAM_IMAGE_CONTROL, IMG_TYPE_RAW10 << 8 | 0x01);

	eb = 0;
	DCAM_REG_MWR(idx, DCAM_PDAF_CONTROL, BIT_1 | BIT_0, eb);
	DCAM_REG_MWR(idx, DCAM_CROP0_START, BIT_31, eb << 31);

	/* default bypass all blocks */
	bypass = 1;
	DCAM_REG_MWR(idx, DCAM_BAYER_INFO_CFG, BIT_0, bypass);
	DCAM_REG_MWR(idx, DCAM_BLC_PARA_R_B, BIT_31, bypass << 31);
	DCAM_REG_MWR(idx, ISP_RGBG_YRANDOM_PARAMETER0,
		BIT_1, bypass << 1);
	DCAM_REG_MWR(idx, ISP_RGBG_YRANDOM_PARAMETER0,
		BIT_0, bypass);
	DCAM_REG_MWR(idx, ISP_PPI_PARAM, BIT_0, bypass);
	DCAM_REG_MWR(idx, DCAM_LSCM_FRM_CTRL0, BIT_0, bypass);
	DCAM_REG_MWR(idx, DCAM_LENS_LOAD_ENABLE, BIT_0, bypass);
	DCAM_REG_MWR(idx, DCAM_AEM_FRM_CTRL0, BIT_0, bypass);
	DCAM_REG_MWR(idx, DCAM_HIST_FRM_CTRL0, BIT_0, bypass);
	DCAM_REG_MWR(idx, ISP_AFL_PARAM0, BIT_1, bypass << 1);
	DCAM_REG_MWR(idx, ISP_AFL_FRM_CTRL0, BIT_0, bypass);
	DCAM_REG_MWR(idx, DCAM_CROP0_START, BIT_31, 0 << 31);
	DCAM_REG_MWR(idx, ISP_AWBC_GAIN0, BIT_31, bypass << 31);
	DCAM_REG_MWR(idx, ISP_BPC_PARAM, BIT_0, bypass);
	DCAM_REG_MWR(idx, ISP_AFM_FRM_CTRL, BIT_0, bypass);
	DCAM_REG_WR(idx, NR3_FAST_ME_PARAM, 0x109);
	DCAM_REG_MWR(idx, DCAM_GTM_GLB_CTRL, BIT_0, 0);
	DCAM_REG_MWR(idx, DCAM_FBC_CTRL, BIT_0, 0);
	pr_info("DCAM%d: reset end\n", idx);
	sprd_iommu_restore(&hw->soc_dcam->pdev->dev);

	return ret;
}

static int dcamhw_fetch_set(void *handle, void *arg)
{
	int ret = 0;
	uint32_t fetch_pitch;
	struct dcam_hw_fetch_set *fetch = NULL;

	pr_debug("enter.\n");

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	fetch = (struct dcam_hw_fetch_set *)arg;
	/* !0 is loose */
	if (fetch->fetch_info->pack_bits != 0)
		fetch_pitch = (fetch->fetch_info->size.w * 16 + 127) / 128;
	else
		fetch_pitch = (fetch->fetch_info->size.w * 10 + 127) / 128;

	pr_info("size [%d %d], start %d, pitch %d, 0x%x\n",
		fetch->fetch_info->trim.size_x, fetch->fetch_info->trim.size_y,
		fetch->fetch_info->trim.start_x, fetch_pitch, fetch->fetch_info->addr.addr_ch0);
	/* (bitfile)unit 32b,(spec)64b */

	DCAM_REG_MWR(fetch->idx, DCAM_INT_CLR,
		DCAMINT_IRQ_LINE_MASK, DCAMINT_IRQ_LINE_MASK);
	DCAM_REG_MWR(fetch->idx, DCAM_INT_EN,
		DCAMINT_IRQ_LINE_MASK, DCAMINT_IRQ_LINE_MASK);
	DCAM_REG_MWR(fetch->idx,
		DCAM_MIPI_CAP_CFG, BIT_12, 0x1 << 12);
	DCAM_REG_MWR(fetch->idx,
		DCAM_MIPI_CAP_CFG, BIT_1, 0x1 << 1);
	DCAM_REG_MWR(fetch->idx,
		DCAM_MIPI_CAP_CFG, BIT_3, 0x0 << 3);
	DCAM_REG_MWR(fetch->idx, DCAM_BAYER_INFO_CFG,
		BIT_5 | BIT_4, (fetch->fetch_info->pattern & 3) << 4);
	DCAM_AXIM_MWR(IMG_FETCH_CTRL,
		BIT_1 | BIT_0, fetch->fetch_info->pack_bits);
	DCAM_AXIM_MWR(IMG_FETCH_CTRL,
		BIT_3 | BIT_2, fetch->fetch_info->endian << 2);
	DCAM_AXIM_WR(IMG_FETCH_SIZE,
		(fetch->fetch_info->trim.size_y << 16) | (fetch->fetch_info->trim.size_x & 0xffff));
	DCAM_AXIM_WR(IMG_FETCH_X,
		(fetch_pitch << 16) | (fetch->fetch_info->trim.start_x & 0xffff));
	DCAM_AXIM_WR(IMG_FETCH_RADDR, fetch->fetch_info->addr.addr_ch0);

	pr_info("done.\n");

	return ret;
}

static int dcamhw_mipi_cap_set(void *handle, void *arg)
{
	int ret = 0;
	uint32_t idx = 0;
	uint32_t reg_val;
	struct dcam_mipi_info *cap_info = NULL;
	struct dcam_hw_mipi_cap *caparg = NULL;

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	caparg = (struct dcam_hw_mipi_cap *)arg;
	cap_info = &caparg->cap_info;
	idx = caparg->idx;

	/* set mipi interface  */
	if (cap_info->sensor_if != DCAM_CAP_IF_CSI2) {
		pr_err("fail to support sensor if : %d\n",
			cap_info->sensor_if);
		return -EINVAL;
	}

	/* data format */
	if (cap_info->format == DCAM_CAP_MODE_RAWRGB) {
		DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_1, 1 << 1);
		DCAM_REG_MWR(idx, DCAM_BAYER_INFO_CFG,
			BIT_4 | BIT_5, cap_info->pattern << 4);
	} else if (cap_info->format == DCAM_CAP_MODE_YUV) {
		if (unlikely(cap_info->data_bits != DCAM_CAP_8_BITS)) {
			pr_err("fail to get valid data bits for yuv format %d\n",
				cap_info->data_bits);
			return -EINVAL;
		}

		DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_1,  0 << 1);
		DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG,
			BIT_14 | BIT_15, cap_info->pattern << 14);

		/* x & y deci */
		DCAM_REG_MWR(idx, DCAM_MIPI_CAP_FRM_CTRL,
				BIT_9 | BIT_8 | BIT_5 | BIT_4,
				(cap_info->y_factor << 8)
				| (cap_info->x_factor << 4));
	} else {
		pr_err("fail to support capture format: %d\n",
			cap_info->format);
		return -EINVAL;
	}

	/* data mode */
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_3, cap_info->mode << 3);
	/* href */
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_13, cap_info->href << 13);
	/* frame deci */
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_7 | BIT_6,
			cap_info->frm_deci << 6);
	/* MIPI capture start */
	reg_val = (cap_info->cap_size.start_y << 16);
	reg_val |= cap_info->cap_size.start_x;
	DCAM_REG_WR(idx, DCAM_MIPI_CAP_START, reg_val);

	/* MIPI capture end */
	reg_val = (cap_info->cap_size.start_y
			+ cap_info->cap_size.size_y - 1) << 16;
	reg_val |= (cap_info->cap_size.start_x
			+ cap_info->cap_size.size_x - 1);
	DCAM_REG_WR(idx, DCAM_MIPI_CAP_END, reg_val);

	/* frame skip before capture */
	DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG,
			BIT_8 | BIT_9 | BIT_10 | BIT_11,
				cap_info->frm_skip << 8);

	/* for C-phy */
	if (cap_info->is_cphy == 1)
		DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_31, BIT_31);
	DCAM_REG_MWR(idx,
		DCAM_MIPI_CAP_CFG, BIT_12, 0x0 << 12);
	DCAM_REG_MWR(idx,
		DCAM_MIPI_CAP_CFG, BIT_28, 0x0 << 28);
	/* bypass 4in1 */
	if (cap_info->is_4in1) { /* 4in1 use sum, not avrg */
		DCAM_REG_MWR(idx, DCAM_BAYER_INFO_CFG, BIT_1,
						(1) << 1);
		DCAM_REG_MWR(idx, DCAM_BAYER_INFO_CFG, BIT_2, 0 << 2);
	}
	DCAM_REG_MWR(idx, DCAM_BAYER_INFO_CFG, BIT_0, !cap_info->is_4in1);

	/* > 24M */
	if (cap_info->dcam_slice_mode) {
		DCAM_REG_MWR(idx, DCAM_BAYER_INFO_CFG, BIT_2, 1 << 2);
		DCAM_REG_MWR(idx, DCAM_BAYER_INFO_CFG, BIT_0, 0);
	}

	pr_debug("cap size : %d %d %d %d\n",
		cap_info->cap_size.start_x, cap_info->cap_size.start_y,
		cap_info->cap_size.size_x, cap_info->cap_size.size_y);
	pr_debug("cap: frm %d, mode %d, bits %d, pattern %d, href %d\n",
		cap_info->format, cap_info->mode, cap_info->data_bits,
		cap_info->pattern, cap_info->href);
	pr_debug("cap: deci %d, skip %d, x %d, y %d, 4in1 %d\n",
		cap_info->frm_deci, cap_info->frm_skip, cap_info->x_factor,
		cap_info->y_factor, cap_info->is_4in1);

	return ret;
}

static int dcamhw_path_start(void *handle, void *arg)
{
	int ret = 0;
	struct isp_img_rect rect;
	struct dcam_hw_path_start *patharg = NULL;
	uint32_t image_data_type = IMG_TYPE_RAW10;

	pr_debug("enter.");

	if (!arg) {
		pr_err("fail to get valid handle\n");
		return -EFAULT;
	}

	patharg = (struct dcam_hw_path_start *)arg;
	if (patharg->data_bits == DCAM_CAP_8_BITS)
		image_data_type = IMG_TYPE_RAW8;

	switch (patharg->path_id) {
	case  DCAM_PATH_FULL:
		DCAM_REG_MWR(patharg->idx, DCAM_PATH_ENDIAN,
			BIT_17 |  BIT_16, patharg->endian.y_endian << 16);

		DCAM_REG_MWR(patharg->idx, DCAM_FULL_CFG,
			BIT_2 | BIT_3, patharg->pack_bits << 2);
		DCAM_REG_MWR(patharg->idx, DCAM_FULL_CFG, BIT_4, patharg->src_sel << 4);

		/* full_path_en */
		DCAM_REG_MWR(patharg->idx, DCAM_FULL_CFG, BIT_0, (0x1));
		DCAM_REG_MWR(patharg->idx, NR3_FAST_ME_PARAM, BIT_7 | BIT_6,
			(patharg->bayer_pattern & 0x3) << 6);

		if (patharg->cap_info.format == DCAM_CAP_MODE_YUV)
			DCAM_REG_MWR(patharg->idx, DCAM_CAM_BIN_CFG, BIT_0, 0x1);

		break;

	case  DCAM_PATH_BIN:
		DCAM_REG_MWR(patharg->idx, DCAM_PATH_ENDIAN,
			BIT_3 | BIT_2, patharg->endian.y_endian << 2);
		DCAM_REG_MWR(patharg->idx, DCAM_CAM_BIN_CFG,
			BIT_2 | BIT_3, patharg->pack_bits << 2);
		DCAM_REG_MWR(patharg->idx, DCAM_CAM_BIN_CFG,
				BIT_16, !!patharg->slowmotion_count << 16);
		DCAM_REG_MWR(patharg->idx, DCAM_CAM_BIN_CFG,
				BIT_19 | BIT_18 | BIT_17,
				(patharg->slowmotion_count & 7) << 17);
		DCAM_REG_MWR(patharg->idx, DCAM_CAM_BIN_CFG, BIT_0, 0x1);
		DCAM_REG_MWR(patharg->idx, NR3_FAST_ME_PARAM, BIT_7 | BIT_6,
			(patharg->bayer_pattern & 0x3) << 6);
		break;
	case DCAM_PATH_PDAF:
		/* pdaf path en */
		if (patharg->pdaf_path_eb)
			DCAM_REG_MWR(patharg->idx, DCAM_PPE_FRM_CTRL0, BIT_0, 1);
		break;

	case DCAM_PATH_VCH2:
		/* data type for raw picture */
		if (patharg->src_sel)
			DCAM_REG_WR(patharg->idx, DCAM_VC2_CONTROL, image_data_type << 8 | 0x01 << 4);

		DCAM_REG_MWR(patharg->idx, DCAM_PATH_ENDIAN,
			BIT_23 |  BIT_22, patharg->endian.y_endian << 22);

		/*vch2 path en */
		DCAM_REG_MWR(patharg->idx, DCAM_VC2_CONTROL, BIT_0, 1);
		break;

	case DCAM_PATH_VCH3:
		DCAM_REG_MWR(patharg->idx, DCAM_PATH_ENDIAN,
			BIT_25 |  BIT_24, patharg->endian.y_endian << 24);
		/*vch3 path en */
		DCAM_REG_MWR(patharg->idx, DCAM_VC3_CONTROL, BIT_0, 1);
		break;
	case DCAM_PATH_3DNR:
		/*
		 * set default value for 3DNR
		 * nr3_mv_bypass: 0
		 * nr3_channel_sel: 0
		 * nr3_project_mode: 0
		 * nr3_sub_me_bypass: 0x1
		 * nr3_out_en: 0
		 * nr3_ping_pong_en: 0
		 * nr3_bypass: 0
		 */
		rect.x = patharg->in_trim.start_x;
		rect.y = patharg->in_trim.start_y;
		rect.w = patharg->in_trim.size_x;
		rect.h = patharg->in_trim.size_y;
		if (patharg->cap_info.cap_size.size_x < (rect.x + rect.w) ||
			patharg->cap_info.cap_size.size_y < (rect.y + rect.h)) {
			pr_err("fail to get valid dcam 3dnr input rect [%d %d %d %d]\n",
				rect.x, rect.y, rect.w, rect.h);
			break;
		}
		DCAM_REG_WR(patharg->idx, NR3_FAST_ME_PARAM, 0x8);
		dcam_k_3dnr_set_roi(rect, 0/* project_mode=0 */, patharg->idx);
		break;
	default:
		break;
	}

	pr_debug("done\n");
	return ret;
}

static int dcamhw_path_stop(void *handle, void *arg)
{
	int ret = 0;
	uint32_t idx;
	uint32_t reg_val;

	struct dcam_hw_path_stop *patharg = NULL;

	pr_debug("enter.");

	if (!arg) {
		pr_err("fail to get valid handle\n");
		return -EFAULT;
	}

	patharg = (struct dcam_hw_path_stop *)arg;
	idx = patharg->idx;

	switch (patharg->path_id) {
	case  DCAM_PATH_FULL:
		reg_val = 0;
		DCAM_REG_MWR(idx, DCAM_PATH_STOP, BIT_0, 1);
		DCAM_REG_MWR(idx, DCAM_FULL_CFG, BIT_0, 0);
		break;
	case  DCAM_PATH_BIN:
		DCAM_REG_MWR(idx, DCAM_PATH_STOP, BIT_1, 1 << 1);
		DCAM_REG_MWR(idx, DCAM_CAM_BIN_CFG, BIT_0, 0);
		break;
	case  DCAM_PATH_PDAF:
		DCAM_REG_MWR(idx, DCAM_PATH_STOP, BIT_2, 1 << 2);
		DCAM_REG_MWR(idx, DCAM_PPE_FRM_CTRL0, BIT_0, 0);
		break;
	case  DCAM_PATH_VCH2:
		DCAM_REG_MWR(idx, DCAM_PATH_STOP, BIT_3, 1 << 3);
		DCAM_REG_MWR(idx, DCAM_VC2_CONTROL, BIT_0, 0);
		break;
	case  DCAM_PATH_VCH3:
		DCAM_REG_MWR(idx, DCAM_PATH_STOP, BIT_4, 1 << 4);
		DCAM_REG_MWR(idx, DCAM_VC3_CONTROL, BIT_0, 0);
		break;
	default:
		break;
	}

	pr_debug("done\n");
	return ret;
}

static int dcamhw_path_ctrl(void *handle, void *arg)
{
	struct dcam_hw_path_ctrl *pathctl = NULL;

	pathctl = (struct dcam_hw_path_ctrl *)arg;

	switch (pathctl->path_id) {
	case DCAM_PATH_FULL:
		DCAM_REG_MWR(pathctl->idx, DCAM_FULL_CFG, BIT_0, pathctl->type);
		break;
	case DCAM_PATH_BIN:
		DCAM_REG_MWR(pathctl->idx, DCAM_CAM_BIN_CFG, BIT_0, pathctl->type);
		break;
	case DCAM_PATH_PDAF:
		DCAM_REG_MWR(pathctl->idx, DCAM_PPE_FRM_CTRL0, BIT_0, pathctl->type);
		break;
	case DCAM_PATH_VCH2:
		DCAM_REG_MWR(pathctl->idx, DCAM_VC2_CONTROL, BIT_0, pathctl->type);
		break;
	case DCAM_PATH_VCH3:
		DCAM_REG_MWR(pathctl->idx, DCAM_VC3_CONTROL, BIT_0, pathctl->type);
		break;
	default:
		break;
	}

	return 0;
}

static int dcamhw_fetch_start(void *handle, void *arg)
{
	DCAM_AXIM_WR(IMG_FETCH_START, 1);

	return 0;
}

static int dcamhw_rds_phase_info_calc(void *handle, void *arg)
{
	int32_t rtn = 0;
	uint16_t adj_hor = 1, adj_ver = 1;
	uint16_t raw_input_hor = 0;
	uint16_t raw_output_hor = 0;
	uint16_t raw_input_ver = 0;
	uint16_t raw_output_ver = 0;
	int32_t glb_phase_w = 0, glb_phase_h = 0;
	int sphase_w = 0, spixel_w = 0, sphase_h, spixel_h = 0;
	uint8_t raw_tap_hor = 8;
	uint8_t raw_tap_ver = 4;
	int raw_tap = raw_tap_hor * 2;
	int col_tap = raw_tap_ver * 2;
	uint16_t output_slice_start_x = 0, output_slice_start_y = 0;
	struct dcam_rds_slice_ctrl *gphase = NULL;
	struct dcam_hw_calc_rds_phase *info = NULL;

	if (!arg) {
		pr_err("fail to get valid handle\n");
		return -EFAULT;
	}

	info = (struct dcam_hw_calc_rds_phase *)arg;
	gphase = info->gphase;

	raw_input_hor = (uint16_t)(gphase->rds_input_w_global * adj_hor);
	raw_output_hor = (uint16_t)(gphase->rds_output_w_global * adj_hor);
	raw_input_ver = (uint16_t)(gphase->rds_input_h_global * adj_ver);
	raw_output_ver = (uint16_t)(gphase->rds_output_h_global * adj_ver);

	glb_phase_w = (raw_input_hor - raw_output_hor) >> 1;
	glb_phase_h = (raw_input_ver - raw_output_ver) >> 1;

	if (info->slice_id)
		output_slice_start_x = (info->slice_end1 * raw_output_hor - 1 - glb_phase_w) / raw_input_hor + 1;

	sphase_w = glb_phase_w + output_slice_start_x * gphase->rds_input_w_global;

	spixel_w = sphase_w / gphase->rds_output_w_global - raw_tap + 1;

	spixel_w = spixel_w < 0 ? 0 : spixel_w;

	sphase_w -= spixel_w * gphase->rds_output_w_global;

	gphase->rds_init_phase_int0 = (int16_t)(sphase_w / gphase->rds_output_w_global);
	gphase->rds_init_phase_rdm0 = (uint16_t)(sphase_w - gphase->rds_output_w_global * gphase->rds_init_phase_int0);

	sphase_h = glb_phase_h + output_slice_start_y * gphase->rds_input_h_global;
	spixel_h = sphase_h / gphase->rds_output_h_global - col_tap + 1;
	spixel_h = spixel_h < 0 ? 0 : spixel_h;
	sphase_h -= spixel_h * gphase->rds_output_h_global;
	gphase->rds_init_phase_int1 = (int16_t)(sphase_h / gphase->rds_output_h_global);
	gphase->rds_init_phase_rdm1 = (uint16_t)(sphase_h - gphase->rds_output_h_global * gphase->rds_init_phase_int1);

	return rtn;
}

static int dcamhw_path_size_update(void *handle, void *arg)
{
	int ret = 0;
	uint32_t idx;
	uint32_t reg_val;
	struct dcam_hw_path_size *sizearg = NULL;
	struct isp_img_rect rect;

	pr_debug("enter.");
	if (!arg) {
		pr_err("fail to get valid handle\n");
		return -EFAULT;
	}

	sizearg = (struct dcam_hw_path_size *)arg;
	idx = sizearg->idx;

	switch (sizearg->path_id) {
	case DCAM_PATH_FULL:
		if ((sizearg->in_size.w > sizearg->in_trim.size_x) ||
			(sizearg->in_size.h > sizearg->in_trim.size_y)) {

			DCAM_REG_MWR(idx, DCAM_FULL_CFG, BIT_1, 1 << 1);
			reg_val = (sizearg->in_trim.start_y << 16) |
						sizearg->in_trim.start_x;
			DCAM_REG_WR(idx, DCAM_FULL_CROP_START, reg_val);
			reg_val = (sizearg->in_trim.size_y << 16) |
						sizearg->in_trim.size_x;
			DCAM_REG_WR(idx, DCAM_FULL_CROP_SIZE, reg_val);
		} else {
			DCAM_REG_MWR(idx, DCAM_FULL_CFG, BIT_1, 0 << 1);
		}
		break;
	case DCAM_PATH_BIN:
		DCAM_REG_MWR(idx, DCAM_CAM_BIN_CFG,
					BIT_6, sizearg->bin_ratio << 6);
		DCAM_REG_MWR(idx, DCAM_CAM_BIN_CFG,
					BIT_5 | BIT_4,
					(sizearg->scaler_sel & 3) << 4);
		/* set size to path[DCAM_PATH_3DNR]
		 * because, 3dnr set roi need know bin path crop size
		 * 3dnr end_y should <= bin crop.end_y
		 */
		if ((sizearg->in_size.w > sizearg->in_trim.size_x) ||
			(sizearg->in_size.h > sizearg->in_trim.size_y)) {

			reg_val = (sizearg->in_trim.start_y << 16) |
						sizearg->in_trim.start_x;
			DCAM_REG_WR(idx, DCAM_CAM_BIN_CROP_START, reg_val);

			reg_val = (sizearg->in_trim.size_y << 16) |
						sizearg->in_trim.size_x;
			DCAM_REG_WR(idx, DCAM_CAM_BIN_CROP_SIZE, reg_val);
			DCAM_REG_MWR(idx, DCAM_CAM_BIN_CFG, BIT_1, 1 << 1);
		} else {
			/* bypass trim */
			DCAM_REG_MWR(idx, DCAM_CAM_BIN_CFG, BIT_1, 0 << 1);
		}

		if (sizearg->scaler_sel == DCAM_SCALER_RAW_DOWNSISER) {
			uint32_t cnt;
			uint32_t *ptr = (uint32_t *)sizearg->rds_coeff_buf;
			unsigned long addr = RDS_COEF_TABLE_START;

			for (cnt = 0; cnt < sizearg->rds_coeff_size;
				cnt += 4, addr += 4)
				DCAM_REG_WR(idx, addr, *ptr++);

			reg_val = ((sizearg->out_size.h & 0x1fff) << 16) |
						(sizearg->out_size.w & 0x1fff);
			pr_debug("output = 0x%x\n", reg_val);
			DCAM_REG_WR(idx, DCAM_RDS_DES_SIZE, reg_val);
			DCAM_REG_WR(idx, DCAM_RDS_SLICE_CTRL1, reg_val);

			reg_val = ((sizearg->in_trim.size_y & 0x1fff) << 16) |
						(sizearg->in_trim.size_x & 0x1fff);
			pr_debug("input = 0x%x\n", reg_val);
			DCAM_REG_WR(idx, DCAM_RDS_SLICE_CTRL0, reg_val);

			reg_val = (sizearg->rds_init_phase_int1 << 16) | sizearg->rds_init_phase_int0;
			pr_debug("phase_init = 0x%x\n", reg_val);
			DCAM_REG_WR(idx, DCAM_RDS_SLICE_CTRL2, reg_val);
			reg_val = (sizearg->rds_init_phase_rdm1 << 16) | sizearg->rds_init_phase_rdm0;
			pr_debug("rdm0_init = 0x%x\n", reg_val);
			DCAM_REG_WR(idx, DCAM_RDS_SLICE_CTRL3, reg_val);

			sizearg->auto_cpy_id |= DCAM_CTRL_RDS;
		}
		break;
	case DCAM_PATH_3DNR:
		/* reset when zoom */
		rect.x = sizearg->in_trim.start_x;
		rect.y = sizearg->in_trim.start_y;
		rect.w = sizearg->in_trim.size_x;
		rect.h = sizearg->in_trim.size_y;
		if (sizearg->size_x < (rect.x + rect.w) ||
			sizearg->size_y < (rect.y + rect.h)) {
			pr_err("fail to get valid dcam 3dnr input rect[%d %d %d %d]\n",
				rect.x, rect.y, rect.w, rect.h);
			break;
		}
		dcam_k_3dnr_set_roi(rect, 0/* project_mode=0 */, idx);
		break;
	default:
		break;
	}

	pr_debug("done\n");
	return ret;
}

static int dcamhw_full_path_src_sel(void *handle, void *arg)
{
	int ret = 0;
	struct dcam_hw_path_src_sel *patharg = NULL;

	if (!arg) {
		pr_err("fail to get valid handle\n");
		return -EFAULT;
	}

	patharg = (struct dcam_hw_path_src_sel *)arg;

	switch (patharg->src_sel) {
	case ORI_RAW_SRC_SEL:
		DCAM_REG_MWR(patharg->idx, DCAM_FULL_CFG, BIT(4), 0);
		break;
	case PROCESS_RAW_SRC_SEL:
		DCAM_REG_MWR(patharg->idx, DCAM_FULL_CFG, BIT(4), BIT(4));
		break;
	default:
		pr_err("fail to support src_sel %d\n", patharg->src_sel);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int dcamhw_lbuf_share_set(void *handle, void *arg)
{
	int i = 0;
	int ret = 0;
	struct cam_hw_lbuf_share *camarg = (struct cam_hw_lbuf_share *)arg;
	uint32_t dcam0_mipi_en = 0, dcam1_mipi_en = 0;
	uint32_t tb_w[] = {
	/*     dcam0, dcam1 */
		5664, 3264,
		5184, 4160,
		4672, 4672,
		4160, 5184,
		3264, 5664,
	};

	dcam0_mipi_en = DCAM_REG_RD(0, DCAM_MIPI_CAP_CFG) & BIT_0;
	dcam1_mipi_en = DCAM_REG_RD(1, DCAM_MIPI_CAP_CFG) & BIT_0;
	pr_debug("dcam %d offline %d en0 %d en1 %d\n", camarg->idx, camarg->offline_flag,
		dcam0_mipi_en, dcam1_mipi_en);
	if (!camarg->offline_flag && (dcam0_mipi_en || dcam1_mipi_en)) {
		pr_warn("warning: dcam 0/1 already in working\n");
		return 0;
	}

	switch (camarg->idx) {
	case 0:
		if (camarg->width > tb_w[0]) {
			DCAM_AXIM_MWR(DCAM_LBUF_SHARE_MODE, 0x7, 2);
			break;
		}
		for (i = 4; i >= 0; i--) {
			if (camarg->width <= tb_w[i * 2]) {
				DCAM_AXIM_MWR(DCAM_LBUF_SHARE_MODE, 0x7, i);
				pr_debug("alloc dcam linebuf %d %d\n", tb_w[i*2], tb_w[i*2 + 1]);
				break;
			}
		}
		break;
	case 1:
		if (camarg->width > tb_w[9]) {
			DCAM_AXIM_MWR(DCAM_LBUF_SHARE_MODE, 0x7, 2);
			break;
		}
		for (i = 0; i <= 4; i++) {
			if (camarg->width <= tb_w[i * 2 + 1]) {
				DCAM_AXIM_MWR(DCAM_LBUF_SHARE_MODE, 0x7, i);
				pr_debug("alloc dcam linebuf %d %d\n", tb_w[i*2], tb_w[i*2 + 1]);
				break;
			}
		}
		break;
	default:
		pr_err("fail to get valid dcam id %d\n", camarg->idx);
		ret = 1;
	}
	DCAM_AXIM_MWR(DCAM_LBUF_SHARE_MODE, 0x3 << 8, 0 << 8);

	return ret;
}

static int dcamhw_lbuf_share_get(void *handle, void *arg)
{
	int i = 0;
	int ret = 0;
	int idx = 0;
	struct cam_hw_lbuf_share *camarg = (struct cam_hw_lbuf_share *)arg;
	uint32_t tb_w[] = {
	/*     dcam0, dcam1 */
		5664, 3264,
		5184, 4160,
		4672, 4672,
		4160, 5184,
		3264, 5664,
	};

	if (!arg)
		return -EFAULT;

	idx = camarg->idx;
	camarg->width = 3264;

	if (idx > DCAM_ID_1)
		goto exit;

	i = DCAM_AXIM_RD(DCAM_LBUF_SHARE_MODE) & 7;
	if (i < 5)
		camarg->width = tb_w[i * 2 + idx];

exit:
	pr_debug("dcam%d, lbuf %d\n", idx, camarg->width);
	return ret;
}

static int dcamhw_slice_fetch_set(void *handle, void *arg)
{
	int ret = 0;
	uint32_t idx;
	struct img_trim *cur_slice = NULL;
	struct dcam_fetch_info *fetch = NULL;
	struct dcam_hw_slice_fetch *slicearg = NULL;
	uint32_t fetch_pitch, prev_picth, bfp;

	if (!arg) {
		pr_err("fail to check param");
		return -1;
	}

	slicearg = (struct dcam_hw_slice_fetch *)arg;
	fetch = slicearg->fetch;
	cur_slice = slicearg->cur_slice;
	idx = slicearg->idx;

	if (fetch->pack_bits != 0) {
		fetch_pitch = (fetch->size.w * 16 + 127) / 128;
		prev_picth = (slicearg->slice_trim.size_x * 16 + 127) / 128;
		bfp = 8;
	} else {
		fetch_pitch = (fetch->size.w * 10 + 127) / 128;
		prev_picth = (slicearg->slice_trim.size_x * 16 + 127) / 128;
		bfp = 5;
	}
	DCAM_REG_MWR(idx, DCAM_INT_CLR, DCAMINT_IRQ_LINE_MASK, DCAMINT_IRQ_LINE_MASK);
	DCAM_REG_MWR(idx, DCAM_INT_EN, DCAMINT_IRQ_LINE_MASK, DCAMINT_IRQ_LINE_MASK);

	if (slicearg->dcam_slice_mode == CAM_OFFLINE_SLICE_SW) {
		DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_12, 0x1 << 12);
		DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_1, 0x1 << 1);
		DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_3, 0x0 << 3);
		DCAM_REG_MWR(idx, DCAM_BAYER_INFO_CFG, BIT_5 | BIT_4, (fetch->pattern & 3) << 4);
		DCAM_AXIM_MWR(IMG_FETCH_CTRL, BIT_1 | BIT_0, fetch->pack_bits);
		DCAM_AXIM_MWR(IMG_FETCH_CTRL, BIT_3 | BIT_2, fetch->endian << 2);
		DCAM_AXIM_MWR(IMG_FETCH_CTRL, 0xFF << 4, 0x01 << 4);
		DCAM_AXIM_MWR(IMG_FETCH_CTRL, 0x0F << 12, 0x01 << 12);

		DCAM_AXIM_WR(IMG_FETCH_SIZE,
			(slicearg->slice_trim.size_y << 16) | (slicearg->slice_trim.size_x & 0x1fff));
		DCAM_AXIM_WR(IMG_FETCH_X, (fetch_pitch << 16) | (slicearg->slice_trim.start_x & 0x1fff));
		DCAM_AXIM_WR(IMG_FETCH_RADDR, fetch->addr.addr_ch0);

		DCAM_REG_WR(idx, DCAM_CAM_BIN_CFG, (prev_picth << 20) | BIT_5 | BIT_4 | BIT_0);
		DCAM_REG_MWR(idx, DCAM_CAM_BIN_CFG, BIT_2 | BIT_3, fetch->pack_bits << 2);
	} else {
		if (slicearg->slice_count == 2) {
			DCAM_REG_MWR(idx, DCAM_BAYER_INFO_CFG,
				BIT_5 | BIT_4, (fetch->pattern & 3) << 4);
			DCAM_AXIM_MWR(IMG_FETCH_CTRL, BIT_1 | BIT_0, fetch->pack_bits);
			DCAM_AXIM_MWR(IMG_FETCH_CTRL, BIT_3 | BIT_2, fetch->endian << 2);

			/* cfg mipicap */
			DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_28, 0x1 << 28);
			DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_29, 0x1 << 29);
			DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_30, 0x0 << 30);
			DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_12, 0x1 << 12);
			DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_3, 0x0 << 3);
			DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_1, 0x1 << 1);

			DCAM_AXIM_WR(IMG_FETCH_RADDR, fetch->addr.addr_ch0);
			DCAM_AXIM_WR(IMG_FETCH_SIZE,
				(cur_slice->size_y << 16) | ((cur_slice->size_x + DCAM_OVERLAP)& 0xffff));
			DCAM_AXIM_WR(IMG_FETCH_X, (fetch_pitch << 16) | (cur_slice->start_x & 0x1fff));

			/* cfg bin path */
			DCAM_REG_MWR(idx, DCAM_CAM_BIN_CFG,
				(0x3FF << 20) |(1 << 1), (fetch_pitch << 20) | (1 << 1));
			DCAM_REG_WR(idx, DCAM_CAM_BIN_CROP_START, (0<< 16) | (0 & 0x1fff));
			DCAM_REG_WR(idx, DCAM_CAM_BIN_CROP_SIZE,
				((cur_slice->size_y & 0x1fff) << 16) | (cur_slice->size_x & 0x1fff));
		} else {
			uint32_t reg_val = 0;

			DCAM_REG_MWR(idx, DCAM_MIPI_CAP_CFG, BIT_30, 0x1 << 30);
			DCAM_AXIM_WR(IMG_FETCH_RADDR, fetch->addr.addr_ch0
				+ (cur_slice->start_x - DCAM_OVERLAP) * bfp / 4);
			DCAM_AXIM_WR(IMG_FETCH_SIZE,
				(cur_slice->size_y << 16) | ((cur_slice->size_x + DCAM_OVERLAP) & 0x1fff));
			DCAM_AXIM_WR(IMG_FETCH_X, (fetch_pitch << 16) | (0 & 0x1fff));
			DCAM_REG_MWR(idx, DCAM_CAM_BIN_CFG,
				(0x3FF << 20) |(1 << 1), (fetch_pitch << 20) | (1 << 1));
			DCAM_REG_WR(idx, DCAM_CAM_BIN_CROP_START,
				(0 << 16) | (DCAM_OVERLAP & 0x1fff));
			DCAM_REG_WR(idx, DCAM_CAM_BIN_CROP_SIZE,
				((cur_slice->size_y & 0x1fff) << 16) | (cur_slice->size_x & 0x1fff));

			reg_val = DCAM_REG_RD(idx, DCAM_BIN_BASE_WADDR0);
			DCAM_REG_WR(idx, DCAM_BIN_BASE_WADDR0, reg_val + cur_slice->start_x * bfp / 4);
		}
	}

	return ret;
}

static int dcamhw_ebd_set(void *handle, void *arg)
{
	struct dcam_hw_ebd_set *ebd = NULL;

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	ebd = (struct dcam_hw_ebd_set *)arg;
	pr_info("mode:0x%x, vc:0x%x, dt:0x%x\n", ebd->p->mode,
			ebd->p->image_vc, ebd->p->image_dt);
	DCAM_REG_WR(ebd->idx, DCAM_VC2_CONTROL,
		((ebd->p->image_vc & 0x3) << 16) |
		((ebd->p->image_dt & 0x3F) << 8) |
		(ebd->p->mode & 0x3) << 4);

	return 0;
}

static int dcamhw_binning_4in1_set(void *handle, void *arg)
{
	struct dcam_hw_binning_4in1 *binning = NULL;

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	binning = (struct dcam_hw_binning_4in1 *)arg;

	if (binning->binning_4in1_en) {
		DCAM_REG_MWR(binning->idx, DCAM_BAYER_INFO_CFG, BIT_0, 0);
		DCAM_REG_MWR(binning->idx, DCAM_BAYER_INFO_CFG, BIT_1, 1 << 1);
		DCAM_REG_MWR(binning->idx, DCAM_BAYER_INFO_CFG, BIT_2, 0 << 2);
	} else {
		DCAM_REG_MWR(binning->idx, DCAM_BAYER_INFO_CFG, BIT_0, 1);
		DCAM_REG_MWR(binning->idx, DCAM_BAYER_INFO_CFG, BIT_1, 1 << 1);
		DCAM_REG_MWR(binning->idx, DCAM_BAYER_INFO_CFG, BIT_2, 0 << 2);
	}
	return 0;
}

static int dcamhw_sram_ctrl_set(void *handle, void *arg)
{
	struct dcam_hw_sram_ctrl *sramarg = NULL;

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	sramarg = (struct dcam_hw_sram_ctrl *)arg;

	if (sramarg->sram_ctrl_en)
		DCAM_REG_MWR(sramarg->idx, DCAM_APB_SRAM_CTRL, BIT_0, 1);
	else
		DCAM_REG_MWR(sramarg->idx, DCAM_APB_SRAM_CTRL, BIT_0, 0);

	return 0;
}

static int dcamhw_fbc_ctrl(void *handle, void *arg)
{
	struct dcam_hw_fbc_ctrl *fbc_arg = NULL;

	fbc_arg = (struct dcam_hw_fbc_ctrl *)arg;

	DCAM_REG_MWR(fbc_arg->idx, DCAM_FBC_CTRL, 0x7, fbc_arg->fbc_mode);

	return 0;
}

static int dcamhw_fbc_addr_set(void *handle, void *arg)
{
	struct dcam_hw_fbc_addr *fbcadr = NULL;

	fbcadr = (struct dcam_hw_fbc_addr *)arg;

	if (!arg) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	DCAM_REG_WR(fbcadr->idx, DCAM_FBC_PAYLOAD_WADDR, fbcadr->fbc_addr->addr1);
	DCAM_REG_WR(fbcadr->idx, DCAM_FBC_MID_WADDR, fbcadr->fbc_addr->addr2);
	DCAM_REG_WR(fbcadr->idx, fbcadr->addr, fbcadr->fbc_addr->addr3);

	return 0;
}

static int dcamhw_gtm_status_get(void *handle, void *arg)
{
	int val = 0;
	uint32_t idx;

	idx = *(uint32_t *)arg;
	if (idx >= DCAM_ID_MAX) {
		pr_err("fail to get dcam_idx %d\n", idx);
		return -EFAULT;
	}

	val = DCAM_REG_RD(idx, DCAM_GTM_GLB_CTRL) & BIT_0;
	return val;
}

static int dcamhw_gtm_ltm_eb(void *handle, void *arg)
{
	struct cam_hw_gtm_ltm_eb *eb = NULL;
	struct dcam_dev_param *p = NULL;
	uint64_t val = 1;

	eb = (struct cam_hw_gtm_ltm_eb *)arg;
	if (eb->dcam_idx >= DCAM_ID_MAX || eb->isp_idx >= ISP_CONTEXT_SW_NUM) {
		pr_err("fail to get dcam_idx %d isp_idx %d\n", eb->dcam_idx, eb->isp_idx);
		return -EFAULT;
	}

	g_dcam_bypass[eb->dcam_idx] &= (~(1 << _E_GTM));

	val = (~(val << _EISP_LTM));
	g_isp_bypass[eb->isp_idx] &= val;
	p = (struct dcam_dev_param *)(eb->dcam_param);
	if (p)
		dcam_k_gtm_bypass(p, &p->gtm[DCAM_GTM_PARAM_PRE].gtm_info.bypass_info);
	else
		pr_err("fail to get gtm param\n");
	pr_debug("gtm&ltm enable, dcam hw ctx %d, isp sw ctx %d\n", eb->dcam_idx, eb->isp_idx);
	return 0;
}

static int dcamhw_gtm_ltm_dis(void *handle, void *arg)
{
	struct cam_hw_gtm_ltm_dis *dis = NULL;
	uint64_t val = 1;

	dis = (struct cam_hw_gtm_ltm_dis *)arg;

	if (dis->dcam_idx >= DCAM_ID_MAX || dis->isp_idx >= ISP_CONTEXT_SW_NUM) {
		pr_err("fail to get dcam_idx %d isp_idx %d\n", dis->dcam_idx, dis->isp_idx);
		return -EFAULT;
	}

	g_dcam_bypass[dis->dcam_idx] |= (1 << _E_GTM);
	DCAM_REG_MWR(dis->dcam_idx, DCAM_GTM_GLB_CTRL, BIT_0, 0);

	val = (val << _EISP_LTM);
	g_isp_bypass[dis->isp_idx] |= val;
	ISP_REG_MWR(dis->isp_idx, ISP_LTM_MAP_RGB_BASE + ISP_LTM_MAP_PARAM0, BIT_0, 1);

	pr_debug("gtm &ltm disable, dcam hw ctx %d, isp sw ctx %d\n", dis->dcam_idx, dis->isp_idx);
	return 0;
}

static int dcamhw_gtm_update(void *handle, void *arg)
{
	int ret = 0;
	struct cam_hw_gtm_update *gtmarg = NULL;
	struct dcam_hw_auto_copy copyarg;

	if (unlikely(!arg)) {
		pr_err("fail to get valid arg\n");
		return -EFAULT;
	}

	gtmarg = (struct cam_hw_gtm_update *)arg;
	ret = dcam_k_raw_gtm_block(gtmarg->gtm_idx, gtmarg->blk_dcam_pm);
	copyarg.id = DCAM_CTRL_COEF;
	copyarg.idx = gtmarg->idx;
	copyarg.glb_reg_lock = gtmarg->glb_reg_lock;
	gtmarg->hw->dcam_ioctl(gtmarg->hw, DCAM_HW_CFG_AUTO_COPY, &copyarg);

	return ret;
}

static struct dcam_cfg_entry dcam_hw_cfg_func_tab[DCAM_BLOCK_TOTAL] = {
[DCAM_BLOCK_BLC - DCAM_BLOCK_BASE]         = {DCAM_BLOCK_BLC,         dcam_k_cfg_blc},
[DCAM_BLOCK_AEM - DCAM_BLOCK_BASE]         = {DCAM_BLOCK_AEM,         dcam_k_cfg_aem},
[DCAM_BLOCK_AWBC - DCAM_BLOCK_BASE]        = {DCAM_BLOCK_AWBC,        dcam_k_cfg_awbc},
[DCAM_BLOCK_AFM - DCAM_BLOCK_BASE]         = {DCAM_BLOCK_AFM,         dcam_k_cfg_afm},
[DCAM_BLOCK_AFL - DCAM_BLOCK_BASE]         = {DCAM_BLOCK_AFL,         dcam_k_cfg_afl},
[DCAM_BLOCK_LSC - DCAM_BLOCK_BASE]         = {DCAM_BLOCK_LSC,         dcam_k_cfg_lsc},
[DCAM_BLOCK_BPC - DCAM_BLOCK_BASE]         = {DCAM_BLOCK_BPC,         dcam_k_cfg_bpc},
[DCAM_BLOCK_RGBG - DCAM_BLOCK_BASE]        = {DCAM_BLOCK_RGBG,        dcam_k_cfg_rgb_gain},
[DCAM_BLOCK_RGBG_DITHER - DCAM_BLOCK_BASE] = {DCAM_BLOCK_RGBG_DITHER, dcam_k_cfg_rgb_dither},
[DCAM_BLOCK_PDAF - DCAM_BLOCK_BASE]        = {DCAM_BLOCK_PDAF,        dcam_k_cfg_pdaf},
[DCAM_BLOCK_BAYERHIST - DCAM_BLOCK_BASE]   = {DCAM_BLOCK_BAYERHIST,   dcam_k_cfg_bayerhist},
[DCAM_BLOCK_3DNR_ME - DCAM_BLOCK_BASE]     = {DCAM_BLOCK_3DNR_ME,     dcam_k_cfg_3dnr_me},
[DCAM_BLOCK_GTM - DCAM_BLOCK_BASE]         = {DCAM_BLOCK_GTM,         dcam_k_cfg_raw_gtm},
[DCAM_BLOCK_LSCM - DCAM_BLOCK_BASE]        = {DCAM_BLOCK_LSCM,        dcam_k_cfg_lscm},
};

static int dcamhw_block_func_get(void *handle, void *arg)
{
	void *block_func = NULL;
	struct dcam_hw_block_func_get *fucarg = NULL;

	fucarg = (struct dcam_hw_block_func_get *)arg;

	if (fucarg->index < DCAM_BLOCK_TOTAL) {
		block_func = (struct dcam_cfg_entry *)&dcam_hw_cfg_func_tab[fucarg->index];
		fucarg->dcam_entry = block_func;
	}

	if (block_func == NULL)
		pr_err("fail to get valid block func %d\n", DCAM_BLOCK_TYPE);

	return 0;
}

static int dcamhw_blocks_setall(void *handle, void *arg)
{
	uint32_t idx;
	struct dcam_dev_param *p;

	if (arg == NULL) {
		pr_err("fail to get ptr %p\n", arg);
		return -EFAULT;
	}
	p = (struct dcam_dev_param *)arg;
	idx = p->idx;
	dcam_k_awbc_block(p);
	dcam_k_blc_block(p);
	dcam_k_bpc_block(p);
	dcam_k_bpc_ppi_param(p);
	dcam_k_rgb_gain_block(p);
	if (p->non_zsl_cap)
		dcam_k_raw_gtm_block(DCAM_GTM_PARAM_CAP, p);
	else
		dcam_k_raw_gtm_block(DCAM_GTM_PARAM_PRE, p);

	/* simulator should set this block(random) carefully */
	dcam_k_rgb_dither_random_block(p);
	pr_info("dcam%d set all\n", idx);

	return 0;
}

static int dcamhw_blocks_setstatis(void *handle, void *arg)
{
	uint32_t idx;
	struct dcam_dev_param *p;

	if (arg == NULL) {
		pr_err("fail to get ptr %p\n", arg);
		return -EFAULT;
	}
	p = (struct dcam_dev_param *)arg;
	idx = p->idx;

	p->aem.update = 0xff;
	dcam_k_aem_bypass(p);
	dcam_k_aem_mode(p);
	dcam_k_aem_skip_num(p);
	dcam_k_aem_rgb_thr(p);
	dcam_k_aem_win(p);

	dcam_k_afm_block(p);
	dcam_k_afm_mode(p);
	dcam_k_afm_skipnum(p);
	dcam_k_afm_win_info(p);
	dcam_k_afm_bypass(p);

	dcam_k_afl_block(p);

	dcam_k_bayerhist_block(p);

	dcam_k_lscm_monitor(p);
	dcam_k_lscm_bypass(p);

	dcam_k_pdaf(p);
	dcam_k_3dnr_me(p);

	pr_info("dcam%d set statis done\n", idx);
	return 0;
}

static int dcamhw_mipicap_cfg(void *handle, void *arg)
{
	struct dcam_hw_cfg_mipicap *mipiarg = NULL;

	mipiarg = (struct dcam_hw_cfg_mipicap *)arg;

	DCAM_REG_MWR(mipiarg->idx, DCAM_MIPI_CAP_CFG, BIT_30, 0x1 << 30);
	DCAM_REG_MWR(mipiarg->idx, DCAM_MIPI_CAP_CFG, BIT_29, 0x1 << 29);
	DCAM_REG_MWR(mipiarg->idx, DCAM_MIPI_CAP_CFG, BIT_28, 0x1 << 28);
	DCAM_REG_MWR(mipiarg->idx, DCAM_MIPI_CAP_CFG, BIT_12, 0x1 << 12);
	DCAM_REG_MWR(mipiarg->idx, DCAM_MIPI_CAP_CFG, BIT_3, 0x0 << 3);
	DCAM_REG_MWR(mipiarg->idx, DCAM_MIPI_CAP_CFG, BIT_1, 0x1 << 1);
	DCAM_REG_WR(mipiarg->idx, DCAM_MIPI_CAP_END, mipiarg->reg_val);

	return 0;
}

static int dcamhw_bin_mipi_cfg(void *handle, void *arg)
{
	uint32_t reg_val = 0;
	struct dcam_hw_start_fetch *parm = NULL;

	parm = (struct dcam_hw_start_fetch *)arg;

	reg_val = DCAM_REG_RD(parm->idx, DCAM_BIN_BASE_WADDR0);
	DCAM_REG_WR(parm->idx, DCAM_BIN_BASE_WADDR0, reg_val + parm->fetch_pitch*128/8/2);
	DCAM_AXIM_WR(IMG_FETCH_X,
		(parm->fetch_pitch << 16) | ((parm->start_x + parm->size_x/2) & 0x1fff));
	DCAM_REG_MWR(parm->idx,
		DCAM_MIPI_CAP_CFG, BIT_30, 0x0 << 30);

	return 0;
}

static int dcamhw_bin_path_cfg(void *handle, void *arg)
{
	struct dcam_hw_cfg_bin_path *parm = NULL;

	parm = (struct dcam_hw_cfg_bin_path *)arg;

	DCAM_REG_MWR(parm->idx, DCAM_CAM_BIN_CFG, 0x3FF << 20, parm->fetch_pitch << 20);

	DCAM_AXIM_WR(IMG_FETCH_X,
		(parm->fetch_pitch << 16) | (parm->start_x & 0xffff));

	return 0;
}

static int dcamhw_bayer_hist_roi_update(void *handle, void *arg)
{
	struct dcam_dev_param *blk_dcam_pm = NULL;

	blk_dcam_pm = (struct dcam_dev_param *)arg;
	dcam_k_bayerhist_roi(blk_dcam_pm);
	return 0;
}

static int dcamhw_set_store_addr(void *handle, void *arg)
{
	struct dcam_hw_cfg_store_addr *param = NULL;
	uint32_t path_id = 0, addr = 0, idx = 0;

	param = (struct dcam_hw_cfg_store_addr *)arg;
	if (!param) {
		pr_err("fail to get valid handle or arg\n");
		return -1;
	}

	path_id = param->path_id;
	addr = param->reg_addr;
	idx = param->idx;

	switch (path_id) {
	case DCAM_PATH_FULL:
		if (param->in_fmt == DCAM_CAP_MODE_YUV)
			param->frame_addr[1] = param->frame_addr[0] + param->out_size.h * param->out_size.w;
		DCAM_REG_WR(idx, DCAM_FULL_BASE_WADDR, param->frame_addr[0]);
		DCAM_REG_WR(idx, DCAM_BIN_BASE_WADDR0, param->frame_addr[1]);
		break;
	case DCAM_PATH_AEM:
		DCAM_REG_WR(idx, addr, param->frame_addr[0] + STATIS_AEM_HEADER_SIZE);
		break;
	case DCAM_PATH_HIST:
		DCAM_REG_WR(idx, addr, param->frame_addr[0] + STATIS_HIST_HEADER_SIZE);
		break;
	default:
		DCAM_REG_WR(idx, addr, param->frame_addr[0]);
		break;
	}
	return 0;
}

static int dcamhw_get_gtm_hist(void *handle, void *arg)
{
	struct dcam_hw_gtm_hist *param = NULL;
	uint32_t idx = 0;
	uint32_t hist_index = 0;
	uint32_t *buf = NULL;
	uint32_t sum = 0;

	if (!arg) {
		pr_err("fail to get gtm hist\n");
		return -1;
	}

	param = (struct dcam_hw_gtm_hist *)arg;
	idx = param->idx;
	buf = param->value;
	if (!buf) {
		pr_err("fail to get gtm buffer\n");
		return -1;
	}

	for (hist_index = 0; hist_index < GTM_HIST_ITEM_NUM; hist_index++) {
		buf[hist_index] = DCAM_REG_RD(idx, GTM_HIST_CNT + hist_index * 4);
		sum += buf[hist_index];
	}
	buf[GTM_HIST_ITEM_NUM] = sum;
	return 0;
}

static int dcamhw_csi_disconnect(void *handle, void *arg)
{
	return 0;
}

static struct hw_io_ctrl_fun dcam_ioctl_fun_tab[] = {
	{DCAM_HW_CFG_ENABLE_CLK,            dcamhw_clk_eb},
	{DCAM_HW_CFG_DISABLE_CLK,           dcamhw_clk_dis},
	{DCAM_HW_CFG_INIT_AXI,              dcamhw_axi_init},
	{DCAM_HW_CFG_SET_QOS,               dcamhw_qos_set},
	{DCAM_HW_CFG_RESET,                 dcamhw_reset},
	{DCAM_HW_CFG_START,                 dcamhw_start},
	{DCAM_HW_CFG_STOP,                  dcamhw_stop},
	{DCAM_HW_CFG_STOP_CAP_EB,           dcamhw_cap_disable},
	{DCAM_HW_CFG_FETCH_START,           dcamhw_fetch_start},
	{DCAM_HW_CFG_AUTO_COPY,             dcamhw_auto_copy},
	{DCAM_HW_CFG_FORCE_COPY,            dcamhw_force_copy},
	{DCAM_HW_CFG_PATH_START,            dcamhw_path_start},
	{DCAM_HW_CFG_PATH_STOP,             dcamhw_path_stop},
	{DCAM_HW_CFG_PATH_CTRL,             dcamhw_path_ctrl},
	{DCAM_HW_CFG_PATH_SRC_SEL,          dcamhw_full_path_src_sel},
	{DCAM_HW_CFG_PATH_SIZE_UPDATE,      dcamhw_path_size_update},
	{DCAM_HW_CFG_CALC_RDS_PHASE_INFO,   dcamhw_rds_phase_info_calc},
	{DCAM_HW_CFG_MIPI_CAP_SET,          dcamhw_mipi_cap_set},
	{DCAM_HW_CFG_FETCH_SET,             dcamhw_fetch_set},
	{DCAM_HW_CFG_EBD_SET,               dcamhw_ebd_set},
	{DCAM_HW_CFG_BINNING_4IN1_SET,      dcamhw_binning_4in1_set},
	{DCAM_HW_CFG_SRAM_CTRL_SET,         dcamhw_sram_ctrl_set},
	{DCAM_HW_CFG_LBUF_SHARE_SET,        dcamhw_lbuf_share_set},
	{DCAM_HW_CFG_LBUF_SHARE_GET,        dcamhw_lbuf_share_get},
	{DCAM_HW_CFG_SLICE_FETCH_SET,       dcamhw_slice_fetch_set},
	{DCAM_HW_CFG_FBC_CTRL,              dcamhw_fbc_ctrl},
	{DCAM_HW_CFG_FBC_ADDR_SET,          dcamhw_fbc_addr_set},
	{DCAM_HW_CFG_GTM_STATUS_GET,        dcamhw_gtm_status_get},
	{DCAM_HW_CFG_GTM_LTM_EB,            dcamhw_gtm_ltm_eb},
	{DCAM_HW_CFG_GTM_LTM_DIS,           dcamhw_gtm_ltm_dis},
	{DCAM_HW_CFG_GTM_UPDATE,            dcamhw_gtm_update},
	{DCAM_HW_CFG_BLOCK_FUNC_GET,        dcamhw_block_func_get},
	{DCAM_HW_CFG_BLOCKS_SETALL,         dcamhw_blocks_setall},
	{DCAM_HW_CFG_BLOCKS_SETSTATIS,      dcamhw_blocks_setstatis},
	{DCAM_HW_CFG_MIPICAP,               dcamhw_mipicap_cfg},
	{DCAM_HW_CFG_BIN_MIPI,              dcamhw_bin_mipi_cfg},
	{DCAM_HW_CFG_BIN_PATH,              dcamhw_bin_path_cfg},
	{DCAM_HW_CFG_HIST_ROI_UPDATE,       dcamhw_bayer_hist_roi_update},
	{DCAM_HW_CFG_STORE_ADDR,            dcamhw_set_store_addr},
	{DCAM_HW_CFG_GTM_HIST_GET,          dcamhw_get_gtm_hist},
	{DCAM_HW_DISCONECT_CSI,             dcamhw_csi_disconnect},
};

static hw_ioctl_fun dcamhw_ioctl_fun_get(enum dcam_hw_cfg_cmd cmd)
{
	hw_ioctl_fun hw_ctrl = NULL;
	uint32_t total_num = 0;
	uint32_t i = 0;

	total_num = sizeof(dcam_ioctl_fun_tab) / sizeof(struct hw_io_ctrl_fun);
	for (i = 0; i < total_num; i++) {
		if (cmd == dcam_ioctl_fun_tab[i].cmd) {
			hw_ctrl = dcam_ioctl_fun_tab[i].hw_ctrl;
			break;
		}
	}

	return hw_ctrl;
}
#endif
