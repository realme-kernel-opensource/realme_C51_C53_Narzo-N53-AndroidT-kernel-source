#
# sprd_camera.ko
#
# Makefile: for external make invocation
#
# Note:
# - Please refer to modules/sample/Makefile to find out what should be
#   done in this Makefile
#

KO_MODULE_NAME := sprd_camera
KO_MODULE_OUT := $(BSP_MODULES_OUT)/$(KO_MODULE_NAME)
KO_MODULE_KBUILD := $(CURDIR)/Kbuild

ifneq ($(findstring $(strip $(BSP_KERNEL_VERSION)), kernel5.4 kernel5.15),)
KBUILD_EXTRA_SYMBOLS += $(BSP_MODULES_OUT)/sprd_flash_drv/Module.symvers
KBUILD_EXTRA_SYMBOLS += $(BSP_MODULES_OUT)/sprd_sensor/Module.symvers
export KBUILD_EXTRA_SYMBOLS
endif

ifeq ($(strip $(BSP_KERNEL_VERSION)), kernel5.15)
KBUILD_EXTRA_SYMBOLS += $(BSP_MODULES_OUT)/sprd-dmabuf/Module.symvers
export KBUILD_EXTRA_SYMBOLS
endif

ifeq ($(strip $(BSP_KERNEL_VERSION)), kernel5.4)
KBUILD_EXTRA_SYMBOLS += $(BSP_MODULES_OUT)/sprd-ion/Module.symvers
export KBUILD_EXTRA_SYMBOLS
endif

KBUILD_EXTRA_SYMBOLS += $(BSP_MODULES_OUT)/sprd_camsys_pw_domain/Module.symvers
export KBUILD_EXTRA_SYMBOLS

.PHONY: modules modules_install clean

modules:
	@mkdir -p $(KO_MODULE_OUT) && ln -snf $(KO_MODULE_KBUILD) $(KO_MODULE_OUT)/Kbuild
	@ln -snf $(CURDIR) $(KO_MODULE_OUT)/source
	$(MAKE) -C $(BSP_KERNEL_PATH) M=$(KO_MODULE_OUT) src=$(CURDIR) $@

modules_install:
	$(MAKE) -C $(BSP_KERNEL_PATH) M=$(KO_MODULE_OUT) $@

# Remove the out directory wholly
clean:
	@#$(MAKE) -C $(BSP_KERNEL_PATH) M=$(KO_MODULE_OUT) src=$(CURDIR) $@
	rm -rf $(KO_MODULE_OUT)
