/*
 * Copyright (C) 2017 Spreadtrum Communications Inc.
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#include <linux/delay.h>
#include <linux/hwspinlock.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/spi/spi.h>
#include <linux/sizes.h>
#if IS_ENABLED(CONFIG_SPRD_SIPC)
#include <linux/sipc.h>
#endif

/* Registers definitions for ADI controller */
#define REG_ADI_CTRL0			0x4
#define REG_ADI_CHN_PRIL		0x8
#define REG_ADI_CHN_PRIH		0xc
#define REG_ADI_INT_EN			0x10
#define REG_ADI_INT_RAW			0x14
#define REG_ADI_INT_MASK		0x18
#define REG_ADI_INT_CLR			0x1c
#define REG_ADI_GSSI_CFG0		0x20
#define REG_ADI_GSSI_CFG1		0x24
#define REG_ADI_RD_CMD			0x28
#define REG_ADI_RD_DATA			0x2c
#define REG_ADI_ARM_FIFO_STS		0x30
#define REG_ADI_STS			0x34
#define REG_ADI_EVT_FIFO_STS		0x38
#define REG_ADI_ARM_CMD_STS		0x3c
#define REG_ADI_CHN_EN			0x40
#define REG_ADI_CHN_ADDR(id)		(0x44 + (id - 2) * 4)
#define REG_ADI_CHN_EN1			0x20c

/* Bits definitions for register REG_ADI_GSSI_CFG0 */
#define BIT_CLK_ALL_ON			BIT(30)

/* Bits definitions for register REG_ADI_RD_DATA */
#define BIT_RD_CMD_BUSY			BIT(31)
#define RD_ADDR_SHIFT			16
#define RD_VALUE_MASK			GENMASK(15, 0)
#define RD_ADDR_MASK			GENMASK(30, 16)

/* Bits definitions for register REG_ADI_ARM_FIFO_STS */
#define BIT_ARM_WR_FREQ			BIT(31)
#define BIT_FIFO_FULL			BIT(11)
#define BIT_FIFO_EMPTY			BIT(10)

/*
 * ADI slave devices include RTC, ADC, regulator, charger, thermal and so on.
 * The slave devices address offset is always 0x8000 and size is 4K.
 */
#define ADI_SLAVE_ADDR_SIZE		SZ_128K
#define ADI_15BIT_CHANNEL_OFFSET	0x20000
#define ADI_CHANNEL_OFFSET		0x8000

/* Timeout (ms) for the trylock of hardware spinlocks */
#define ADI_HWSPINLOCK_TIMEOUT		5000
/*
 * ADI controller has 50 channels including 2 software channels
 * and 48 hardware channels.
 */
#define ADI_HW_CHNS			50

#define ADI_FIFO_DRAIN_TIMEOUT		1000
#define ADI_READ_TIMEOUT		2000
#define ADI_WRITE_TIMEOUT		2000
#define REG_ADDR_LOW_MASK		GENMASK(16, 0)
#define RDBACK_ADDR_OFFSET		2

/* Registers definitions for PMIC watchdog controller */
#define REG_WDG_LOAD_LOW		0x0
#define REG_WDG_LOAD_HIGH		0x4
#define REG_WDG_CTRL			0x8
#define REG_WDG_LOCK			0x20

/* Bits definitions for register REG_WDG_CTRL */
#define BIT_WDG_RUN			BIT(1)
#define BIT_WDG_NEW			BIT(2)
#define BIT_WDG_RST			BIT(3)

/* Bits definitions for register REG_MODULE_EN */
#define BIT_WDG_EN			BIT(2)

