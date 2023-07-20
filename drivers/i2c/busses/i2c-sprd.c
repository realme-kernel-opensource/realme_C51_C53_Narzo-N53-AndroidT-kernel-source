/*
 * Copyright (C) 2017 Spreadtrum Communications Inc.
 *
 * SPDX-License-Identifier: (GPL-2.0+ OR MIT)
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/dmaengine.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/dma/sprd-dma.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/types.h>
#include <linux/reset.h>

#define I2C_CTL			0x00
#define I2C_ADDR_CFG		0x04
#define I2C_COUNT		0x08
#define I2C_RX			0x0c
#define I2C_TX			0x10
#define I2C_STATUS		0x14
#define I2C_HSMODE_CFG		0x18
#define I2C_VERSION		0x1c
#define ADDR_DVD0		0x20
#define ADDR_DVD1		0x24
#define ADDR_STA0_DVD		0x28
#define ADDR_RST		0x2c

/* I2C_CTL */
#define I2C_NACK_EN		BIT(22)
#define I2C_TRANS_EN		BIT(21)
#define STP_EN			BIT(20)
#define FIFO_AF_LVL_MASK	GENMASK(19, 16)
#define FIFO_AF_LVL		16
#define FIFO_AE_LVL_MASK	GENMASK(15, 12)
#define FIFO_AE_LVL		12
#define I2C_DMA_EN		BIT(11)
#define FULL_INTEN		BIT(10)
#define EMPTY_INTEN		BIT(9)
#define I2C_DVD_OPT		BIT(8)
#define I2C_OUT_OPT		BIT(7)
#define I2C_TRIM_OPT		BIT(6)
#define I2C_HS_MODE		BIT(4)
#define I2C_MODE		BIT(3)
#define I2C_EN			BIT(2)
#define I2C_INT_EN		BIT(1)
#define I2C_START		BIT(0)

/* I2C_STATUS */
#define SDA_IN			BIT(21)
#define SCL_IN			BIT(20)
#define FIFO_FULL		BIT(4)
#define FIFO_EMPTY		BIT(3)
#define I2C_INT			BIT(2)
#define I2C_RX_ACK		BIT(1)
#define I2C_BUSY		BIT(0)

/* ADDR_RST */
#define I2C_RST			BIT(0)

#define I2C_FIFO_DEEP		12
#define I2C_FIFO_FULL_THLD	15
#define I2C_FIFO_EMPTY_THLD	4
#define I2C_DATA_STEP		8
#define I2C_ADDR_DVD0_CALC(high, low)	\
	((((high) & GENMASK(15, 0)) << 16) | ((low) & GENMASK(15, 0)))
#define I2C_ADDR_DVD1_CALC(high, low)	\
	(((high) & GENMASK(31, 16)) | (((low) & GENMASK(31, 16)) >> 16))

/* timeout (ms) for pm runtime autosuspend */
#define SPRD_I2C_PM_TIMEOUT	1000
/* timeout (ms) for transfer message */
#define I2C_XFER_TIMEOUT	10000

/* dynamic modify clk_freq flag  */
#define I2C_1M_FLAG		0X0080
#define I2C_400K_FLAG		0X0040

#define SPRD_I2C_DMA_STEP	8

enum sprd_i2c_dma_channel {
	SPRD_I2C_RX,
	SPRD_I2C_TX,
	SPRD_I2C_MAX,
};

/* SPRD i2c dma structure */
struct sprd_i2c_dma {
	bool dma_enable;
	dma_cookie_t cookie;
	struct dma_chan	*chan_tx;
	struct dma_chan	*chan_rx;
	struct dma_chan	*chan_using;
	struct dma_chan	*dma_chan[SPRD_I2C_MAX];
	struct completion	cmd_complete;
	dma_addr_t	dma_buf;
	unsigned int	dma_len;
	dma_addr_t	dma_phys_addr;
	u32	fragmens_len;

};

struct sprd_syscon_i2c {
	struct regmap *regmap;
	u32 reg;
	u32 mask;
};
/* SPRD i2c data structure */
struct sprd_i2c {
	struct i2c_adapter adap;
	struct device *dev;
	void __iomem *base;
	phys_addr_t phy_base;
	struct i2c_msg *msg;
	struct clk *clk;
	u32 src_clk;
	u32 bus_freq;
	struct completion complete;
	u8 *buf;
	u32 count;
	u32 dma_trans_len;
	int irq;
	int err;
	bool ack_flag;
	struct sprd_syscon_i2c i2c_syscon_rst;
	struct sprd_i2c_dma dma;
	struct reset_control *rst;
};

