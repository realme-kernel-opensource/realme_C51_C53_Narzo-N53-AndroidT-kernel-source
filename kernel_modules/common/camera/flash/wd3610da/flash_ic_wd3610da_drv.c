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

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>

#include "sprd_img.h"
#include "flash_drv.h"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "FLASH_WD3610da: %d %d %s : " fmt, current->pid, __LINE__, __func__

#define FLASH_DRIVER_NAME "flash-wd3610da"
#define FLASH_GPIO_MAX 2

enum {
	GPIO_FLASH_TORCH_EN,
	GPIO_FLASH_EN,
};

struct flash_driver_data {
	int gpio_tab[FLASH_GPIO_MAX];
	u32 torch_led_index;
};

static const char *const flash_gpio_names[FLASH_GPIO_MAX] = {
	"flash-torch-en-gpios", /* for torch/flash mode */
	"flash-en-gpios", /* for enable ic pin */
};


static int sprd_flash_wd3610da_init(void *drvd)
{
	int ret = 0;
	struct flash_driver_data *drv_data = (struct flash_driver_data *)drvd;

	if (!drv_data)
		return -EFAULT;

	return ret;
}

static int sprd_flash_wd3610da_deinit(void *drvd)
{
	int ret = 0;
	struct flash_driver_data *drv_data = (struct flash_driver_data *)drvd;

	if (!drv_data)
		return -EFAULT;

	return ret;
}

static int sprd_flash_wd3610da_open_torch(void *drvd, uint8_t idx)
{
	int ret = 0;
	int gpio_id = 0;
	struct flash_driver_data *drv_data = (struct flash_driver_data *)drvd;

	if (!drv_data)
		return -EFAULT;

	pr_info("torch_led_index:%d, idx:%d\n", drv_data->torch_led_index, idx);
	idx = drv_data->torch_led_index;
	if (SPRD_FLASH_LED0 & idx) {
		gpio_id = drv_data->gpio_tab[GPIO_FLASH_EN];
		if (gpio_is_valid(gpio_id)) {
			ret = gpio_direction_output(gpio_id, SPRD_FLASH_ON);
			if (ret)
				goto exit;
		}
	}
exit:
	return ret;
}

static int sprd_flash_wd3610da_close_torch(void *drvd, uint8_t idx)
{
	int ret = 0;
	int gpio_id = 0;
	struct flash_driver_data *drv_data = (struct flash_driver_data *)drvd;

	if (!drv_data)
		return -EFAULT;
	
	pr_info("torch_led_index:%d, idx:%d\n", drv_data->torch_led_index, idx);
	idx = drv_data->torch_led_index;
	if (SPRD_FLASH_LED0 & idx) {
		gpio_id = drv_data->gpio_tab[GPIO_FLASH_EN];
		if (gpio_is_valid(gpio_id)) {
			ret = gpio_direction_output(gpio_id, SPRD_FLASH_OFF);
			if (ret)
				goto exit;
		}
	}
exit:
	return ret;
}

static int sprd_flash_wd3610da_open_preflash(void *drvd, uint8_t idx)
{
	int ret = 0;
	int gpio_id = 0;
	struct flash_driver_data *drv_data = (struct flash_driver_data *)drvd;

	if (!drv_data)
		return -EFAULT;
	pr_info("torch_led_index:%d, idx:%d\n", drv_data->torch_led_index, idx);
	if (SPRD_FLASH_LED0 & idx) {
		gpio_id = drv_data->gpio_tab[GPIO_FLASH_EN];
		if (gpio_is_valid(gpio_id)) {
			ret = gpio_direction_output(gpio_id, SPRD_FLASH_ON);
			if (ret)
				goto exit;
		}
	}
exit:
	return ret;
}