/* Registers definitions for PMIC */
#define PMIC_RST_STATUS			0xee8
#define PMIC_MODULE_EN			0xc08
#define PMIC_CLK_EN			0xc18
#define PMIC_WDG_BASE			0x80
#define SC2730_RST_STATUS		0x1bac
#define SC2730_MODULE_EN		0x1808
#define SC2730_CLK_EN			0x1810
#define SC2730_WDT_BASE			0x40
#define SC2721_RST_STATUS		0xed8
#define SC2721_MODULE_EN		0xc08
#define SC2721_CLK_EN			0xc10
#define SC2721_WDG_BASE			0x40
#define SC2720_RST_STATUS		0xe24
#define SC2720_MODULE_EN		0xc08
#define SC2720_CLK_EN			0xc10
#define SC2720_WDG_BASE			0x40
#define UMP9620_RST_STATUS		0x23ac
#define UMP9620_MODULE_EN		0x2008
#define UMP9620_CLK_EN			0x2010
#define UMP9620_WDT_BASE		0x40
#define UMP9620_SWRST_CTRL0             0x23f8
#define UMP9620_SOFT_RST_HW             0x2024
#define SC2730_SWRST_CTRL0              0x1bf8
#define SC2730_SOFT_RST_HW              0x1824
#define SC2721_SWRST_CTRL0              0xf1c
#define SC2721_SOFT_RST_HW              0xc24
#define SC2720_SWRST_CTRL0              0xe68
#define SC2720_SOFT_RST_HW              0xc24
#define REG_RST_EN                      BIT(4)
#define REG_SOFT_RST                    BIT(0)

/* Definition of PMIC reset status register */
#define HWRST_STATUS_SECURITY		0x02
#define HWRST_STATUS_SECBOOT		0x03
#define HWRST_STATUS_BOOTLOADER_PANIC	0x10
#define HWRST_STATUS_RECOVERY		0x20
#define HWRST_STATUS_NORMAL		0x40
#define HWRST_STATUS_ALARM		0x50
#define HWRST_STATUS_SLEEP		0x60
#define HWRST_STATUS_FASTBOOT		0x30
#define HWRST_STATUS_SPECIAL		0x70
#define HWRST_STATUS_PANIC		0x80
#define HWRST_STATUS_CFTREBOOT		0x90
#define HWRST_STATUS_AUTODLOADER	0xa0
#define HWRST_STATUS_IQMODE		0xb0
#define HWRST_STATUS_SPRDISK		0xc0
#define HWRST_STATUS_SILENT             0xd0
#define HWRST_STATUS_FACTORYTEST	0xe0
#define HWRST_STATUS_WATCHDOG		0xf0

/* Use default timeout 50 ms that converts to watchdog values */
#define WDG_LOAD_VAL			((50 * 32768) / 1000)
#define WDG_LOAD_MASK			GENMASK(15, 0)
#define WDG_UNLOCK_KEY			0xe551

/*Adi single soft multi hard*/
#define SPRD_ADI_MAGIC_LEN_MAX          5

struct sprd_adi_variant_data {
	int (*read_check)(u32 val, u32 reg_paddr);
	int (*write_wait)(void __iomem *adi_base);
	u32 channel_offset;
	u32 wdg_base;
	u32 rst_sts;
	u32 wdg_en;
	u32 wdg_clk;
	u32 swrst_base;
	u32 softrst_base;
};

struct sprd_adi {
	struct spi_controller	*ctlr;
	struct device		*dev;
	void __iomem		*base;
	struct hwspinlock	*hwlock;
	unsigned long		slave_vbase;
	unsigned long		slave_pbase;
	struct notifier_block	restart_handler;
	const struct sprd_adi_variant_data *data;
};

static int sprd_adi_check_paddr(struct sprd_adi *sadi, u32 paddr)
{
	if (paddr < sadi->slave_pbase || paddr >
	    (sadi->slave_pbase + ADI_SLAVE_ADDR_SIZE)) {
		dev_err(sadi->dev,
			"slave physical address is incorrect, addr = 0x%x\n",
			paddr);
		return -EINVAL;
	}

	return 0;
}

static unsigned long sprd_adi_to_vaddr(struct sprd_adi *sadi, u32 paddr)
{
	return (paddr - sadi->slave_pbase + sadi->slave_vbase);
}

