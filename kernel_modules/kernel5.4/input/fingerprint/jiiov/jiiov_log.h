#ifndef __JIIOV_LOG_H__
#define __JIIOV_LOG_H__

#define ANC_LOGD(format, ...) \
    printk(KERN_ERR "[D][ANC_DRIVER][%s] " format "\n", __func__, ##__VA_ARGS__)
#define ANC_LOGI(format, ...) \
    printk(KERN_ERR "[I][ANC_DRIVER][%s] " format "\n", __func__, ##__VA_ARGS__)
#define ANC_LOGW(format, ...) \
    printk(KERN_ERR "[W][ANC_DRIVER][%s] " format "\n", __func__, ##__VA_ARGS__)
#define ANC_LOGE(format, ...) \
    printk(KERN_ERR "[E][ANC_DRIVER][%s] " format "\n", __func__, ##__VA_ARGS__)

#endif