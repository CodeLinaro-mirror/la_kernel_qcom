/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023-2024 Linaro Ltd.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __QCOM_TZMEM_PRIV_H
#define __QCOM_TZMEM_PRIV_H

struct device;

bool qcom_tzmem_get_status(void);
int qcom_tzmem_enable(struct device *dev);

#if IS_ENABLED(CONFIG_QCOM_TZMEM_MODE_SHMBRIDGE)
int32_t qcom_tzmem_query(phys_addr_t paddr);
int qcom_tzmem_shm_bridge_create_with_vmid(phys_addr_t paddr, size_t size, u32 vmid, u64 *handle);
#else
static inline int32_t qcom_tzmem_query(phys_addr_t paddr)
{
	return 0;
}
static inline int qcom_tzmem_shm_bridge_create_with_vmid(phys_addr_t paddr, size_t size,
							 u32 vmid, u64 *handle)
{
	return 0;
}
#endif /* CONFIG_QCOM_TZMEM_MODE_SHMBRIDGE */

#endif /* __QCOM_TZMEM_PRIV_H */
