// SPDX-License-Identifier: GPL-2.0-only
/*
 * Unisoc UMS512 VSP driver
 *
 * Copyright (C) 2019 Unisoc, Inc.
 */
#include <linux/apsys_dvfs.h>
#include <linux/cdev.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/pm_wakeup.h>
#include <linux/regmap.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/sprd_iommu.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/wait.h>
#include <linux/notifier.h>
#include <linux/compat.h>
#include "sprd_vsp.h"
#include "vsp_common.h"
//#include "sprd_dvfs_vsp.h"
#include <sprd_camsys_domain.h>

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "sprd-vsp: " fmt

static unsigned long sprd_vsp_phys_addr;
static void __iomem *sprd_vsp_base;
static void __iomem *vsp_glb_reg_base;

static struct vsp_dev_t vsp_hw_dev;
static atomic_t vsp_instance_cnt = ATOMIC_INIT(0);
static char *vsp_clk_src[] = {
	"clk_src_76m8",
	"clk_src_96m",
	"clk_src_128m",
	"clk_src_153m6",
	"clk_src_192m",
	"clk_src_256m",
	"clk_src_307m2",
	"clk_src_384m",
	"clk_src_512m"
};

static struct clock_name_map_t clock_name_map[ARRAY_SIZE(vsp_clk_src)];
static struct vsp_qos_cfg qos_cfg;
static int max_freq_level = SPRD_VSP_CLK_LEVEL_NUM;

static irqreturn_t vsp_isr(int irq, void *data);
static irqreturn_t vsp_isr_thread(int irq, void *data);