static int sprd_flash_wd3610da_close_preflash(void *drvd, uint8_t idx)
{
	int ret = 0;
	int gpio_id = 0;
	struct flash_driver_data *drv_data = (struct flash_driver_data *)drvd;

	if (!drv_data)
		return -EFAULT;

	pr_info("torch_led_index:%d, idx:%d\n", drv_data->torch_led_index, idx);
	if (SPRD_FLASH_LED0 & idx) {
		gpio_id = drv_data->gpio_tab[GPIO_FLASH_EN];
		if (gpio_is_valid(gpio_id)) {
			ret = gpio_direction_output(gpio_id, SPRD_FLASH_OFF);
			if (ret)
				goto exit;
		}
	}
exit:
	return ret;
}

static int sprd_flash_wd3610da_open_highlight(void *drvd, uint8_t idx)
{
	int ret = 0;
	int gpio_id = 0;
	struct flash_driver_data *drv_data = (struct flash_driver_data *)drvd;

	if (!drv_data)
		return -EFAULT;

	pr_info("torch_led_index:%d, idx:%d\n", drv_data->torch_led_index, idx);
	if (SPRD_FLASH_LED0 & idx) {
		gpio_id = drv_data->gpio_tab[GPIO_FLASH_EN];
		if (gpio_is_valid(gpio_id)) {
			ret = gpio_direction_output(gpio_id, SPRD_FLASH_ON);
			if (ret)
				goto exit;
		}
	}
exit:
	return ret;
}

static int sprd_flash_wd3610da_close_highlight(void *drvd, uint8_t idx)
{
	int ret = 0;
	int gpio_id = 0;
	struct flash_driver_data *drv_data = (struct flash_driver_data *)drvd;

	if (!drv_data)
		return -EFAULT;

	pr_info("torch_led_index:%d, idx:%d\n", drv_data->torch_led_index, idx);
	if (SPRD_FLASH_LED0 & idx) {
		gpio_id = drv_data->gpio_tab[GPIO_FLASH_EN];
		if (gpio_is_valid(gpio_id)) {
			ret = gpio_direction_output(gpio_id, SPRD_FLASH_OFF);
			if (ret)
				goto exit;
		}
	}
exit:
	return ret;
}

static int sprd_flash_wd3610da_cfg_value_torch(void *drvd, uint8_t idx,
				struct sprd_flash_element *element) {
	struct flash_driver_data *drv_data = (struct flash_driver_data *)drvd;
	int gpio_id = 0;
	int ret = 0;

	if (!drv_data)
		return -EFAULT;

	pr_info("torch_led_index:%d, idx:%d\n", drv_data->torch_led_index, idx);
	idx = drv_data->torch_led_index;
	if (SPRD_FLASH_LED0 & idx) {
		gpio_id = drv_data->gpio_tab[GPIO_FLASH_TORCH_EN];
		if (gpio_is_valid(gpio_id)) {
			ret = gpio_direction_output(gpio_id, SPRD_FLASH_OFF);
			if (ret)
				goto exit;
		}
	}
exit:
	return ret;
}

static int sprd_flash_wd3610da_cfg_value_preflash(void *drvd, uint8_t idx,
				struct sprd_flash_element *element) {
	struct flash_driver_data *drv_data = (struct flash_driver_data *)drvd;
	int ret = 0;
	int gpio_id = 0;

	if (!drv_data)
		return -EFAULT;

	pr_info("torch_led_index:%d, idx:%d\n", drv_data->torch_led_index, idx);
	if (SPRD_FLASH_LED0 & idx) {
		gpio_id = drv_data->gpio_tab[GPIO_FLASH_TORCH_EN];
		if (gpio_is_valid(gpio_id)) {
			ret = gpio_direction_output(gpio_id, SPRD_FLASH_OFF);
			if (ret)
				goto exit;
		}
	}
exit:
	return ret;
}

static int sprd_flash_wd3610da_cfg_value_highlight(void *drvd, uint8_t idx,
				struct sprd_flash_element *element) {
	struct flash_driver_data *drv_data = (struct flash_driver_data *)drvd;
	int ret = 0;
	int gpio_id = 0;

	if (!drv_data)
		return -EFAULT;

	pr_info("torch_led_index:%d, idx:%d\n", drv_data->torch_led_index, idx);
	if (SPRD_FLASH_LED0 & idx) {
		gpio_id = drv_data->gpio_tab[GPIO_FLASH_TORCH_EN];
		if (gpio_is_valid(gpio_id)) {
			ret = gpio_direction_output(gpio_id, SPRD_FLASH_ON);
			if (ret)
				goto exit;
		}
	}
exit:
	return ret;
}