static int sprd_adi_drain_fifo(struct sprd_adi *sadi)
{
	u32 timeout = ADI_FIFO_DRAIN_TIMEOUT;
	u32 sts;

	do {
		sts = readl_relaxed(sadi->base + REG_ADI_ARM_FIFO_STS);
		if (sts & BIT_FIFO_EMPTY)
			break;

		cpu_relax();
	} while (--timeout);

	if (timeout == 0) {
		dev_err(sadi->dev, "drain write fifo timeout\n");
		return -EBUSY;
	}

	return 0;
}

static int sprd_adi_fifo_is_full(struct sprd_adi *sadi)
{
	return readl_relaxed(sadi->base + REG_ADI_ARM_FIFO_STS) & BIT_FIFO_FULL;
}

static int sprd_adi_read_check(u32 val, u32 reg_paddr)
{
	u32 rd_addr;

	rd_addr = (val & RD_ADDR_MASK) >> RD_ADDR_SHIFT;

	if (rd_addr != (reg_paddr & REG_ADDR_LOW_MASK) >> RDBACK_ADDR_OFFSET) {
		pr_err("ADI read error, reg addr = 0x%x, val = 0x%x\n",
			reg_paddr, val);
		return -EIO;
	}

	return 0;
}

static int sprd_adi_write_wait(void __iomem *adi_base)
{
	int write_timeout = ADI_WRITE_TIMEOUT;
	u32 val;

	do {
		val = readl_relaxed(adi_base + REG_ADI_ARM_FIFO_STS);
		if (!(val & BIT_ARM_WR_FREQ))
			break;

		cpu_relax();
	} while (--write_timeout);

	if (write_timeout == 0) {
		pr_err("ADI write fail\n");
		return -EBUSY;
	}

	return 0;
}

static int sprd_adi_read(struct sprd_adi *sadi, u32 reg_paddr, u32 *read_val)
{
	int read_timeout = ADI_READ_TIMEOUT;
	unsigned long flags;
	u32 val;
	int ret = 0;

	if (sadi->hwlock) {
		ret = hwspin_lock_timeout_irqsave(sadi->hwlock,
						  ADI_HWSPINLOCK_TIMEOUT,
						  &flags);
		if (ret) {
			dev_err(sadi->dev, "get the hw lock failed\n");
			return ret;
		}
	}

	/*
	 * Set the physical register address need to read into RD_CMD register,
	 * then ADI controller will start to transfer automatically.
	 */
	writel_relaxed(reg_paddr, sadi->base + REG_ADI_RD_CMD);

	/*
	 * Wait read operation complete, the BIT_RD_CMD_BUSY will be set
	 * simultaneously when writing read command to register, and the
	 * BIT_RD_CMD_BUSY will be cleared after the read operation is
	 * completed.
	 */
	do {
		val = readl_relaxed(sadi->base + REG_ADI_RD_DATA);
		if (!(val & BIT_RD_CMD_BUSY))
			break;

		cpu_relax();
	} while (--read_timeout);

	if (read_timeout == 0) {
		dev_err(sadi->dev, "ADI read timeout\n");
		ret = -EBUSY;
		goto out;
	}

	/*
	 * The return value before adi r5p0 includes data and read register
	 * address, from bit 0to bit 15 are data, and from bit 16 to bit 30
	 * are read register address. Then we can check the returned register
	 * address to validate data.
	 */
	if (sadi->data->read_check) {
		ret = sadi->data->read_check(val, reg_paddr);
		if (ret < 0)
			goto out;
	}

	*read_val = val & RD_VALUE_MASK;

out:
	if (sadi->hwlock)
		hwspin_unlock_irqrestore(sadi->hwlock, &flags);
	return ret;
}

