/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
 */

#ifndef _CAM_COMMON_UTIL_H_
#define _CAM_COMMON_UTIL_H_

#define CAM_BITS_MASK_SHIFT(x, mask, shift) (((x) & (mask)) >> shift)

#define CAM_GET_TIMESTAMP(timestamp) ktime_get_real_ts64(&(timestamp))
#define CAM_GET_TIMESTAMP_DIFF_IN_MICRO(ts_start, ts_end, diff_microsec)       \
({                                                                             \
	diff_microsec = 0;                                                     \
	if (ts_end.tv_nsec >= ts_start.tv_nsec) {                              \
		diff_microsec =                                                \
			(ts_end.tv_nsec - ts_start.tv_nsec) / 1000;            \
		diff_microsec +=                                               \
			(ts_end.tv_sec - ts_start.tv_sec) * 1000 * 1000;       \
	} else {                                                               \
		diff_microsec =                                                \
			(ts_end.tv_nsec +                                      \
			(1000*1000*1000 - ts_start.tv_nsec)) / 1000;           \
		diff_microsec +=                                               \
			(ts_end.tv_sec - ts_start.tv_sec - 1) * 1000 * 1000;   \
	}                                                                      \
})


/**
 * cam_common_util_get_string_index()
 *
 * @brief                  Match the string from list of strings to return
 *                         matching index
 *
 * @strings:               Pointer to list of strings
 * @num_strings:           Number of strings in 'strings'
 * @matching_string:       String to match
 * @index:                 Pointer to index to return matching index
 *
 * @return:                0 for success
 *                         -EINVAL for Fail
 */
int cam_common_util_get_string_index(const char **strings,
	uint32_t num_strings, char *matching_string, uint32_t *index);

#endif /* _CAM_COMMON_UTIL_H_ */