static const struct of_device_id wd3610da_flash_of_match_table[] = {
	{.compatible = "sprd,flash-wd3610da"},
	{/* MUST end with empty struct */},
};

static const struct sprd_flash_driver_ops flash_gpio_ops = {
	.open_torch = sprd_flash_wd3610da_open_torch,
	.close_torch = sprd_flash_wd3610da_close_torch,
	.open_preflash = sprd_flash_wd3610da_open_preflash,
	.close_preflash = sprd_flash_wd3610da_close_preflash,
	.open_highlight = sprd_flash_wd3610da_open_highlight,
	.close_highlight = sprd_flash_wd3610da_close_highlight,
	.cfg_value_torch = sprd_flash_wd3610da_cfg_value_torch,
	.cfg_value_preflash = sprd_flash_wd3610da_cfg_value_preflash,
	.cfg_value_highlight = sprd_flash_wd3610da_cfg_value_highlight,
};

static int sprd_flash_wd3610da_probe(struct platform_device *pdev)
{
	int ret = 0;
	u32 gpio_node = 0;
	struct flash_driver_data *drv_data = NULL;
	int gpio[FLASH_GPIO_MAX];
	int j;

	if (IS_ERR_OR_NULL(pdev))
		return -EINVAL;

	if (!pdev->dev.of_node) {
		pr_err("no device node %s", __func__);
		return -ENODEV;
	}
	pr_info("flash-wd3610da probe\n");

	ret = of_property_read_u32(pdev->dev.of_node,
						"flash-ic", &gpio_node);
	if (ret) {
		pr_err("no gpio flash\n");
		return -ENODEV;
	}

	drv_data = devm_kzalloc(&pdev->dev, sizeof(*drv_data), GFP_KERNEL);
	if (!drv_data)
		return -ENOMEM;

	pdev->dev.platform_data = (void *)drv_data;

	for (j = 0; j < FLASH_GPIO_MAX; j++) {
		gpio[j] = of_get_named_gpio(pdev->dev.of_node,
						flash_gpio_names[j], 0);
		if (gpio_is_valid(gpio[j])) {
			ret = devm_gpio_request(&pdev->dev,
						gpio[j],
						flash_gpio_names[j]);

			if (ret) {
				pr_err("flash gpio err\n");
				goto exit;
			}

			ret = gpio_direction_output(gpio[j], SPRD_FLASH_OFF);

			if (ret) {
				pr_err("flash gpio output err\n");
				goto exit;
			}
		}
	}

	memcpy((void *)drv_data->gpio_tab, (void *)gpio, sizeof(gpio));

	ret = of_property_read_u32(pdev->dev.of_node,
				"torch-led-idx", &drv_data->torch_led_index);
	if (ret)
		drv_data->torch_led_index = SPRD_FLASH_LED0;

	ret = sprd_flash_wd3610da_init(drv_data);
	if (ret)
		goto exit;
	ret = sprd_flash_register(&flash_gpio_ops, drv_data, SPRD_FLASH_REAR);

exit:
	return ret;
}

static int sprd_flash_wd3610da_remove(struct platform_device *pdev)
{
	int ret = 0;

	ret = sprd_flash_wd3610da_deinit(pdev->dev.platform_data);

	return ret;
}

static struct platform_driver sprd_flash_wd3610da_driver = {
	.probe = sprd_flash_wd3610da_probe,
	.remove = sprd_flash_wd3610da_remove,
	.driver = {
		.name = FLASH_DRIVER_NAME,
		.of_match_table = of_match_ptr(wd3610da_flash_of_match_table),
	},
};

module_platform_driver(sprd_flash_wd3610da_driver);
MODULE_DESCRIPTION("Sprd wd3610da Flash Driver");
MODULE_LICENSE("GPL");