static void sprd_i2c_dump_reg(struct sprd_i2c *i2c_dev)
{
	dev_err(i2c_dev->dev, "I2C_CTL = 0x%x\n", readl(i2c_dev->base + I2C_CTL));
	dev_err(i2c_dev->dev, "I2C_ADDR_CFG = 0x%x\n", readl(i2c_dev->base + I2C_ADDR_CFG));
	dev_err(i2c_dev->dev, "I2C_COUNT = 0x%x\n", readl(i2c_dev->base + I2C_COUNT));
	dev_err(i2c_dev->dev, "I2C_STATUS = 0x%x\n", readl(i2c_dev->base + I2C_STATUS));
	dev_err(i2c_dev->dev, "ADDR_DVD0 = 0x%x\n", readl(i2c_dev->base + ADDR_DVD0));
	dev_err(i2c_dev->dev, "ADDR_DVD1 = 0x%x\n", readl(i2c_dev->base + ADDR_DVD1));
	dev_err(i2c_dev->dev, "ADDR_STA0_DVD = 0x%x\n", readl(i2c_dev->base + ADDR_STA0_DVD));
}
/*
static void sprd_i2c_reset(struct sprd_i2c *i2c_dev)
{
	struct reset_control *rst_test;
	int ret;

	rst_test = devm_reset_control_get(i2c_dev->dev, "i2c_rst");
	if (!IS_ERR(rst_test)) {
		ret = reset_control_reset(rst_test);
		if (ret < 0)
			dev_err(i2c_dev->dev, "i2c soft reset failed, ret = %d\n", ret);
	} else {
		dev_err(i2c_dev->dev, "reset control get failed\n");
	}
}
*/
static void sprd_i2c_set_count(struct sprd_i2c *i2c_dev, u32 count)
{
	writel(count, i2c_dev->base + I2C_COUNT);
}

static void sprd_i2c_send_stop(struct sprd_i2c *i2c_dev, int stop)
{
	u32 tmp = readl(i2c_dev->base + I2C_CTL);

	if (stop)
		writel(tmp & ~STP_EN, i2c_dev->base + I2C_CTL);
	else
		writel(tmp | STP_EN, i2c_dev->base + I2C_CTL);
}

static void sprd_i2c_clear_start(struct sprd_i2c *i2c_dev)
{
	u32 tmp = readl(i2c_dev->base + I2C_CTL);

	writel(tmp & ~I2C_START, i2c_dev->base + I2C_CTL);
}
static int sprd_i2c_get_ack_busy(struct sprd_i2c *i2c_dev)
{
	bool ack = (readl(i2c_dev->base + I2C_STATUS) & I2C_RX_ACK);
	bool busy = !(readl(i2c_dev->base + I2C_STATUS) & SCL_IN);

	return busy && ack;
}

static void sprd_i2c_clear_ack(struct sprd_i2c *i2c_dev)
{
	u32 tmp = readl(i2c_dev->base + I2C_STATUS);

	writel(tmp & ~I2C_RX_ACK, i2c_dev->base + I2C_STATUS);
}

static void sprd_i2c_clear_irq(struct sprd_i2c *i2c_dev)
{
	u32 tmp = readl(i2c_dev->base + I2C_STATUS);

	writel((tmp & ~I2C_INT) | I2C_RX_ACK, i2c_dev->base + I2C_STATUS);
}

static void sprd_i2c_reset_fifo(struct sprd_i2c *i2c_dev)
{
	writel(I2C_RST, i2c_dev->base + ADDR_RST);
}

static void sprd_i2c_set_devaddr(struct sprd_i2c *i2c_dev, struct i2c_msg *m)
{
	writel(m->addr << 1, i2c_dev->base + I2C_ADDR_CFG);
}

static void sprd_i2c_write_bytes(struct sprd_i2c *i2c_dev, u8 *buf, u32 len)
{
	u32 i;

	for (i = 0; i < len; i++)
		writeb(buf[i], i2c_dev->base + I2C_TX);
}

static void sprd_i2c_read_bytes(struct sprd_i2c *i2c_dev, u8 *buf, u32 len)
{
	u32 i;

	for (i = 0; i < len; i++)
		buf[i] = readb(i2c_dev->base + I2C_RX);
}

static void sprd_i2c_set_full_thld(struct sprd_i2c *i2c_dev, u32 full_thld)
{
	u32 tmp = readl(i2c_dev->base + I2C_CTL);

	tmp &= ~FIFO_AF_LVL_MASK;
	tmp |= full_thld << FIFO_AF_LVL;
	writel(tmp, i2c_dev->base + I2C_CTL);
};

static void sprd_i2c_set_empty_thld(struct sprd_i2c *i2c_dev, u32 empty_thld)
{
	u32 tmp = readl(i2c_dev->base + I2C_CTL);

	tmp &= ~FIFO_AE_LVL_MASK;
	tmp |= empty_thld << FIFO_AE_LVL;
	writel(tmp, i2c_dev->base + I2C_CTL);
};

static void sprd_i2c_set_fifo_full_int(struct sprd_i2c *i2c_dev, int enable)
{
	u32 tmp = readl(i2c_dev->base + I2C_CTL);

	if (enable)
		tmp |= FULL_INTEN;
	else
		tmp &= ~FULL_INTEN;

	writel(tmp, i2c_dev->base + I2C_CTL);
};

static void sprd_i2c_set_fifo_empty_int(struct sprd_i2c *i2c_dev, int enable)
{
	u32 tmp = readl(i2c_dev->base + I2C_CTL);

	if (enable)
		tmp |= EMPTY_INTEN;
	else
		tmp &= ~EMPTY_INTEN;

	writel(tmp, i2c_dev->base + I2C_CTL);
};