static int sprd_adi_write(struct sprd_adi *sadi, u32 reg_paddr, u32 val)
{
	unsigned long reg = sprd_adi_to_vaddr(sadi, reg_paddr);
	u32 timeout = ADI_FIFO_DRAIN_TIMEOUT;
	unsigned long flags;
	int ret;

	if (sadi->hwlock) {
		ret = hwspin_lock_timeout_irqsave(sadi->hwlock,
						  ADI_HWSPINLOCK_TIMEOUT,
						  &flags);
		if (ret) {
			dev_err(sadi->dev, "get the hw lock failed\n");
			return ret;
		}
	}

	ret = sprd_adi_drain_fifo(sadi);
	if (ret < 0)
		goto out;

	/*
	 * we should wait for write fifo is empty before writing data to PMIC
	 * registers.
	 */
	do {
		if (!sprd_adi_fifo_is_full(sadi)) {
			writel_relaxed(val, (void __iomem *)reg);
			break;
		}

		cpu_relax();
	} while (--timeout);

	if (timeout == 0) {
		dev_err(sadi->dev, "write fifo is full\n");
		ret = -EBUSY;
		goto out;
	}

	/* we should wait success flag when write for adi r5px */
	if (sadi->data->write_wait)
		ret = sadi->data->write_wait(sadi->base);

out:
	if (sadi->hwlock)
		hwspin_unlock_irqrestore(sadi->hwlock, &flags);
	return ret;
}

static int sprd_adi_transfer_one(struct spi_controller *ctlr,
				 struct spi_device *spi_dev,
				 struct spi_transfer *t)
{
	struct sprd_adi *sadi = spi_controller_get_devdata(ctlr);
	u32 phy_reg, val;
	int ret;

	if (t->rx_buf) {
		phy_reg = *(u32 *)t->rx_buf + sadi->slave_pbase;

		ret = sprd_adi_check_paddr(sadi, phy_reg);
		if (ret)
			return ret;

		ret = sprd_adi_read(sadi, phy_reg, &val);
		if (ret)
			return ret;

		*(u32 *)t->rx_buf = val;
	} else if (t->tx_buf) {
		u32 *p = (u32 *)t->tx_buf;

		/*
		 * Get the physical register address need to write and convert
		 * the physical address to virtual address. Since we need
		 * virtual register address to write.
		 */
		phy_reg = *p++ + sadi->slave_pbase;
		ret = sprd_adi_check_paddr(sadi, phy_reg);
		if (ret)
			return ret;

		val = *p;
		ret = sprd_adi_write(sadi, phy_reg, val);
		if (ret)
			return ret;
	} else {
		dev_err(sadi->dev, "no buffer for transfer\n");
		return -EINVAL;
	}

	return 0;
}

static void sprd_adi_set_wdt_rst_mode(struct sprd_adi *sadi)
{
#if IS_ENABLED(CONFIG_SPRD_WATCHDOG) || IS_ENABLED(CONFIG_SPRD_WATCHDOG_FIQ)
	u32 val = 0;

	/* Init watchdog reset mode */
	sprd_adi_read(sadi, sadi->slave_pbase + sadi->data->rst_sts, &val);
	val |= HWRST_STATUS_WATCHDOG;
	sprd_adi_write(sadi, sadi->slave_pbase + sadi->data->rst_sts, val);
#endif
}

