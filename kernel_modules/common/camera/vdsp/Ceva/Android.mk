# Copyright (C) 2021-2022 UNISOC Communications Inc.
#
# This software is licensed under the terms of the GNU General Public
# License version 2, as published by the Free Software Foundation, and
# may be copied, distributed, and modified under those terms.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

ifeq ($(strip $(TARGET_BOARD_VDSP_MODULAR_KERNEL)),ceva1.0)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := vdsp.ko
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR)/lib/modules
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := $(LOCAL_MODULE)
LOCAL_STRIP_MODULE := keep_symbols

# delete .ko before making anything
LOCAL_PATH_VDSP := $(shell pwd)/$(LOCAL_PATH)
$(shell rm $(LOCAL_PATH_VDSP)/$(LOCAL_MODULE) -f)
$(shell rm $(shell find $(LOCAL_PATH_VDSP) -name "*.o") -f)

include $(BUILD_PREBUILT)

#convert to absolute directory
PRODUCT_OUT_ABSOLUTE:=$(shell cd $(PRODUCT_OUT); pwd)


$(LOCAL_PATH)/vdsp.ko: $(TARGET_PREBUILT_KERNEL)
	$(MAKE) -C $(shell dirname $@) VDSP_VER=$(TARGET_BOARD_VDSP_MODULAR_KERNEL) REL_PATH=$(LOCAL_PATH_VDSP) ARCH=$(TARGET_KERNEL_ARCH) CROSS_COMPILE=$(KERNEL_CROSS_COMPILE) KDIR=$(PRODUCT_OUT_ABSOLUTE)/obj/KERNEL clean
	$(MAKE) -C $(shell dirname $@) VDSP_VER=$(TARGET_BOARD_VDSP_MODULAR_KERNEL) REL_PATH=$(LOCAL_PATH_VDSP) ARCH=$(TARGET_KERNEL_ARCH) CROSS_COMPILE=$(KERNEL_CROSS_COMPILE) KDIR=$(PRODUCT_OUT_ABSOLUTE)/obj/KERNEL
	$(TARGET_STRIP) -d --strip-unneeded $@

endif