static long vsp_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	int codec_counter = -1;
	u32 mm_eb_reg;
	struct clk *clk_parent;
	unsigned long frequency;
	struct vsp_iommu_map_data mapdata;
	struct vsp_iommu_map_data ummapdata;
	struct vsp_fh *vsp_fp = filp->private_data;
	u8 need_rst_axi = 0;
	u32 tmp_rst_msk = 0;
	int tmp_codec_id = 0;

	if (vsp_fp == NULL) {
		pr_err("%s error occurred, vsp_fp == NULL\n", __func__);
		return -EINVAL;
	}
	switch (cmd) {
	case VSP_CONFIG_FREQ:
		get_user(vsp_hw_dev.freq_div, (int __user *)arg);
		clk_parent = vsp_get_clk_src_name(clock_name_map,
				vsp_hw_dev.freq_div, max_freq_level);
		vsp_hw_dev.vsp_parent_clk = clk_parent;
		pr_debug("VSP_CONFIG_FREQ %d\n", vsp_hw_dev.freq_div);
#if IS_ENABLED(CONFIG_DVFS_APSYS_SPRD)
		if (vsp_hw_dev.freq_div >= max_freq_level)
			vsp_hw_dev.freq_div = max_freq_level - 1;
		frequency = clock_name_map[vsp_hw_dev.freq_div].freq;
		pr_debug("%s,cfg freq %ld\n", __func__, frequency);
		vsp_dvfs_notifier_call_chain(&frequency, 0);
#endif
		break;

	case VSP_GET_FREQ:
		frequency = clk_get_rate(vsp_hw_dev.vsp_clk);
		ret = find_vsp_freq_level(clock_name_map,
			frequency, max_freq_level);
		put_user(ret, (int __user *)arg);
		pr_debug("vsp ioctl VSP_GET_FREQ %d\n", ret);
		break;

	case VSP_ENABLE:
		pr_debug("vsp ioctl VSP_ENABLE\n");
		__pm_stay_awake(vsp_hw_dev.vsp_wakelock);
		vsp_fp->is_wakelock_got = true;

		ret = vsp_clk_enable(&vsp_hw_dev);
		if (ret == 0)
			vsp_fp->is_clock_enabled = 1;
		break;

	case VSP_DISABLE:
		pr_debug("vsp ioctl VSP_DISABLE\n");
		if (vsp_fp->is_clock_enabled == 1) {
			vsp_clr_interrupt_mask(&vsp_hw_dev,
				sprd_vsp_base, vsp_glb_reg_base);
			vsp_fp->is_clock_enabled = 0;
			vsp_clk_disable(&vsp_hw_dev);
		}
		vsp_fp->is_wakelock_got = false;
		__pm_relax(vsp_hw_dev.vsp_wakelock);
		break;

	case VSP_ACQUAIRE:
		pr_debug("vsp ioctl VSP_ACQUAIRE begin\n");
		ret = down_timeout(&vsp_hw_dev.vsp_mutex,
				   msecs_to_jiffies(VSP_AQUIRE_TIMEOUT_MS));
		if (ret) {
			pr_err("vsp error timeout\n");
			/* up(&vsp_hw_dev.vsp_mutex); */
			return ret;
		}

		vsp_hw_dev.vsp_fp = vsp_fp;
		vsp_fp->is_vsp_acquired = 1;
		pr_debug("vsp ioctl VSP_ACQUAIRE end\n");
		break;

	case VSP_RELEASE:
		pr_debug("vsp ioctl VSP_RELEASE\n");
		if (vsp_fp->is_vsp_acquired) {
			vsp_fp->is_vsp_acquired = 0;
			vsp_hw_dev.vsp_fp = NULL;
			up(&vsp_hw_dev.vsp_mutex);
		}
		break;
	case VSP_COMPLETE:
		pr_debug("vsp ioctl VSP_COMPLETE\n");
		ret = wait_event_interruptible_timeout(vsp_fp->wait_queue_work,
						       vsp_fp->condition_work,
						       msecs_to_jiffies
						       (VSP_INIT_TIMEOUT_MS));
		if (ret == -ERESTARTSYS) {
			pr_info("vsp complete -ERESTARTSYS\n");
			vsp_fp->vsp_int_status |= (1 << 30);
			put_user(vsp_fp->vsp_int_status, (int __user *)arg);
			ret = -EINVAL;
		} else {
			vsp_fp->vsp_int_status &= (~(1 << 30));
			if (ret == 0) {
				pr_err("vsp complete  timeout 0x%x\n",
					readl_relaxed(vsp_glb_reg_base +
						VSP_INT_RAW_OFF));
				vsp_fp->vsp_int_status |= (1 << 31);
				ret = -ETIMEDOUT;
				/* clear vsp int */
				vsp_clr_interrupt_mask(&vsp_hw_dev,
					sprd_vsp_base,
					vsp_glb_reg_base);
			} else {
				ret = 0;
			}
			put_user(vsp_fp->vsp_int_status, (int __user *)arg);
			vsp_fp->vsp_int_status = 0;
			vsp_fp->condition_work = 0;
		}
		pr_debug("vsp ioctl VSP_COMPLETE end\n");
		break;

	case VSP_RESET:
		pr_debug("vsp ioctl VSP_RESET\n");

		if ((vsp_hw_dev.version == SHARKL3) ||
			(vsp_hw_dev.version == PIKE2))
			need_rst_axi = (readl_relaxed(vsp_glb_reg_base +
						VSP_AXI_STS_OFF) & 0x7) > 0;
		else
			need_rst_axi = 0;

		if (need_rst_axi) {
			pr_info("vsp_axi_busy");
			switch (vsp_hw_dev.version) {
			case SHARKL3:
				tmp_rst_msk = regs[RESET].mask | BIT(1);
				break;
			case PIKE2:
				tmp_rst_msk = regs[RESET].mask | BIT(12);
				break;
			default:
				tmp_rst_msk = regs[RESET].mask;
				break;
			}
		} else
			tmp_rst_msk = regs[RESET].mask;

		ret = regmap_update_bits(regs[RESET].gpr, regs[RESET].reg,
				   tmp_rst_msk, tmp_rst_msk);
		if (ret) {
			pr_err("regmap_update_bits failed %s, %d\n",
				__func__, __LINE__);
			break;
		}

		ret = regmap_update_bits(regs[RESET].gpr, regs[RESET].reg,
				   tmp_rst_msk, 0);
		if (ret) {
			pr_err("regmap_update_bits failed %s, %d\n",
				__func__, __LINE__);
		}

		if ((vsp_hw_dev.version == PIKE2
			|| vsp_hw_dev.version == SHARKLE
			|| vsp_hw_dev.version == SHARKL3
			|| vsp_hw_dev.version == SHARKL5
			|| vsp_hw_dev.version == ROC1
			|| vsp_hw_dev.version == SHARKL5Pro)
			&& vsp_hw_dev.iommu_exist_flag) {
			vsp_check_pw_status(&vsp_hw_dev);
			sprd_iommu_restore(vsp_hw_dev.vsp_dev);
		}

		if (vsp_hw_dev.vsp_qos_exist_flag) {
			if (vsp_hw_dev.version == SHARKL5Pro
				|| vsp_hw_dev.version == ROC1) {
				writel_relaxed(((qos_cfg.awqos & 0x7) << 29) |
				((qos_cfg.arqos_low & 0x7) << 23),
				vsp_glb_reg_base + qos_cfg.reg_offset);
			} else {
				writel_relaxed(((qos_cfg.awqos & 0x7) << 8) |
				(qos_cfg.arqos_low & 0x7),
				vsp_glb_reg_base + qos_cfg.reg_offset);
			}
		}

		break;

	case VSP_HW_INFO:

		pr_debug("vsp ioctl VSP_HW_INFO\n");

		regmap_read(regs[VSP_DOMAIN_EB].gpr, regs[VSP_DOMAIN_EB].reg,
					&mm_eb_reg);
		mm_eb_reg &= regs[VSP_DOMAIN_EB].mask;

		put_user(mm_eb_reg, (int __user *)arg);

		break;

	case VSP_VERSION:

		pr_debug("vsp version -enter\n");
		put_user(vsp_hw_dev.version, (int __user *)arg);

		break;

	case VSP_GET_IOMMU_STATUS:

		ret = sprd_iommu_attach_device(vsp_hw_dev.vsp_dev);

		break;

	case VSP_GET_IOVA:

		ret =
		    copy_from_user((void *)&mapdata,
				   (const void __user *)arg,
				   sizeof(struct vsp_iommu_map_data));
		if (ret) {
			pr_err("copy mapdata failed, ret %d\n", ret);
			return -EFAULT;
		}
		if (mapdata.fd < 0) {
			pr_err("%s %d, mapdata.fd < 0, INVALID\n", __func__, __LINE__);
			return -EINVAL;
		}

		ret = vsp_get_iova((void *)vsp_fp, &vsp_hw_dev, &mapdata,
					(void __user *)arg);

		break;

	case VSP_FREE_IOVA:

		ret =
		    copy_from_user((void *)&ummapdata,
				   (const void __user *)arg,
				   sizeof(struct vsp_iommu_map_data));
		if (ret) {
			pr_err("copy ummapdata failed, ret %d\n", ret);
			return -EFAULT;
		}
		if (ummapdata.fd < 0) {
			pr_err("%s %d, ummapdata.fd < 0, INVALID\n", __func__, __LINE__);
			return -EINVAL;
		}

		ret = vsp_free_iova((void *)vsp_fp, &vsp_hw_dev, &ummapdata);

		break;

	case VSP_SET_CODEC_ID:
		get_user(tmp_codec_id, (int __user *)arg);
		if (tmp_codec_id < VSP_H264_DEC || tmp_codec_id > VSP_ENC) {
			pr_err("set invalid codec_id %d\n", tmp_codec_id);
			return -EINVAL;
		}
		vsp_fp->codec_id = tmp_codec_id;
		pr_debug("set codec_id %d\n", vsp_fp->codec_id);
		break;

	case VSP_GET_CODEC_COUNTER:

		if (vsp_fp->codec_id < VSP_H264_DEC || vsp_fp->codec_id > VSP_ENC) {
			pr_info("invalid vsp codec_id %d\n", vsp_fp->codec_id);
			return -EINVAL;
		}

		codec_counter = atomic_read(&vsp_instance_cnt);
		put_user(codec_counter, (int __user *)arg);
		pr_debug("total  counter %d current codec-id %d\n",
			codec_counter, vsp_fp->codec_id);
		break;

	case VSP_SET_SCENE:
		get_user(vsp_hw_dev.scene_mode, (int __user *)arg);
		pr_debug("VSP_SET_SCENE_MODE %d\n", vsp_hw_dev.scene_mode);
		break;

	case VSP_GET_SCENE:
		put_user(vsp_hw_dev.scene_mode, (int __user *)arg);
		pr_debug("VSP_GET_SCENE_MODE %d\n", ret);
		break;

	case VSP_SYNC_GSP:
		break;

	default:
		pr_err("bad vsp-ioctl cmd %d\n", cmd);
		return -EINVAL;
	}

	return ret;
}

