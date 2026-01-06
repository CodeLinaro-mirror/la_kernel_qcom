/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _CRYPTO_QTI_COMMON_H
#define _CRYPTO_QTI_COMMON_H

#include <linux/blk-crypto.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#if IS_ENABLED(CONFIG_QTI_CRYPTO_FDE)
#include <linux/errno.h>
#include <linux/delay.h>

#define QTI_ICE_MAX_BIST_CHECK_COUNT 100
#define QTI_ICE_TYPE_NAME_LEN 8
#endif
#define RAW_SECRET_SIZE 32

/* Storage types for crypto */
#define UFS_CE 10
#define SDCC_CE 20

struct ice_mmio_data {
	void __iomem *ice_base_mmio;
	void __iomem *ice_hwkm_mmio;
	struct device *dev;
	void __iomem *km_base;
	struct resource *km_res;
	struct list_head clk_list_head;
	bool is_hwkm_clk_available;
	bool is_hwkm_enabled;
};

int crypto_qti_keyslot_program(void __iomem *base,
			       const struct blk_crypto_key *key,
			       unsigned int slot, u8 data_unit_mask,
			       int capid, int storage_type);
int crypto_qti_keyslot_evict(void __iomem *base,
			     unsigned int slot, int storage_type);
int crypto_qti_derive_raw_secret(const u8 *wrapped_key,
				 unsigned int wrapped_key_size, u8 *secret,
				 unsigned int secret_size);

#if IS_ENABLED(CONFIG_QTI_CRYPTO_FDE)
struct crypto_vops_qti_entry {
	void __iomem *icemmio_base;
	void __iomem *hwkm_slave_mmio_base;
	uint32_t ice_hw_version;
	uint8_t ice_dev_type[QTI_ICE_TYPE_NAME_LEN];
	uint32_t flags;
};

/* MSM ICE Crypto Data Unit of target DUN of Transfer Request */
enum ice_crypto_data_unit {
	ICE_CRYPTO_DATA_UNIT_512_B	= 0,
	ICE_CRYPTO_DATA_UNIT_1_KB	= 1,
	ICE_CRYPTO_DATA_UNIT_2_KB	= 2,
	ICE_CRYPTO_DATA_UNIT_4_KB	= 3,
	ICE_CRYPTO_DATA_UNIT_8_KB	= 4,
	ICE_CRYPTO_DATA_UNIT_16_KB	= 5,
	ICE_CRYPTO_DATA_UNIT_32_KB	= 6,
	ICE_CRYPTO_DATA_UNIT_64_KB	= 7,
};
struct request;

enum ice_cryto_algo_mode {
	ICE_CRYPTO_ALGO_MODE_AES_ECB = 0x0,
	ICE_CRYPTO_ALGO_MODE_AES_XTS = 0x3,
};

enum ice_crpto_key_size {
	ICE_CRYPTO_KEY_SIZE_128 = 0x0,
	ICE_CRYPTO_KEY_SIZE_256 = 0x2,
};

struct ice_crypto_setting {
	enum ice_crpto_key_size		key_size;
	enum ice_cryto_algo_mode	algo_mode;
	short				key_index;
};

struct ice_data_setting {
	struct ice_crypto_setting	crypto_data;
	bool				sw_forced_context_switch;
	bool				decr_bypass;
	bool				encr_bypass;
};
typedef void (*ice_error_cb)(void *, u32 error);
int crypto_qti_ice_setup_ice_hw(const char *storage_type, int enable);
int crypto_qti_ice_config_start(struct request *req,
				struct ice_data_setting *setting);
unsigned int crypto_qti_ice_get_num_fde_slots(void);
int crypto_qti_ice_init_fde_node(struct device *dev);
int crypto_qti_ice_add_userdata(const unsigned char *inhash);
#endif /* CONFIG_QTI_CRYPTO_FDE */

#endif /* _CRYPTO_QTI_COMMON_H */