static void sprd_i2c_enable_dma(struct sprd_i2c *i2c_dev, int bool)
{
	u32 tmp = readl(i2c_dev->base + I2C_CTL);
	if (bool) {
		tmp |= I2C_DMA_EN;
	} else {
		tmp &= ~I2C_DMA_EN;
	}
	writel(tmp, i2c_dev->base + I2C_CTL);
}
static void sprd_i2c_opt_start(struct sprd_i2c *i2c_dev)
{
	u32 tmp = readl(i2c_dev->base + I2C_CTL);

	writel(tmp | I2C_START, i2c_dev->base + I2C_CTL);
}

static void sprd_i2c_opt_mode(struct sprd_i2c *i2c_dev, int rw)
{
	u32 cmd = readl(i2c_dev->base + I2C_CTL) & ~I2C_MODE;

	writel(cmd | rw << 3, i2c_dev->base + I2C_CTL);
}

static void sprd_i2c_data_transfer(struct sprd_i2c *i2c_dev)
{
	u32 i2c_count = i2c_dev->count;
	u32 need_tran = i2c_count <= I2C_FIFO_DEEP ? i2c_count : I2C_FIFO_DEEP;
	struct i2c_msg *msg = i2c_dev->msg;

	if (msg->flags & I2C_M_RD) {
		sprd_i2c_read_bytes(i2c_dev, i2c_dev->buf, I2C_FIFO_FULL_THLD);
		i2c_dev->count -= I2C_FIFO_FULL_THLD;
		i2c_dev->buf += I2C_FIFO_FULL_THLD;

		/*
		 * If the read data count is larger than rx fifo full threshold,
		 * we should enable the rx fifo full interrupt to read data
		 * again.
		 */
		if (i2c_dev->count >= I2C_FIFO_FULL_THLD)
			sprd_i2c_set_fifo_full_int(i2c_dev, 1);
	} else {
		sprd_i2c_write_bytes(i2c_dev, i2c_dev->buf, need_tran);
		i2c_dev->buf += need_tran;
		i2c_dev->count -= need_tran;

		/*
		 * If the write data count is arger than tx fifo depth which
		 * means we can not write all data in one time, then we should
		 * enable the tx fifo empty interrupt to write again.
		 */
		if (i2c_count > I2C_FIFO_DEEP)
			sprd_i2c_set_fifo_empty_int(i2c_dev, 1);
	}
}

static void sprd_i2c_set_clk(struct sprd_i2c *i2c_dev, u32 freq);
static int sprd_i2c_handle_msg(struct i2c_adapter *i2c_adap,
			       struct i2c_msg *msg, bool is_last_msg)
{
	struct sprd_i2c *i2c_dev = i2c_adap->algo_data;
	unsigned long time_left;
	int ret;

	i2c_dev->msg = msg;
	i2c_dev->buf = msg->buf;
	i2c_dev->count = msg->len;
	reinit_completion(&i2c_dev->complete);
	sprd_i2c_reset_fifo(i2c_dev);
	sprd_i2c_set_devaddr(i2c_dev, msg);
	sprd_i2c_set_count(i2c_dev, msg->len);

	if (msg->flags & I2C_M_RD) {
		sprd_i2c_opt_mode(i2c_dev, 1);
		sprd_i2c_send_stop(i2c_dev, 1);
	} else {
		sprd_i2c_opt_mode(i2c_dev, 0);
		sprd_i2c_send_stop(i2c_dev, !!is_last_msg);
	}

	if (msg->flags & I2C_400K_FLAG) {
		sprd_i2c_set_clk(i2c_dev, 400000);
	} else if (msg->flags & I2C_1M_FLAG) {
		sprd_i2c_set_clk(i2c_dev, 1000000);
	}
	/*
	 * We should enable rx fifo full interrupt to get data when receiving
	 * full data.
	 */
	if (msg->flags & I2C_M_RD)
		sprd_i2c_set_fifo_full_int(i2c_dev, 1);
	else
		sprd_i2c_data_transfer(i2c_dev);

	sprd_i2c_opt_start(i2c_dev);

	time_left = wait_for_completion_timeout(&i2c_dev->complete,
				msecs_to_jiffies(I2C_XFER_TIMEOUT));
	if (!time_left) {
		//sprd_i2c_reset(i2c_dev);
		dev_err(i2c_dev->dev, "transfer timeout, I2C_STATUS = 0x%x\n",
           readl(i2c_dev->base + I2C_STATUS));
       if (i2c_dev->rst != NULL) {
            ret = reset_control_reset(i2c_dev->rst);
           if (ret < 0)
                dev_err(i2c_dev->dev, "i2c soft reset failed, ret = %d\n", ret);
        }
		return -ETIMEDOUT;
	}

	return i2c_dev->err;
}

static int sprd_i2c_dma_submit(struct sprd_i2c *i2c_dev,
			       struct dma_chan *dma_chan,
			       struct dma_slave_config *c,
			       enum dma_transfer_direction dir)
{
	struct dma_async_tx_descriptor *desc;

	unsigned long flags;
	int ret;