static irqreturn_t vsp_isr(int irq, void *data)
{
	int ret, status = 0;
	struct vsp_fh *vsp_fp = vsp_hw_dev.vsp_fp;

	if (vsp_fp == NULL) {
		pr_err("%s error occurred, vsp_fp == NULL\n", __func__);
		return IRQ_WAKE_THREAD;
	}

	if (vsp_fp->is_clock_enabled == 0) {
		pr_err(" vsp clk is disabled");
		return IRQ_HANDLED;
	}

	/* check which module occur interrupt and clear corresponding bit */
	ret = vsp_handle_interrupt(&vsp_hw_dev, &status,
		sprd_vsp_base, vsp_glb_reg_base);
	if (ret == IRQ_NONE)
		return IRQ_NONE;

	if (vsp_fp != NULL) {
		vsp_fp->vsp_int_status = status;
		vsp_fp->condition_work = 1;
		wake_up_interruptible(&vsp_fp->wait_queue_work);
	}

	return IRQ_HANDLED;
}

static irqreturn_t vsp_isr_thread(int irq, void *data)
{
	int ret;

	ret = vsp_clk_enable(&vsp_hw_dev);
	if (ret == 0) {
		pr_info("VSP_INT_RAW 0x%x, 0x%x\n",
			readl_relaxed(vsp_glb_reg_base + VSP_INT_RAW_OFF),
			readl_relaxed(sprd_vsp_base + VSP_MMU_INT_RAW_OFF));
		vsp_clr_interrupt_mask(&vsp_hw_dev,
			sprd_vsp_base, vsp_glb_reg_base);

		vsp_clk_disable(&vsp_hw_dev);
	}

	return IRQ_HANDLED;
}

