/************************************************************************
*
* File Name: touch.c
*
* Author: likaoshan
*
* Created: 2021-02-19
*
* Abstract: for tp Compatibility
*
************************************************************************/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/device.h>

#include "touch.h"

struct touch_panel tp_interface;
EXPORT_SYMBOL(tp_interface);

void tp_charge_status_switch(int status)
{
    if (tp_interface.charger_mode_switch_status) {
        printk("dfy tp charge status switch status = %d \n",status);
        tp_interface.charger_mode_switch_status(status);
    } else {
        printk("dfy tp charge status switch not func\n");
    }
}
EXPORT_SYMBOL(tp_charge_status_switch);

void tp_headset_status_switch(int status)
{
    if (tp_interface.headset_switch_status) {
        printk("dfy tp headset status switch status =%d \n",status);
        tp_interface.headset_switch_status(status);
    } else {
        printk(" dfy tp headset status switch not func\n");
    }
}
EXPORT_SYMBOL(tp_headset_status_switch);

// #define FW_NAME_LEN                    128
// /*add tp interface */
// static ssize_t tp_fw_upgrade_show(
//     struct device *dev, struct device_attribute *attr, char *buf)
// {
//     return -EPERM;
// }

// static ssize_t tp_fw_upgrade_store(
//     struct device *dev,
//     struct device_attribute *attr, const char *buf, size_t count)
// {
//    char fwname[FW_NAME_LEN];
//    int cnt = count;
//    memset(fwname, 0, sizeof(fwname));

//    snprintf(fwname, count, "%s", buf);

//    TP_INFO("fw_name = %s ",fwname);

//    if(tp_interface.tp_inferface_fw_upgrade)
//       tp_interface.tp_inferface_fw_upgrade(fwname,cnt);
//    else
//    	  TP_INFO("tp_inferface_fw_upgrade not func\n");

//    return -EPERM;

// }

// static ssize_t tp_edge_mode_show(
//     struct device *dev, struct device_attribute *attr, char *buf)
// {
//     return -EPERM;
// }

// static ssize_t tp_edge_mode_store(
//     struct device *dev,
//     struct device_attribute *attr, const char *buf, size_t count)
// {
//    char buf_edge_mode[4];
//    int cnt = count;
//    memset(buf_edge_mode, 0, sizeof(buf_edge_mode));

//    snprintf(buf_edge_mode, 4, "%s", buf);

//    TP_INFO("buf = %s ",buf);

//    TP_INFO("buf_edge_mode = %s ",buf_edge_mode);

//    if(tp_interface.tp_inferface_edge_mode)
//       tp_interface.tp_inferface_edge_mode(buf_edge_mode,cnt);
//    else
//    	  TP_INFO("tp_inferface_fw_upgrade not func\n");

//    return -EPERM;

// }

// static DEVICE_ATTR(tp_fw_upgrade, S_IRUGO | S_IWUSR, tp_fw_upgrade_show, tp_fw_upgrade_store);
// static DEVICE_ATTR(tp_edge_mode, S_IRUGO | S_IWUSR, tp_edge_mode_show,tp_edge_mode_store);

// static struct attribute *tp_attributes[] = {
//     &dev_attr_tp_fw_upgrade.attr,
// 	&dev_attr_tp_edge_mode.attr,
//     NULL
// };

// static struct attribute_group tp_attribute_group = {
//     .attrs = tp_attributes
// };

// int tp_create_sysfs( struct device *dev)
// {
//     int ret = 0;

//     ret = sysfs_create_group(&dev->kobj, &tp_attribute_group);
//     if (ret) {
//         TP_ERROR("[EX]: sysfs_create_group() failed!!");
//         sysfs_remove_group(&dev->kobj, &tp_attribute_group);
//         return -ENOMEM;
//     } else {
//         TP_INFO("[EX]: sysfs_create_group() succeeded!!");
//     }

//     return ret;
// }

// int tp_remove_sysfs( struct device *dev)
// {
//     sysfs_remove_group(&dev->kobj, &tp_attribute_group);
//     return 0;
// }