	ret = dmaengine_slave_config(dma_chan, c);
	if (ret < 0)
		return ret;

	flags = SPRD_DMA_FLAGS(SPRD_DMA_CHN_MODE_NONE, SPRD_DMA_NO_TRG,
			       SPRD_DMA_FRAG_REQ, SPRD_DMA_TRANS_INT);


	desc = dmaengine_prep_slave_single(dma_chan,
						   i2c_dev->dma.dma_phys_addr,
						   i2c_dev->dma_trans_len,
						   dir, flags);

	if (!desc)
		return  -ENODEV;

	i2c_dev->dma.cookie = dmaengine_submit(desc);
	if (dma_submit_error(i2c_dev->dma.cookie))
		return dma_submit_error(i2c_dev->dma.cookie);

	dma_async_issue_pending(dma_chan);


	return 0;
}
static int sprd_i2c_dma_rx_config(struct sprd_i2c *i2c_dev)
{
	struct dma_chan *dma_chan = i2c_dev->dma.dma_chan[SPRD_I2C_RX];
	struct dma_slave_config config = {
		.src_addr = i2c_dev->phy_base + I2C_RX,
		.src_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE,
		.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE,
		.src_maxburst = i2c_dev->dma.fragmens_len,
		.dst_maxburst = i2c_dev->dma.fragmens_len,
		.direction = DMA_DEV_TO_MEM,

	};
	int ret;

	ret = sprd_i2c_dma_submit(i2c_dev, dma_chan, &config,
					DMA_DEV_TO_MEM);

	if (ret)
		return ret;


	return i2c_dev->count;
}

static int sprd_i2c_dma_tx_config(struct sprd_i2c *i2c_dev)
{
	struct dma_chan *dma_chan = i2c_dev->dma.dma_chan[SPRD_I2C_TX];
	struct dma_slave_config config = {
		.dst_addr = i2c_dev->phy_base + I2C_TX,
		.src_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE,
		.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE,
		.src_maxburst = i2c_dev->dma.fragmens_len,

		.direction = DMA_MEM_TO_DEV,

	};
	int ret;
	ret = sprd_i2c_dma_submit(i2c_dev, dma_chan, &config,
					   DMA_MEM_TO_DEV);
	if (ret)
		return ret;

	return i2c_dev->count;
}

static int sprd_i2c_dma_handle_msg(struct i2c_adapter *i2c_adap,
			       struct i2c_msg *msg, bool is_last_msg)
{
	struct sprd_i2c *i2c_dev = i2c_adap->algo_data;
	int ret = 0;
	struct dma_chan *dma_chan;
	unsigned long time_left;

	u8 *dma_tmp_buf = NULL;
	dev_err(i2c_dev->dev, "lhz sprd_i2c_dma_handle_msg, msg->len = %d\n", msg->len);
	i2c_dev->msg = msg;
	i2c_dev->buf = msg->buf;
	i2c_dev->count = msg->len;
	i2c_dev->dma.fragmens_len = SPRD_I2C_DMA_STEP;
	i2c_dev->dma_trans_len = round_up(msg->len, i2c_dev->dma.fragmens_len);

	dma_tmp_buf = kzalloc(i2c_dev->dma_trans_len, GFP_ATOMIC);
	if (!dma_tmp_buf) {
		ret = -ENOMEM;
		dev_err(i2c_dev->dev, "failed to alloc dma_tmp_buf, ret = %d\n",
			ret);
		return ret;
	}


	reinit_completion(&i2c_dev->complete);
	sprd_i2c_reset_fifo(i2c_dev);
	sprd_i2c_set_devaddr(i2c_dev, msg);
	sprd_i2c_set_count(i2c_dev, msg->len);

	if (msg->flags & I2C_400K_FLAG) {
		sprd_i2c_set_clk(i2c_dev, 400000);
	} else if (msg->flags & I2C_1M_FLAG) {
		sprd_i2c_set_clk(i2c_dev, 1000000);
	}
	sprd_i2c_set_fifo_empty_int(i2c_dev, 0);
	sprd_i2c_set_fifo_full_int(i2c_dev, 0);

	if (msg->flags & I2C_M_RD) {
		memcpy(dma_tmp_buf, i2c_dev->buf, i2c_dev->count);

		i2c_dev->dma.dma_phys_addr = dma_map_single(i2c_dev->dev,
								(void *)dma_tmp_buf,
								i2c_dev->count,
								DMA_FROM_DEVICE);

		sprd_i2c_opt_mode(i2c_dev, 1);
		sprd_i2c_send_stop(i2c_dev, 1);
		ret = sprd_i2c_dma_rx_config(i2c_dev);
		if (ret < 0)
			dev_err(i2c_dev->dev, "lhz RX config err!!!");
	} else {
		memcpy(dma_tmp_buf, i2c_dev->buf, i2c_dev->count);

		i2c_dev->dma.dma_phys_addr = dma_map_single(i2c_dev->dev,
								(void *)dma_tmp_buf,
								i2c_dev->count,
								DMA_TO_DEVICE);

		sprd_i2c_opt_mode(i2c_dev, 0);
		sprd_i2c_send_stop(i2c_dev, !!is_last_msg);
		ret = sprd_i2c_dma_tx_config(i2c_dev);
		if (ret < 0)
			dev_err(i2c_dev->dev, "lhz TX config err!!!");
	}