static const struct sprd_vsp_cfg_data sharkle_vsp_data = {
	.version = SHARKLE,
	.max_freq_level = 4,
};

static const struct sprd_vsp_cfg_data pike2_vsp_data = {
	.version = PIKE2,
	.max_freq_level = 4,
};

static const struct sprd_vsp_cfg_data sharkl3_vsp_data = {
	.version = SHARKL3,
	.max_freq_level = 5,
};

static const struct sprd_vsp_cfg_data sharkl5_vsp_data = {
	.version = SHARKL5,
	.max_freq_level = 3,
	.qos_reg_offset = 0x1f8,
};

static const struct sprd_vsp_cfg_data roc1_vsp_data = {
	.version = ROC1,
	.max_freq_level = 4,
	.qos_reg_offset = 0x0194,
};

static const struct sprd_vsp_cfg_data sharkl5pro_vsp_data = {
	.version = SHARKL5Pro,
	.max_freq_level = 3,
	.qos_reg_offset = 0x0194,
};

static const struct of_device_id of_match_table_vsp[] = {
	{.compatible = "sprd,sharkle-vsp", .data = &sharkle_vsp_data},
	{.compatible = "sprd,pike2-vsp", .data = &pike2_vsp_data},
	{.compatible = "sprd,sharkl3-vsp", .data = &sharkl3_vsp_data},
	{.compatible = "sprd,sharkl5-vsp", .data = &sharkl5_vsp_data},
	{.compatible = "sprd,roc1-vsp", .data = &roc1_vsp_data},
	{.compatible = "sprd,sharkl5pro-vsp", .data = &sharkl5pro_vsp_data},
	{},
};