static int sprd_adi_restart_handler(struct notifier_block *this,
				    unsigned long mode, void *cmd)
{
	struct sprd_adi *sadi = container_of(this, struct sprd_adi,
					     restart_handler);
	u32 val = 0, reboot_mode = 0;

	if (!cmd)
		reboot_mode = HWRST_STATUS_NORMAL;
	else if (!strncmp(cmd, "recovery", 8))
		reboot_mode = HWRST_STATUS_RECOVERY;
	else if (!strncmp(cmd, "alarm", 5))
		reboot_mode = HWRST_STATUS_ALARM;
	else if (!strncmp(cmd, "fastsleep", 9))
		reboot_mode = HWRST_STATUS_SLEEP;
	else if (!strncmp(cmd, "bootloader", 10))
		reboot_mode = HWRST_STATUS_FASTBOOT;
	else if (!strncmp(cmd, "panic", 5))
		reboot_mode = HWRST_STATUS_PANIC;
	else if (!strncmp(cmd, "special", 7))
		reboot_mode = HWRST_STATUS_SPECIAL;
	else if (!strncmp(cmd, "cftreboot", 9))
		reboot_mode = HWRST_STATUS_CFTREBOOT;
	else if (!strncmp(cmd, "autodloader", 11))
		reboot_mode = HWRST_STATUS_AUTODLOADER;
	else if (!strncmp(cmd, "iqmode", 6))
		reboot_mode = HWRST_STATUS_IQMODE;
	else if (!strncmp(cmd, "sprdisk", 7))
		reboot_mode = HWRST_STATUS_SPRDISK;
	else if (!strncmp(cmd, "tospanic", 8))
		reboot_mode = HWRST_STATUS_SECURITY;
	else if (!strncmp(cmd, "dm-verity", 9))
		reboot_mode = HWRST_STATUS_SECBOOT;
	else if (!strncmp(cmd, "factorytest", 11))
		reboot_mode = HWRST_STATUS_FACTORYTEST;
	else if (!strncmp(cmd, "silent", 6))
		reboot_mode = HWRST_STATUS_SILENT;
	else
		reboot_mode = HWRST_STATUS_NORMAL;

	/* Record the reboot mode */
	sprd_adi_read(sadi, sadi->slave_pbase + sadi->data->rst_sts, &val);
	val &= ~0xFF;
	val |= reboot_mode;
	sprd_adi_write(sadi, sadi->slave_pbase + sadi->data->rst_sts, val);

	/*enable register reboot mode*/
	sprd_adi_read(sadi, sadi->slave_pbase + sadi->data->swrst_base, &val);
	val |= REG_RST_EN;
	sprd_adi_write(sadi, sadi->slave_pbase + sadi->data->swrst_base, val);

	/*enable soft reboot mode */
	sprd_adi_read(sadi, sadi->slave_pbase + sadi->data->softrst_base, &val);
	val |= REG_SOFT_RST;
	sprd_adi_write(sadi, sadi->slave_pbase + sadi->data->softrst_base, val);

	mdelay(1000);

	dev_emerg(sadi->dev, "Unable to restart system\n");
	return NOTIFY_DONE;
}

static void sprd_adi_power_ssmh(char *adi_supply)
{
	struct device_node *cmdline_node;
	const char *cmd_line, *adi_type;
	char adi_value[SPRD_ADI_MAGIC_LEN_MAX] = "";
	int ret;

	cmdline_node = of_find_node_by_path("/chosen");
	ret = of_property_read_string(cmdline_node, "bootargs", &cmd_line);

	if (ret) {
		pr_err("can't parse bootargs property\n");
		return;
	}

	adi_type = strstr(cmd_line, "power.from.extern=");
	if (!adi_type) {
		pr_err("can't find power.from.extern\n");
		return;
	}

	sscanf(adi_type, "power.from.extern=%s\n", adi_value);
	if (!adi_value[0])
		return;

	strcat(adi_supply, adi_value);
}

static void sprd_adi_hw_init(struct sprd_adi *sadi)
{
	struct device_node *np = sadi->dev->of_node;
	int i, size, chn_cnt;
	const __be32 *list;
	u32 tmp;
	char adi_supply[25] = "sprd,hw-channels";

	/* Set all channels as default priority */
	writel_relaxed(0, sadi->base + REG_ADI_CHN_PRIL);
	writel_relaxed(0, sadi->base + REG_ADI_CHN_PRIH);

	/* Set clock auto gate mode */
	tmp = readl_relaxed(sadi->base + REG_ADI_GSSI_CFG0);
	tmp &= ~BIT_CLK_ALL_ON;
	writel_relaxed(tmp, sadi->base + REG_ADI_GSSI_CFG0);

	/* Set hardware channels setting */
	sprd_adi_power_ssmh(adi_supply);
	dev_info(sadi->dev, "adi supply is %s\n", adi_supply);

	list = of_get_property(np, adi_supply, &size);
	if (!list || !size) {
		dev_info(sadi->dev, "no hw channels setting in node\n");
		return;
	}

	chn_cnt = size / 8;
	for (i = 0; i < chn_cnt; i++) {
		u32 value;
		u32 chn_id = be32_to_cpu(*list++);
		u32 chn_config = be32_to_cpu(*list++);

		/* Channel 0 and 1 are software channels */
		if (chn_id < 2)
			continue;

		writel_relaxed(chn_config, sadi->base +
			       REG_ADI_CHN_ADDR(chn_id));

		if (chn_id < 32) {
			value = readl_relaxed(sadi->base + REG_ADI_CHN_EN);
			value |= BIT(chn_id);
			writel_relaxed(value, sadi->base + REG_ADI_CHN_EN);
		} else if (chn_id < ADI_HW_CHNS) {
			value = readl_relaxed(sadi->base + REG_ADI_CHN_EN1);
			value |= BIT(chn_id - 32);
			writel_relaxed(value, sadi->base + REG_ADI_CHN_EN1);
		}
	}
}

