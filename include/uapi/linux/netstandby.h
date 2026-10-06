/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#ifndef _UAPI_LINUX_NETSTANDBY_H
#define _UAPI_LINUX_NETSTANDBY_H

/* Maximum number of interfaces that can be carried in a standby request. */
#define MAX_INTERFACE 16

/*
 * Exit-trigger tuple flags.
 * These bits indicate which tuple fields are valid when programming a wakeup
 * rule in the kernel.
 */
#define NETSTANDBY_EXIT_TRIGGER_RULE_SRC_IP_VALID 0x00000001
#define NETSTANDBY_EXIT_TRIGGER_RULE_DES_IP_VALID 0x00000002
#define NETSTANDBY_EXIT_TRIGGER_RULE_SRC_MAC_VALID 0x00000004
#define NETSTANDBY_EXIT_TRIGGER_RULE_DEST_MAC_VALID 0x00000008
#define NETSTANDBY_EXIT_TRIGGER_RULE_PROTOCOL_VALID 0x00000010
#define NETSTANDBY_EXIT_TRIGGER_RULE_IPV4 0x00000020
#define NETSTANDBY_EXIT_TRIGGER_RULE_IPV6 0x00000040

/*
 * NSS standby entry flags.
 * These bits describe which switch-port selection mode the caller requested.
 */
#define NETSTANDBY_ENTER_NSS_FLAG_VALID_PORT_NONE 0x00000001
#define NETSTANDBY_ENTER_NSS_FLAG_VALID_PORT_ALL 0x00000002
#define NETSTANDBY_ENTER_NSS_FLAG_VALID_PORT_IDX 0x00000004

/* Subsystem that owns a standby operation or completion event. */
enum netstandby_subsystem_type {
	NETSTANDBY_SUBSYSTEM_TYPE_NSS = 0,
	NETSTANDBY_SUBSYSTEM_TYPE_WIFI = 1,
	NETSTANDBY_SUBSYSTEM_TYPE_PLATFORM = 2,
	NETSTANDBY_SUBSYSTEM_TYPE_ETHERNET = 3,
	NETSTANDBY_SUBSYSTEM_TYPE_MAX,
};

/* Notification type reported by the kernel back to user space. */
enum netstandby_notif_type {
	NETSTANDBY_NOTIF_ENTER_COMPLETE = 0,
	NETSTANDBY_NOTIF_EXIT_COMPLETE,
	NETSTANDBY_NOTIF_TRIGGER,
};

#endif /* _UAPI_LINUX_NETSTANDBY_H */
