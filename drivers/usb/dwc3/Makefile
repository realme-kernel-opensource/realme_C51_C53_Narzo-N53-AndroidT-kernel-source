# SPDX-License-Identifier: GPL-2.0
# define_trace.h needs to know how to find our header
CFLAGS_trace.o				:= -I$(src)

obj-$(CONFIG_USB_DWC3_SPRD_CORE)	+= dwc3-sprd-core.o
dwc3-sprd-core-y			:= core.o
ifneq ($(CONFIG_TRACING),)
	dwc3-sprd-core-y		+= trace.o
endif

ifneq ($(filter y,$(CONFIG_USB_DWC3_HOST) $(CONFIG_USB_DWC3_DUAL_ROLE)),)
	dwc3-sprd-core-y		+= host.o
endif

ifneq ($(filter y,$(CONFIG_USB_DWC3_GADGET) $(CONFIG_USB_DWC3_DUAL_ROLE)),)
	dwc3-sprd-core-y		+= gadget.o ep0.o
endif

ifneq ($(CONFIG_USB_DWC3_DUAL_ROLE),)
	dwc3-sprd-core-y		+= drd.o
endif

ifneq ($(CONFIG_USB_DWC3_ULPI),)
	dwc3-sprd-core-y		+= ulpi.o
endif

ifneq ($(CONFIG_DEBUG_FS),)
	dwc3-sprd-core-y		+= debugfs.o
endif
## Add prefix to ignore conflict with build-in dwc3
OBJCOPYFLAGS_dwc3-sprd-core.o := -prefix-symbols=__sprd_

##
# Platform-specific glue layers go here
#
# NOTICE: Make sure your glue layer doesn't depend on anything
# which is arch-specific and that it compiles on all situations.
#
# We want to keep this requirement in order to be able to compile
# the entire driver (with all its glue layers) on several architectures
# and make sure it compiles fine. This will also help with allmodconfig
# and allyesconfig builds.
##

obj-$(CONFIG_USB_DWC3_OMAP)		+= dwc3-omap.o
obj-$(CONFIG_USB_DWC3_EXYNOS)		+= dwc3-exynos.o
obj-$(CONFIG_USB_DWC3_PCI)		+= dwc3-pci.o
obj-$(CONFIG_USB_DWC3_HAPS)		+= dwc3-haps.o
obj-$(CONFIG_USB_DWC3_KEYSTONE)		+= dwc3-keystone.o
obj-$(CONFIG_USB_DWC3_MESON_G12A)	+= dwc3-meson-g12a.o
obj-$(CONFIG_USB_DWC3_OF_SIMPLE)	+= dwc3-of-simple.o
obj-$(CONFIG_USB_DWC3_ST)		+= dwc3-st.o
obj-$(CONFIG_USB_DWC3_QCOM)		+= dwc3-qcom.o
obj-$(CONFIG_USB_DWC3_SPRD)		+= dwc3-sprd.o
