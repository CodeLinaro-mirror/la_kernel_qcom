/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * rmnet_stats.h - Public API for rmnet per-protocol packet statistics
 */

#ifndef _RMNET_STATS_H_
#define _RMNET_STATS_H_

#include <linux/netdevice.h>

/**
 * struct rmnet_proto_stats - Protocol-level packet counters for one rmnet VND.
 *
 * All counters are aggregated across all CPUs at read time and represent
 * cumulative totals since the interface was created. Counters do not reset
 * on read.
 *
 * @rx_icmp_pkts:   ICMPv4 packets received (IP protocol = 1)
 * @rx_icmpv6_pkts: ICMPv6 packets received (IPv6 next-header = 58)
 * @tx_icmp_pkts:   ICMPv4 packets transmitted
 * @tx_icmpv6_pkts: ICMPv6 packets transmitted
 * @rx_dns_pkts:    DNS packets received (UDP or TCP, port 53)
 * @tx_dns_pkts:    DNS packets transmitted (UDP or TCP, port 53)
 */
struct rmnet_proto_stats {
	u64 rx_icmp_pkts;
	u64 rx_icmpv6_pkts;
	u64 tx_icmp_pkts;
	u64 tx_icmpv6_pkts;
	u64 rx_dns_pkts;
	u64 tx_dns_pkts;
};

/**
 * rmnet_get_proto_stats() - Read protocol stats for an rmnet VND interface.
 *
 * @dev:   Pointer to a net_device that is an rmnet virtual network device
 *         (e.g. the device backing rmnet_data0). Must not be NULL.
 *         The caller must hold a reference to @dev (via dev_hold() or
 *         equivalent) to ensure the device remains valid for the duration
 *         of this call.  RTNL lock is NOT required.
 * @stats: Caller-allocated output buffer. On success, filled with aggregated
 *         per-CPU counters. Undefined on error.
 *
 * Context: May be called from process context or softirq context.
 *          Must not be called from hard IRQ context.
 *          Caller must ensure @dev remains valid for the duration of the call.
 *
 * Returns:
 *   0       - success; @stats is valid.
 *  -EINVAL  - @dev is NULL or is not an rmnet VND device.
 */
int rmnet_get_proto_stats(struct net_device *dev,
			  struct rmnet_proto_stats *stats);

#endif /* _RMNET_STATS_H_ */
