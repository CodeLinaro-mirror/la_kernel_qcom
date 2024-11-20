/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved. */
#ifndef _NF_NATTYPE_H
#define _NF_NATTYPE_H

#include <linux/types.h>

#define NATTYPE_TIMEOUT 300

enum nft_nattype_mode {
	NFT_MODE_DNAT,
	NFT_MODE_FORWARD_IN,
	NFT_MODE_FORWARD_OUT
};

enum nft_nattype_type {
	NFT_TYPE_PORT_ADDRESS_RESTRICTED,
	NFT_TYPE_ENDPOINT_INDEPENDENT,
	NFT_TYPE_ADDRESS_RESTRICTED
};

enum nft_nattype_attributes {
	NFTA_NATTYPE_UNSPEC,
	NFTA_NATTYPE_MODE,
	NFTA_NATTYPE_TYPE,
	__NFTA_NATTYPE_MAX,
};

#define NFTA_NATTYPE_MAX (__NFTA_NATTYPE_MAX - 1)

struct nf_nattype_info {
	__u32 mode;
	__u32 type;
};

#endif /* _NF_NATTYPE_H */