	sprd_i2c_enable_dma(i2c_dev, true);

	if (msg->flags & I2C_M_RD)
		dma_chan = i2c_dev->dma.dma_chan[SPRD_I2C_RX];
	else
		dma_chan = i2c_dev->dma.dma_chan[SPRD_I2C_TX];


	sprd_i2c_opt_start(i2c_dev);

	time_left = wait_for_completion_timeout(&i2c_dev->complete,
					msecs_to_jiffies(I2C_XFER_TIMEOUT));
	sprd_i2c_dump_reg(i2c_dev);

	if ((msg->flags & I2C_M_RD) && !i2c_dev->err && dma_tmp_buf != NULL) {
		dev_err(i2c_dev->dev, "lhz (msg->flags & I2C_M_RD) && !i2c_dev->err && dma_tmp_buf != NULL)");
		i2c_dev->buf = dma_tmp_buf;
	}

	if (dma_tmp_buf != NULL) {
		dma_unmap_single(i2c_dev->dev,
						 i2c_dev->dma.dma_phys_addr,
						 i2c_dev->count,
						 DMA_TO_DEVICE);
		kfree(dma_tmp_buf);
	}


	if (!time_left) {
		dev_err(i2c_dev->dev, "lhz dma handle timeout!!!");
		sprd_i2c_enable_dma(i2c_dev, false);
		return -EBUSY;
	}


	sprd_i2c_enable_dma(i2c_dev, false);
	sprd_i2c_reset_fifo(i2c_dev);
	return ret;
}

static int sprd_i2c_dma_request(struct sprd_i2c *i2c_dev)
{
	i2c_dev->dma.dma_chan[SPRD_I2C_RX] = dma_request_chan(i2c_dev->dev, "rx");

	if (IS_ERR_OR_NULL(i2c_dev->dma.dma_chan[SPRD_I2C_RX])) {
		if (PTR_ERR(i2c_dev->dma.dma_chan[SPRD_I2C_RX]) == -EPROBE_DEFER)
			return PTR_ERR(i2c_dev->dma.dma_chan[SPRD_I2C_RX]);

		dev_err(i2c_dev->dev, "request RX DMA channel failed!\n");
		return PTR_ERR(i2c_dev->dma.dma_chan[SPRD_I2C_RX]);
	}

	i2c_dev->dma.dma_chan[SPRD_I2C_TX] = dma_request_chan(i2c_dev->dev, "tx");

	if (IS_ERR_OR_NULL(i2c_dev->dma.dma_chan[SPRD_I2C_TX])) {
		if (PTR_ERR(i2c_dev->dma.dma_chan[SPRD_I2C_TX]) == -EPROBE_DEFER)
			return PTR_ERR(i2c_dev->dma.dma_chan[SPRD_I2C_TX]);

		dev_err(i2c_dev->dev, "request TX DMA channel failed!\n");
		dma_release_channel(i2c_dev->dma.dma_chan[SPRD_I2C_RX]);
		return PTR_ERR(i2c_dev->dma.dma_chan[SPRD_I2C_TX]);
	}

	return 0;
}

static void sprd_i2c_dma_release(struct sprd_i2c *i2c_dev)
{
	if (i2c_dev->dma.dma_chan[SPRD_I2C_RX]->client_count) {
		dma_release_channel(i2c_dev->dma.dma_chan[SPRD_I2C_RX]);
	}
	if (i2c_dev->dma.dma_chan[SPRD_I2C_TX]->client_count) {
		dma_release_channel(i2c_dev->dma.dma_chan[SPRD_I2C_TX]);
	}
}
static int sprd_i2c_master_xfer(struct i2c_adapter *i2c_adap,
				struct i2c_msg *msgs, int num)
{
	struct sprd_i2c *i2c_dev = i2c_adap->algo_data;
	int im, ret;

	ret = pm_runtime_resume_and_get(i2c_dev->dev);
	if (ret < 0)
		return ret;

	if (i2c_dev->dma.dma_enable && (IS_ERR_OR_NULL(i2c_dev->dma.dma_chan[SPRD_I2C_RX]) ||
	IS_ERR_OR_NULL(i2c_dev->dma.dma_chan[SPRD_I2C_TX]))) {
		sprd_i2c_dma_request(i2c_dev);
		}

	for (im = 0; im < num; im++) {
		if (!i2c_dev->dma.dma_enable) {
			ret = sprd_i2c_handle_msg(i2c_adap, &msgs[im], im == num - 1);
			if (ret)
				goto err_msg;
		} else {
			ret = sprd_i2c_dma_handle_msg(i2c_adap, &msgs[im], im == num - 1);
			if (ret)
				goto err_msg;
		}
	}

	if (i2c_dev->dma.dma_enable)
		sprd_i2c_dma_release(i2c_dev);


err_msg:
	pm_runtime_mark_last_busy(i2c_dev->dev);
	pm_runtime_put_autosuspend(i2c_dev->dev);