static int sprd_adi_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct sprd_adi_variant_data *data;
	struct spi_controller *ctlr;
	struct sprd_adi *sadi;
	struct resource *res;
	u16 num_chipselect;
	int ret;

	if (!np) {
		dev_err(&pdev->dev, "can not find the adi bus node\n");
		return -ENODEV;
	}

	data = of_device_get_match_data(&pdev->dev);
	if (!data) {
		dev_err(&pdev->dev, "no matching driver data found\n");
		return -EINVAL;
	}

	pdev->id = of_alias_get_id(np, "spi");
	num_chipselect = (__force u16)of_get_child_count(np);

	ctlr = spi_alloc_master(&pdev->dev, sizeof(struct sprd_adi));
	if (!ctlr)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, ctlr);
	sadi = spi_controller_get_devdata(ctlr);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	sadi->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(sadi->base)) {
		ret = PTR_ERR(sadi->base);
		goto put_ctlr;
	}

	sadi->slave_vbase = (__force unsigned long)sadi->base +
			    data->channel_offset;
	sadi->slave_pbase = res->start + data->channel_offset;
	sadi->ctlr = ctlr;
	sadi->dev = &pdev->dev;
	sadi->data = data;
	ret = of_hwspin_lock_get_id(np, 0);
	if (ret > 0 || (IS_ENABLED(CONFIG_HWSPINLOCK) && ret == 0)) {
		sadi->hwlock =
			devm_hwspin_lock_request_specific(&pdev->dev, ret);
		if (!sadi->hwlock) {
			ret = -ENXIO;
			goto put_ctlr;
		}
	} else {
		switch (ret) {
		case -ENOENT:
			dev_info(&pdev->dev, "no hardware spinlock supplied\n");
			break;
		default:
			dev_err(&pdev->dev,
				"failed to find hwlock id, %d\n", ret);
			/* fall-through */
		case -EPROBE_DEFER:
			goto put_ctlr;
		}
	}

	sprd_adi_hw_init(sadi);
	sprd_adi_set_wdt_rst_mode(sadi);

	ctlr->dev.of_node = pdev->dev.of_node;
	ctlr->bus_num = pdev->id;
	ctlr->num_chipselect = num_chipselect;
	ctlr->flags = SPI_MASTER_HALF_DUPLEX;
	ctlr->bits_per_word_mask = 0;
	ctlr->transfer_one = sprd_adi_transfer_one;

	ret = devm_spi_register_controller(&pdev->dev, ctlr);
	if (ret) {
		dev_err(&pdev->dev, "failed to register SPI controller\n");
		goto put_ctlr;
	}

	sadi->restart_handler.notifier_call = sprd_adi_restart_handler;
	sadi->restart_handler.priority = 130;
	ret = register_restart_handler(&sadi->restart_handler);
	if (ret) {
		dev_err(&pdev->dev, "can not register restart handler\n");
		goto put_ctlr;
	}

	return 0;

put_ctlr:
	spi_controller_put(ctlr);
	return ret;
}