static int vsp_parse_dt(struct platform_device *pdev)
{
	struct device *dev = &(pdev->dev);
	struct device_node *np = dev->of_node;
	struct device_node *qos_np = NULL;
	struct resource *res;
	int i, ret, j = 0;
	char *pname;
	struct regmap *tregmap;
	uint32_t syscon_args[2];

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "no VSP resource\n");
		return -EINVAL;
	}

	sprd_vsp_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(sprd_vsp_base))
		return PTR_ERR(sprd_vsp_base);
	sprd_vsp_phys_addr = res->start;

	vsp_glb_reg_base = sprd_vsp_base + 0x1000;

	dev_info(dev, "sprd_vsp_phys_addr = %lx\n", sprd_vsp_phys_addr);
	dev_info(dev, "sprd_vsp_base = %p\n", sprd_vsp_base);
	dev_info(dev, "vsp_glb_reg_base = %p\n", vsp_glb_reg_base);

	vsp_hw_dev.version = vsp_hw_dev.vsp_cfg_data->version;
	max_freq_level = vsp_hw_dev.vsp_cfg_data->max_freq_level;
	qos_cfg.reg_offset =
	    vsp_hw_dev.vsp_cfg_data->qos_reg_offset;

	vsp_hw_dev.irq = platform_get_irq(pdev, 0);
	vsp_hw_dev.dev_np = np;
	vsp_hw_dev.vsp_dev = dev;

	dev_info(dev, "vsp: irq = 0x%x, version = 0x%0x\n", vsp_hw_dev.irq,
		vsp_hw_dev.version);

	for (i = 0; i < ARRAY_SIZE(tb_name); i++) {
		pname = tb_name[i];

		tregmap = syscon_regmap_lookup_by_phandle_args(np, pname, 2, syscon_args);
		if (IS_ERR(tregmap)) {
			dev_err(dev, "Read Vsp Dts %s regmap fail\n",
				pname);
			regs[i].gpr = NULL;
			regs[i].reg = 0x0;
			regs[i].mask = 0x0;
			continue;
		}
		regs[i].gpr = tregmap;
		regs[i].reg = syscon_args[0];
		regs[i].mask = syscon_args[1];
		dev_info(dev, "VSP syscon[%s]%p, offset 0x%x, mask 0x%x\n",
			pname, regs[i].gpr, regs[i].reg, regs[i].mask);
	}

	for (i = 0; i < ARRAY_SIZE(vsp_clk_src); i++) {
		struct clk *clk_parent;
		unsigned long frequency;

		clk_parent = of_clk_get_by_name(np, vsp_clk_src[i]);
		if (IS_ERR_OR_NULL(clk_parent)) {
			dev_info(dev, "clk %s not found,continue to find next clock\n",
				vsp_clk_src[i]);
			continue;
		}
		frequency = clk_get_rate(clk_parent);

		clock_name_map[j].name = vsp_clk_src[i];
		clock_name_map[j].freq = frequency;
		clock_name_map[j].clk_parent = clk_parent;

		dev_info(dev, "vsp clk in dts file: clk[%d] = (%ld, %s)\n", j,
			frequency, clock_name_map[j].name);
		j++;
	}

	qos_np = of_parse_phandle(np, "sprd,qos", 0);
	if (!qos_np) {
		dev_warn(dev, "can't find vsp qos cfg node\n");
		vsp_hw_dev.vsp_qos_exist_flag = 0;
	} else {
		ret = of_property_read_u8(qos_np, "awqos",
						&qos_cfg.awqos);
		if (ret)
			dev_warn(dev, "read awqos_low failed, use default\n");

		ret = of_property_read_u8(qos_np, "arqos-low",
						&qos_cfg.arqos_low);
		if (ret)
			dev_warn(dev, "read arqos-low failed, use default\n");

		ret = of_property_read_u8(qos_np, "arqos-high",
						&qos_cfg.arqos_high);
		if (ret)
			dev_warn(dev, "read arqos-high failed, use default\n");
		vsp_hw_dev.vsp_qos_exist_flag = 1;
	}
	dev_info(dev, "%x, %x, %x, %x", qos_cfg.awqos, qos_cfg.arqos_high,
		qos_cfg.arqos_low, qos_cfg.reg_offset);

	vsp_hw_dev.iommu_exist_flag =
		(sprd_iommu_attach_device(vsp_hw_dev.vsp_dev) == 0) ? 1 : 0;
	dev_info(dev, "iommu_vsp enabled %d\n", vsp_hw_dev.iommu_exist_flag);

	return 0;
}