	return ret < 0 ? ret : im;
}

static u32 sprd_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm sprd_i2c_algo = {
	.master_xfer = sprd_i2c_master_xfer,
	.functionality = sprd_i2c_func,
};

static void sprd_i2c_set_clk(struct sprd_i2c *i2c_dev, u32 freq)
{
	u32 apb_clk = i2c_dev->src_clk;
	/*
	 * From I2C databook, the prescale calculation formula:
	 * prescale = freq_i2c / (4 * freq_scl) - 1;
	 */
	u32 i2c_dvd = apb_clk / (4 * freq) - 1;
	/*
	 * From I2C databook, the high period of SCL clock is recommended as
	 * 40% (2/5), and the low period of SCL clock is recommended as 60%
	 * (3/5), then the formula should be:
	 * high = (prescale * 2 * 2) / 5
	 * low = (prescale * 2 * 3) / 5
	 */
	u32 high = ((i2c_dvd << 1) * 2) / 5;
	u32 low = ((i2c_dvd << 1) * 3) / 5;
	u32 div0 = I2C_ADDR_DVD0_CALC(high, low);
	u32 div1 = I2C_ADDR_DVD1_CALC(high, low);

	writel(div0, i2c_dev->base + ADDR_DVD0);
	writel(div1, i2c_dev->base + ADDR_DVD1);

	/* Start hold timing = hold time(us) * source clock */
	if (freq == 400000)
		writel((14 * apb_clk) / 10000000, i2c_dev->base + ADDR_STA0_DVD);
	else if (freq == 100000)
		writel((4 * apb_clk) / 1000000, i2c_dev->base + ADDR_STA0_DVD);
	else if (freq == 1000000)
		writel((8 * apb_clk) / 10000000, i2c_dev->base + ADDR_STA0_DVD);
}

static void sprd_i2c_enable(struct sprd_i2c *i2c_dev)
{
	u32 tmp = I2C_DVD_OPT;

	writel(tmp, i2c_dev->base + I2C_CTL);

	sprd_i2c_set_full_thld(i2c_dev, I2C_FIFO_FULL_THLD);
	sprd_i2c_set_empty_thld(i2c_dev, I2C_FIFO_EMPTY_THLD);

	sprd_i2c_set_clk(i2c_dev, i2c_dev->bus_freq);
	sprd_i2c_reset_fifo(i2c_dev);
	sprd_i2c_clear_irq(i2c_dev);

	tmp = readl(i2c_dev->base + I2C_CTL);
	writel(tmp | I2C_EN | I2C_INT_EN | I2C_NACK_EN | I2C_TRANS_EN, i2c_dev->base + I2C_CTL);
}

static irqreturn_t sprd_i2c_isr_thread(int irq, void *dev_id)
{
	struct sprd_i2c *i2c_dev = dev_id;
	struct i2c_msg *msg = i2c_dev->msg;
	u32 i2c_tran;

	if (msg->flags & I2C_M_RD)
		i2c_tran = i2c_dev->count >= I2C_FIFO_FULL_THLD;
	else
		i2c_tran = i2c_dev->count;

	/*
	 * If we got one ACK from slave when writing data, and we did not
	 * finish this transmission (i2c_tran is not zero), then we should
	 * continue to write data.
	 *
	 * For reading data, ack is always true, if i2c_tran is not 0 which
	 * means we still need to contine to read data from slave.
	 */
	if (i2c_tran && i2c_dev->ack_flag) {
		sprd_i2c_data_transfer(i2c_dev);
		return IRQ_HANDLED;
	}

	i2c_dev->err = 0;

	/*
	 * If we did not get one ACK from slave when writing data, we should
	 * return -EIO to notify users.
	 */
	if (!i2c_dev->ack_flag)
		i2c_dev->err = -EIO;
	else if (msg->flags & I2C_M_RD && i2c_dev->count)
		sprd_i2c_read_bytes(i2c_dev, i2c_dev->buf, i2c_dev->count);

	/* Transmission is done and clear ack and start operation */
	sprd_i2c_clear_ack(i2c_dev);
	sprd_i2c_clear_start(i2c_dev);
	complete(&i2c_dev->complete);

	return IRQ_HANDLED;
}

