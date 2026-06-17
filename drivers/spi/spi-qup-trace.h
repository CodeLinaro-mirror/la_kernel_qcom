/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM qup_spi_trace

#if !defined(_TRACE_SPI_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SPI_TRACE_H

#include <linux/ktime.h>
#include <linux/tracepoint.h>

#define MAX_MSG_LEN 256

TRACE_EVENT(spi_log_info,

		TP_PROTO(const char *name, struct va_format *vaf),

		TP_ARGS(name, vaf),

		TP_STRUCT__entry(
				__string(name, name)
				__dynamic_array(char, msg, MAX_MSG_LEN)
		),

		TP_fast_assign(
			__assign_str(name, name);
			if (strnlen(vaf->fmt, MAX_MSG_LEN) >= MAX_MSG_LEN) {
				/* Suspicious format string */
				WARN_ON_ONCE(1);
			} else {
				int len = vsnprintf(__get_dynamic_array(msg), MAX_MSG_LEN,
										vaf->fmt, *vaf->va);
				/* Handle error or truncation */
				WARN_ON_ONCE(len < 0 || len >= MAX_MSG_LEN);
				}
		),

		TP_printk("%s: %s", __get_str(name), __get_str(msg))
);

#endif /* _TRACE_SPI_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE spi-qup-trace
#include <trace/define_trace.h>

