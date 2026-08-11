/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#ifndef _LINUX_NETSTANDBY_H
#define _LINUX_NETSTANDBY_H

#include <uapi/linux/netstandby.h>

#ifdef __KERNEL__

#include <linux/netdevice.h>

/*
 * netstandby_exit_trigger_rule - Tuple used to program a wakeup/exit rule.
 * @valid_flags: Bitmask that marks which tuple fields are valid.
 * @src_ip: Source IPv4/IPv6 address.
 * @smac: Source MAC address.
 * @dest_ip: Destination IPv4/IPv6 address.
 * @dmac: Destination MAC address.
 * @protocol: L4 protocol selector.
 */
struct netstandby_exit_trigger_rule {
	u32 valid_flags;
	u32 src_ip[4];
	u8 smac[6];
	u32 dest_ip[4];
	u8 dmac[6];
	int protocol;
};

/*
 * netstandby_nss_priv_info - NSS-specific standby configuration.
 * @port_id: Switch port selected for wakeup handling.
 * @flags: Port-selection mode encoded with NETSTANDBY_ENTER_NSS_FLAG_*.
 */
struct netstandby_nss_priv_info {
	u8 port_id;
	u16 flags;
};

/*
 * netstandby_wifi_priv_info - Wi-Fi link mapping used for standby entry.
 * @link_id: Link identifiers passed down by the Wi-Fi integration.
 * @link_id_cnt: Number of valid entries in @link_id.
 */
struct netstandby_wifi_priv_info {
	int link_id[MAX_INTERFACE];
	int link_id_cnt;
};

/*
 * netstandby_event_compl_info - Completion notification metadata.
 * @event_type: Completion event that finished.
 * @system_type: Subsystem that reported the completion.
 */
struct netstandby_event_compl_info {
	enum netstandby_notif_type event_type;
	enum netstandby_subsystem_type system_type;
};

/*
 * netstandby_trigger_info - Trigger notification metadata.
 * @dev: Device that caused the trigger, if available.
 * @system_type: Subsystem that detected the trigger.
 * @event_type: Notification reason sent to the broker.
 */
struct netstandby_trigger_info {
	struct net_device *dev;
	enum netstandby_subsystem_type system_type;
	enum netstandby_notif_type event_type;
};

/* Reserved for future exit-time parameters. */
struct netstandby_exit_info {
	u32 reserved;
};

/*
 * netstandby_entry_info - Entry request passed to subsystem callbacks.
 * @dev: Interfaces participating in standby entry.
 * @tuple: Optional trigger tuple used by NSS wakeup programming.
 * @nss_info: NSS-specific standby settings.
 * @wifi_info: Wi-Fi link information for multi-link standby.
 * @iface_cnt: Number of valid entries in @dev.
 */
struct netstandby_entry_info {
	struct net_device *dev[MAX_INTERFACE];
	struct netstandby_exit_trigger_rule tuple;
	struct netstandby_nss_priv_info nss_info;
	struct netstandby_wifi_priv_info wifi_info;
	int iface_cnt;
};

/* Standby exit handler registered by a subsystem. */
typedef int (*netstandby_exit_cb_t)(
	void *app_data, struct netstandby_exit_info *exit_info);
/* Standby enter handler registered by a subsystem. */
typedef int (*netstandby_enter_cb_t)(
	void *app_data, struct netstandby_entry_info *enter_info);
/* Completion callback for enter/exit notifications. */
typedef void (*netstandby_event_compl_cb_t)(
	void *app_data, struct netstandby_event_compl_info *event_info);
/* Trigger callback for wakeup events detected by the subsystem. */
typedef void (*netstandby_trigger_notif_cb_t)(
	void *app_data, struct netstandby_trigger_info *trigger_info);

/* Callback bundle used when a subsystem registers with netstandby. */
struct netstandby_reg_info {
	netstandby_enter_cb_t enter_cb;
	netstandby_exit_cb_t exit_cb;
	netstandby_event_compl_cb_t enter_cmp_cb;
	netstandby_event_compl_cb_t exit_cmp_cb;
	netstandby_trigger_notif_cb_t trigger_cb;
	void *app_data;
};

/* Register NSS standby callbacks with the core netstandby driver. */
int nss_dp_get_and_register_cb(struct netstandby_reg_info *info);
/* Register Wi-Fi standby callbacks with the core netstandby driver. */
int netstandby_wifi_get_and_register_cb(struct netstandby_reg_info *info);
/* Register platform standby callbacks with the core netstandby driver. */
int netstandby_platform_get_and_register_cb(struct netstandby_reg_info *info);
/**
 * ppe_drv_port_num_from_dev - Get a switch port index from a net device.
 * @dev: Net device.
 *
 * Return: Non-negative port index on success, negative error code on failure.
 */
int ppe_drv_port_num_from_dev(struct net_device *dev);

/* Register Ethernet/STMMAC standby callbacks with the core netstandby driver. */
int stmmac_netstandby_register_cb(struct netstandby_reg_info *info);

#endif /* __KERNEL__ */

#endif