static irqreturn_t sprd_i2c_isr(int irq, void *dev_id)
{
	struct sprd_i2c *i2c_dev = dev_id;
	struct i2c_msg *msg = i2c_dev->msg;
	u32 i2c_tran;

	if (msg->flags & I2C_M_RD)
		i2c_tran = i2c_dev->count >= I2C_FIFO_FULL_THLD;
	else
		i2c_tran = i2c_dev->count;

	/*
	 * If we did not get one ACK from slave when writing data, then we
	 * should finish this transmission since we got some errors.
	 *
	 * When writing data, if i2c_tran == 0 which means we have writen
	 * done all data, then we can finish this transmission.
	 *
	 * When reading data, if conut < rx fifo full threshold, which
	 * means we can read all data in one time, then we can finish this
	 * transmission too.
	 */
	i2c_dev->ack_flag = !(readl(i2c_dev->base + I2C_STATUS) & I2C_RX_ACK);
	if (!i2c_tran || !i2c_dev->ack_flag) {
		sprd_i2c_clear_start(i2c_dev);
		sprd_i2c_clear_irq(i2c_dev);
	}

	sprd_i2c_set_fifo_empty_int(i2c_dev, 0);
	sprd_i2c_set_fifo_full_int(i2c_dev, 0);

	return IRQ_WAKE_THREAD;
}
static irqreturn_t sprd_i2c_dma_isr(int irq, void *dev_id)
{
	struct sprd_i2c *i2c_dev = dev_id;
	int ret;


	sprd_i2c_dump_reg(i2c_dev);
	sprd_i2c_clear_start(i2c_dev);
	sprd_i2c_clear_irq(i2c_dev);
	sprd_i2c_set_fifo_empty_int(i2c_dev, 0);
	sprd_i2c_set_fifo_full_int(i2c_dev, 0);

	i2c_dev->ack_flag = !(readl(i2c_dev->base + I2C_STATUS) & I2C_RX_ACK);
	i2c_dev->err = 0;

	if (!i2c_dev->ack_flag) {
		sprd_i2c_dump_reg(i2c_dev);
		ret = sprd_i2c_get_ack_busy(i2c_dev);
		i2c_dev->err = -EIO;
	}
	complete(&i2c_dev->complete);
	return IRQ_HANDLED;
}

static int sprd_i2c_clk_init(struct sprd_i2c *i2c_dev)
{
	struct clk *clk_i2c, *clk_parent;

	clk_i2c = devm_clk_get(i2c_dev->dev, "i2c");
	if (IS_ERR(clk_i2c)) {
		dev_warn(i2c_dev->dev, "i2c%d can't get the i2c clock\n",
			 i2c_dev->adap.nr);
		clk_i2c = NULL;
	}

	clk_parent = devm_clk_get(i2c_dev->dev, "source");
	if (IS_ERR(clk_parent)) {
		dev_warn(i2c_dev->dev, "i2c%d can't get the source clock\n",
			 i2c_dev->adap.nr);
		clk_parent = NULL;
	}

	if (!!clk_i2c && !!clk_parent && !clk_set_parent(clk_i2c, clk_parent))
		i2c_dev->src_clk = clk_get_rate(clk_i2c);
	else
		i2c_dev->src_clk = 26000000;

	dev_dbg(i2c_dev->dev, "i2c%d set source clock is %d\n",
		i2c_dev->adap.nr, i2c_dev->src_clk);

	i2c_dev->clk = devm_clk_get(i2c_dev->dev, "enable");
	if (IS_ERR(i2c_dev->clk)) {
		dev_err(i2c_dev->dev, "i2c%d can't get the enable clock\n",
			i2c_dev->adap.nr);
		return PTR_ERR(i2c_dev->clk);
	}

	return 0;
}