static int sprd_adi_remove(struct platform_device *pdev)
{
	struct spi_controller *ctlr = dev_get_drvdata(&pdev->dev);
	struct sprd_adi *sadi = spi_controller_get_devdata(ctlr);

	unregister_restart_handler(&sadi->restart_handler);
	return 0;
}

static struct sprd_adi_variant_data sc9860_data = {
	.read_check = sprd_adi_read_check,
	.channel_offset = ADI_CHANNEL_OFFSET,
	.wdg_base = PMIC_WDG_BASE,
	.rst_sts = PMIC_RST_STATUS,
	.wdg_en = PMIC_MODULE_EN,
	.wdg_clk = PMIC_CLK_EN,
};

static struct sprd_adi_variant_data sharkl3_data = {
	.read_check = sprd_adi_read_check,
	.channel_offset = ADI_CHANNEL_OFFSET,
	.wdg_base = SC2721_WDG_BASE,
	.rst_sts = SC2721_RST_STATUS,
	.wdg_en = SC2721_MODULE_EN,
	.wdg_clk = SC2721_CLK_EN,
	.swrst_base = SC2721_SWRST_CTRL0,
	.softrst_base = SC2721_SOFT_RST_HW,
};

static struct sprd_adi_variant_data pike2_data = {
	.read_check = sprd_adi_read_check,
	.channel_offset = ADI_CHANNEL_OFFSET,
	.wdg_base = SC2720_WDG_BASE,
	.rst_sts = SC2720_RST_STATUS,
	.wdg_en = SC2720_MODULE_EN,
	.wdg_clk = SC2720_CLK_EN,
	.swrst_base = SC2720_SWRST_CTRL0,
	.softrst_base = SC2720_SOFT_RST_HW,
};

static struct sprd_adi_variant_data sharkl5pro_data = {
	.read_check = sprd_adi_read_check,
	.channel_offset = ADI_15BIT_CHANNEL_OFFSET,
	.wdg_base = SC2730_WDT_BASE,
	.rst_sts = SC2730_RST_STATUS,
	.wdg_en = SC2730_MODULE_EN,
	.wdg_clk = SC2730_CLK_EN,
	.swrst_base = SC2730_SWRST_CTRL0,
	.softrst_base = SC2730_SOFT_RST_HW,
};

static struct sprd_adi_variant_data qogirn6pro_data = {
	.write_wait = sprd_adi_write_wait,
	.channel_offset = ADI_15BIT_CHANNEL_OFFSET,
	.wdg_base = UMP9620_WDT_BASE,
	.rst_sts = UMP9620_RST_STATUS,
	.wdg_en = UMP9620_MODULE_EN,
	.wdg_clk = UMP9620_CLK_EN,
	.swrst_base = UMP9620_SWRST_CTRL0,
	.softrst_base = UMP9620_SOFT_RST_HW,
};

static const struct of_device_id sprd_adi_of_match[] = {
	{
		.compatible = "sprd,sc9860-adi",
		.data = &sc9860_data,
	},
	{
		.compatible = "sprd,sharkl3-adi",
		.data = &sharkl3_data,
	},
	{
		.compatible = "sprd,pike2-adi",
		.data = &pike2_data,
	},
	{
		.compatible = "sprd,sharkl5pro-adi",
		.data = &sharkl5pro_data,
	},
	{
		.compatible = "sprd,qogirn6pro-adi",
		.data = &qogirn6pro_data,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, sprd_adi_of_match);

static struct platform_driver sprd_adi_driver = {
	.driver = {
		.name = "sprd-adi",
		.of_match_table = sprd_adi_of_match,
	},
	.probe = sprd_adi_probe,
	.remove = sprd_adi_remove,
};
module_platform_driver(sprd_adi_driver);

MODULE_DESCRIPTION("Spreadtrum ADI Controller Driver");
MODULE_AUTHOR("Baolin Wang <Baolin.Wang@spreadtrum.com>");
MODULE_LICENSE("GPL v2");