static int vsp_nocache_mmap(struct file *filp, struct vm_area_struct *vma)
{
	size_t memsize = vma->vm_end - vma->vm_start;

	if (memsize > SPRD_VSP_MAP_SIZE) {
		pr_err("%s, need:%lx should be:%x ", __func__, memsize, SPRD_VSP_MAP_SIZE);
		return -EINVAL;
	}

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_pgoff = (sprd_vsp_phys_addr >> PAGE_SHIFT);
	if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			    memsize, vma->vm_page_prot))
		return -EAGAIN;

	pr_info("mmap %x,%lx,%x\n", (unsigned int)PAGE_SHIFT,
		(unsigned long)vma->vm_start,
		(unsigned int)(memsize));
	return 0;
}

static int vsp_open(struct inode *inode, struct file *filp)
{
	int ret = 0;
	struct vsp_fh *vsp_fp = kmalloc(sizeof(struct vsp_fh), GFP_KERNEL);
	int instance_cnt = atomic_read(&vsp_instance_cnt);

	pr_info("%s called %p,vsp_instance_cnt %d\n", __func__,
		vsp_fp, instance_cnt);

	if (vsp_fp == NULL) {
		pr_err("vsp open error occurred\n");
		return -EINVAL;
	}
	filp->private_data = vsp_fp;
	vsp_fp->is_clock_enabled = 0;
	vsp_fp->is_vsp_acquired = 0;
	vsp_fp->codec_id = 0;

	init_waitqueue_head(&vsp_fp->wait_queue_work);
	vsp_fp->vsp_int_status = 0;
	vsp_fp->condition_work = 0;

	if (vsp_hw_dev.version == PIKE2)
		ret = sprd_glb_mm_pw_on_cfg();
	pm_runtime_get_sync(vsp_hw_dev.vsp_dev);
	//atomic_read(&vsp_hw_dev.vsp_dev->power.usage_count);

	if (ret != 0) {
		pr_info("%s: vsp power on failed %d !\n", __func__, ret);
		return ret;
	}

	atomic_inc(&vsp_instance_cnt);
	return ret;
}

static int vsp_release(struct inode *inode, struct file *filp)
{
	struct vsp_fh *vsp_fp = filp->private_data;
	int instance_cnt = atomic_read(&vsp_instance_cnt);

	if (vsp_fp == NULL) {
		pr_err("%s error occurred, vsp_fp == NULL\n", __func__);
		return -EINVAL;
	}

	pr_info("%s: instance_cnt %d\n", __func__, instance_cnt);

	atomic_dec(&vsp_instance_cnt);

	if (vsp_fp->is_clock_enabled) {
		pr_err("error occurred and close clock\n");
		vsp_fp->is_clock_enabled = 0;
		vsp_clk_disable(&vsp_hw_dev);
	}

	if (vsp_fp->is_wakelock_got) {
		pr_err("error occurred and wakelock relax\n");
		__pm_relax(vsp_hw_dev.vsp_wakelock);
	}

	if (vsp_fp->is_vsp_acquired)
		up(&vsp_hw_dev.vsp_mutex);

	pm_runtime_mark_last_busy(vsp_hw_dev.vsp_dev);
	if (vsp_hw_dev.version == PIKE2)
		sprd_glb_mm_pw_off_cfg();
	pm_runtime_put_autosuspend(vsp_hw_dev.vsp_dev);

	kfree(filp->private_data);
	filp->private_data = NULL;
	return 0;
}

static const struct file_operations vsp_fops = {
	.owner = THIS_MODULE,
	.mmap = vsp_nocache_mmap,
	.open = vsp_open,
	.release = vsp_release,
	.unlocked_ioctl = vsp_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = compat_vsp_ioctl,
#endif
};

static struct miscdevice vsp_dev = {
	.minor = VSP_MINOR,
	.name = "sprd_vsp",
	.fops = &vsp_fops,
};