static int sprd_i2c_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sprd_i2c *i2c_dev;
	struct resource *res;
	u32 prop;
	int ret;

	pdev->id = of_alias_get_id(dev->of_node, "i2c");

	i2c_dev = devm_kzalloc(dev, sizeof(struct sprd_i2c), GFP_KERNEL);
	if (!i2c_dev)
		return -ENOMEM;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENOMEM;
	i2c_dev->base = devm_platform_ioremap_resource(pdev, 0);
	i2c_dev->phy_base = res->start;
	if (IS_ERR(i2c_dev->base))
		return PTR_ERR(i2c_dev->base);

	i2c_dev->irq = platform_get_irq(pdev, 0);
	if (i2c_dev->irq < 0) {
		dev_err(&pdev->dev, "failed to get irq resource\n");
		return i2c_dev->irq;
	}

	i2c_set_adapdata(&i2c_dev->adap, i2c_dev);
	init_completion(&i2c_dev->complete);
	snprintf(i2c_dev->adap.name, sizeof(i2c_dev->adap.name),
		 "%s", "sprd-i2c");

	i2c_dev->bus_freq = 100000;
	i2c_dev->adap.owner = THIS_MODULE;
	i2c_dev->dev = dev;
	i2c_dev->adap.retries = 3;
	i2c_dev->adap.algo = &sprd_i2c_algo;
	i2c_dev->adap.algo_data = i2c_dev;
	i2c_dev->adap.dev.parent = dev;
	i2c_dev->adap.nr = pdev->id;
	i2c_dev->adap.dev.of_node = dev->of_node;

	if (!of_property_read_u32(dev->of_node, "clock-frequency", &prop))
		i2c_dev->bus_freq = prop;

	/* We only support 100k and 400k now, otherwise will return error. */
	if (i2c_dev->bus_freq != 100000 && i2c_dev->bus_freq != 400000 &&
	    i2c_dev->bus_freq != 1000000)
		return -EINVAL;

	ret = sprd_i2c_clk_init(i2c_dev);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, i2c_dev);

	i2c_dev->rst = devm_reset_control_get(i2c_dev->dev, "i2c_rst");
	 if (IS_ERR(i2c_dev->rst)) {
		 dev_err(i2c_dev->dev, "can't get i2c reset node\n");
		 i2c_dev->rst = NULL;
	}

	ret = sprd_i2c_dma_request(i2c_dev);

	if (ret) {
		dev_err(i2c_dev->dev, "failed to request dma, enter no dma mode, ret = %d\n", ret);
		i2c_dev->dma.dma_enable = false;
	} else {
		dev_err(i2c_dev->dev, "lhz dma_RX_chnID = %d\n", i2c_dev->dma.dma_chan[SPRD_I2C_RX]->chan_id);
		dev_err(i2c_dev->dev, "lhz dma_TX_chnID = %d\n", i2c_dev->dma.dma_chan[SPRD_I2C_TX]->chan_id);
		i2c_dev->dma.dma_enable = true;
	}
	ret = clk_prepare_enable(i2c_dev->clk);
	if (ret)
		return ret;

	sprd_i2c_enable(i2c_dev);

	pm_runtime_set_autosuspend_delay(i2c_dev->dev, SPRD_I2C_PM_TIMEOUT);
	pm_runtime_use_autosuspend(i2c_dev->dev);
	pm_runtime_set_active(i2c_dev->dev);
	pm_runtime_enable(i2c_dev->dev);

	ret = pm_runtime_get_sync(i2c_dev->dev);
	if (ret < 0)
		goto err_rpm_put;
	if (i2c_dev->dma.dma_enable) {
		ret = devm_request_irq(dev, i2c_dev->irq, sprd_i2c_dma_isr,
			0, pdev->name, i2c_dev);
		if (ret) {
			dev_err(&pdev->dev, "failed to request dma irq %d\n", i2c_dev->irq);
			goto err_rpm_put;
		}
	} else {
		ret = devm_request_threaded_irq(dev, i2c_dev->irq,
			sprd_i2c_isr, sprd_i2c_isr_thread,
			IRQF_NO_SUSPEND | IRQF_ONESHOT,
			pdev->name, i2c_dev);
		if (ret) {
			dev_err(&pdev->dev, "failed to request irq %d\n", i2c_dev->irq);
			goto err_rpm_put;
		}
	}

	ret = i2c_add_numbered_adapter(&i2c_dev->adap);
	if (ret) {
		dev_err(&pdev->dev, "add adapter failed\n");
		goto err_rpm_put;
	}

	pm_runtime_mark_last_busy(i2c_dev->dev);
	pm_runtime_put_autosuspend(i2c_dev->dev);
	return 0;

err_rpm_put:
	pm_runtime_put_noidle(i2c_dev->dev);
	pm_runtime_disable(i2c_dev->dev);
	clk_disable_unprepare(i2c_dev->clk);
	return ret;
}

static int sprd_i2c_remove(struct platform_device *pdev)
{
	struct sprd_i2c *i2c_dev = platform_get_drvdata(pdev);
	int ret;

	ret = pm_runtime_resume_and_get(i2c_dev->dev);
	if (ret < 0)
		return ret;

	i2c_del_adapter(&i2c_dev->adap);
	clk_disable_unprepare(i2c_dev->clk);

	pm_runtime_put_noidle(i2c_dev->dev);
	pm_runtime_disable(i2c_dev->dev);

	return 0;
}

static int __maybe_unused sprd_i2c_suspend_noirq(struct device *dev)
{
	struct sprd_i2c *i2c_dev = dev_get_drvdata(dev);

	i2c_mark_adapter_suspended(&i2c_dev->adap);
	return pm_runtime_force_suspend(dev);
}

static int __maybe_unused sprd_i2c_resume_noirq(struct device *dev)
{
	struct sprd_i2c *i2c_dev = dev_get_drvdata(dev);

	i2c_mark_adapter_resumed(&i2c_dev->adap);
	return pm_runtime_force_resume(dev);
}

static int __maybe_unused sprd_i2c_runtime_suspend(struct device *dev)
{
	struct sprd_i2c *i2c_dev = dev_get_drvdata(dev);

	clk_disable_unprepare(i2c_dev->clk);

	return 0;
}

static int __maybe_unused sprd_i2c_runtime_resume(struct device *dev)
{
	struct sprd_i2c *i2c_dev = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(i2c_dev->clk);
	if (ret)
		return ret;

	sprd_i2c_enable(i2c_dev);

	return 0;
}

static const struct dev_pm_ops sprd_i2c_pm_ops = {
	SET_RUNTIME_PM_OPS(sprd_i2c_runtime_suspend,
			   sprd_i2c_runtime_resume, NULL)

	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(sprd_i2c_suspend_noirq,
				      sprd_i2c_resume_noirq)
};

static const struct of_device_id sprd_i2c_of_match[] = {
	{ .compatible = "sprd,sc9860-i2c", },
	{},
};

static struct platform_driver sprd_i2c_driver = {
	.probe = sprd_i2c_probe,
	.remove = sprd_i2c_remove,
	.driver = {
		   .name = "sprd-i2c",
		   .of_match_table = sprd_i2c_of_match,
		   .pm = &sprd_i2c_pm_ops,
	},
};

module_platform_driver(sprd_i2c_driver);

MODULE_DESCRIPTION("Spreadtrum I2C master controller driver");
MODULE_LICENSE("GPL v2");
