subdir-ccflags-y += -I$(src)

obj-$(CONFIG_SPRD_MODEM_DEBUG) += sprd_mipi.o
sprd_mipi-objs := core.o \
				sysfs.o \
				serdes.o

#obj-$(CONFIG_SPRD_MIPI_LOG_R2P0) += sharkle/
obj-$(CONFIG_SPRD_MIPI_LOG_R4P2) += sharkl3/
obj-$(CONFIG_SPRD_MIPI_LOG_R5P0) += sharkl5/
obj-$(CONFIG_SPRD_MIPI_LOG_R5P1) += qogirl6/
obj-$(CONFIG_SPRD_MIPI_LOG_QOGIRN6PRO) += qogirn6pro/
