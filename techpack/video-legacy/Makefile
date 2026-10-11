# SPDX-License-Identifier: GPL-2.0-only

# auto-detect subdirs

ifeq ($(CONFIG_ARCH_SDM845), y)
include $(srctree)/techpack/video-legacy/config/sdm845vid.conf
endif

ifeq ($(CONFIG_ARCH_SDM845), y)
LINUXINCLUDE    += -include $(srctree)/techpack/video-legacy/config/sdm845vidconf.h
endif

obj-y +=msm/
