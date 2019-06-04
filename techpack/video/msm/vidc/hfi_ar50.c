// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 */

#include "hfi_common.h"
#include "hfi_io_common.h"

#define WRAPPER_CLOCK_CONFIG_AR50	(WRAPPER_BASE_OFFS + 0x04)

void __interrupt_init_ar50(struct venus_hfi_device *device, u32 sid)
{
	__write_register(device, WRAPPER_INTR_MASK,
			WRAPPER_INTR_MASK_A2HVCODEC_BMSK, sid);
}


void clock_config_on_enable_ar50(struct venus_hfi_device *device, u32 sid)
{
	__write_register(device, WRAPPER_CLOCK_CONFIG_AR50, 0, sid);
	__write_register(device, WRAPPER_CPU_CLOCK_CONFIG, 0, sid);
}