static int vsp_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct device_node *node = pdev->dev.of_node;
	const struct of_device_id *of_id;

	of_id = of_match_node(of_match_table_vsp, node);
	if (of_id)
		vsp_hw_dev.vsp_cfg_data =
		    (struct sprd_vsp_cfg_data *)of_id->data;
	else
		panic("%s: Not find matched id!", __func__);
	if (pdev->dev.of_node) {
		if (vsp_parse_dt(pdev)) {
			dev_err(dev, "vsp_parse_dt failed\n");
			return -EINVAL;
		}
	}

	sema_init(&vsp_hw_dev.vsp_mutex, 1);

	vsp_hw_dev.freq_div = max_freq_level;
	vsp_hw_dev.scene_mode = 0;

	vsp_hw_dev.vsp_clk = NULL;
	vsp_hw_dev.clk_ahb_vsp = NULL;
	vsp_hw_dev.clk_emc_vsp = NULL;
	vsp_hw_dev.vsp_parent_clk = NULL;
	vsp_hw_dev.clk_mm_eb = NULL;
	vsp_hw_dev.clk_axi_gate_vsp = NULL;
	vsp_hw_dev.clk_vsp_mq_ahb_eb = NULL;
	vsp_hw_dev.clk_ahb_gate_vsp_eb = NULL;
	vsp_hw_dev.clk_vsp_ahb_mmu_eb = NULL;
	vsp_hw_dev.vsp_fp = NULL;
	vsp_hw_dev.light_sleep_en = false;
	mutex_init(&vsp_hw_dev.map_lock);
	INIT_LIST_HEAD(&vsp_hw_dev.map_list);

	ret = vsp_get_mm_clk(&vsp_hw_dev);
	if (ret) {
		dev_err(dev, "vsp_get_mm_clk error (%d)\n", ret);
		return ret;
	}

	vsp_hw_dev.vsp_parent_df_clk =
		vsp_get_clk_src_name(clock_name_map, 0, max_freq_level);

	ret = misc_register(&vsp_dev);
	if (ret) {
		dev_err(dev, "cannot register miscdev on minor=%d (%d)\n",
		       VSP_MINOR, ret);
		return ret;
	}

	if (dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64))) {
		if (dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32)))
			dev_err(dev, "vsp: failed to set dma mask!\n");
	} else {
		dev_info(dev, "vsp: set dma mask as 64bit\n");
	}

	/* register isr */
	ret = devm_request_threaded_irq(&pdev->dev, vsp_hw_dev.irq, vsp_isr,
			vsp_isr_thread, 0, "VSP", &vsp_hw_dev);
	if (ret) {
		dev_err(dev, "vsp: failed to request irq!\n");
		ret = -EINVAL;
		goto errout;
	}

	vsp_hw_dev.vsp_wakelock = wakeup_source_create("pm_message_wakelock_vsp");
	wakeup_source_add(vsp_hw_dev.vsp_wakelock);

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	return 0;

errout:
	misc_deregister(&vsp_dev);

	return ret;
}

static int vsp_remove(struct platform_device *pdev)
{
	misc_deregister(&vsp_dev);
	devm_free_irq(&pdev->dev, vsp_hw_dev.irq, &vsp_hw_dev);
	return 0;
}

static int vsp_suspend(struct device *dev)
{
	pm_runtime_put_autosuspend(vsp_hw_dev.vsp_dev);
	return 0;
}

static int vsp_resume(struct device *dev)
{
	pm_runtime_get_sync(vsp_hw_dev.vsp_dev);
	return 0;
}

static SIMPLE_DEV_PM_OPS(vsp_sprd_pm_ops, vsp_suspend,
			vsp_resume);

static struct platform_driver vsp_driver = {
	.probe = vsp_probe,
	.remove = vsp_remove,

	.driver = {
		   .name = "sprd_vsp",
		   .pm = &vsp_sprd_pm_ops,
		   .of_match_table = of_match_ptr(of_match_table_vsp),
		   },
};
module_platform_driver(vsp_driver);

MODULE_DESCRIPTION("SPRD VSP Driver");
MODULE_LICENSE("GPL");
