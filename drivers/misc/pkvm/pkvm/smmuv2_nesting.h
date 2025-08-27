/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SMMUv2 Nested Virtualization Header
 *
 * This file defines structures and constants for SMMUv2 nested virtualization
 * support in the pKVM hypervisor. It includes definitions for Stream Matching
 * Registers (SMR), Context Banks (CB), and the main nested SMMU structure.
 */
#ifndef _SMMUV2_NESTING_H
#define _SMMUV2_NESTING_H

#include <kvm/iommu.h>

/* Maximum number of SMR entries (Stream Matching Registers) */
#define MAXNUM_SMR			0xff
/* Maximum number of CBAR entries (Context Bank Attribute Registers) */
#define MAXNUM_CBAR			0xff
/* Global address offset mask for SMMUv2 */
#define SMMU_V2_GLB_ADDR_OFFSET_MASK	0x0000FFFFU

/* Maximum pool size for SMR and CB entries */
#define SMMU_V2_MAX_POOL_SIZE		255

/**
 * struct smmu_v2_cb_info - Context Bank information for SMMUv2
 * @pa: Physical address of the context bank
 * @irq_num: IRQ number associated with this context bank
 * @cb_idx: Context bank index
 */
struct smmu_v2_cb_info {
	u64 pa;
	u32 irq_num;
	u32 cb_idx;
};

/**
 * struct smmu_v2_smr_info - Stream Matching Register information for SMMUv2
 * @pa: Physical address of the SMR
 * @sid_and_mask: Stream ID and mask value
 * @idx: SMR index
 */
struct smmu_v2_smr_info {
	u64 pa;
	u32 sid_and_mask;
	u32 idx;
};

/**
 * struct smmu_v2_nested - SMMUv2 nested virtualization structure
 * @iommu: Base IOMMU structure
 * @base_pa: Base physical address
 * @base_va: Base virtual address
 * @size: Size of the SMMU region
 * @ias: Input Address Size
 * @oas: Output Address Size
 * @pgsize_bitmap: Page size bitmap
 * @cr0: Control Register 0 value
 * @num_smr: Number of Stream Matching Registers
 * @num_cb: Number of Context Banks
 * @smr_pool: Pool of SMR information structures
 * @cb_pool: Pool of Context Bank information structures
 */
struct smmu_v2_nested {
	struct kvm_hyp_iommu iommu;
	u64 base_pa;
	u64 base_va;
	u32 size;
	u32 ias;
	u32 oas;
	u32 pgsize_bitmap;
	u32 cr0;
	u32 num_smr;
	u32 num_cb;
	struct smmu_v2_smr_info smr_pool[SMMU_V2_MAX_POOL_SIZE];
	struct smmu_v2_cb_info cb_pool[SMMU_V2_MAX_POOL_SIZE];
};

#if defined(__KVM_NVHE_HYPERVISOR__) && defined(MODULE)

#include <asm/kvm_pkvm_module.h>

extern const struct pkvm_module_ops *smmu_v2_ops;
extern const struct pkvm_module_ops *smmu_v2_ops;

#undef memset
#undef memcpy
#undef kern_hyp_va

#define CALL_FROM_OPS(fn, ...)		(smmu_v2_ops->(fn)(__VA_ARGS__))

#define hyp_virt_to_phys(x)		CALL_FROM_OPS(hyp_pa, x)
#define hyp_phys_to_virt(x)		CALL_FROM_OPS(hyp_va, x)
#define memcpy(x, y, z)			CALL_FROM_OPS(memcpy, x, y, z)
#define pkvm_udelay(x)			CALL_FROM_OPS(udelay, x)
#define ___pkvm_host_donate_hyp(x, y, z) \
	CALL_FROM_OPS(host_donate_hyp, x, y, z)
#define kern_hyp_va(x) \
	((void *)CALL_FROM_OPS(kern_hyp_va, (unsigned long)x))
#define __pkvm_host_donate_hyp(x, y) \
	CALL_FROM_OPS(host_donate_hyp, x, y, false)
#define kvm_iommu_donate_pages_atomic(x) \
	CALL_FROM_OPS(iommu_donate_pages_atomic, x)
#define kvm_iommu_reclaim_pages_atomic(x, y) \
	CALL_FROM_OPS(iommu_reclaim_pages_atomic, x, y)
#define kvm_iommu_snapshot_host_stage2(x) \
	CALL_FROM_OPS(iommu_snapshot_host_stage2, x)
#define __pkvm_host_share_hyp(x) \
	CALL_FROM_OPS(host_share_hyp, x)
#define __pkvm_host_unshare_hyp(x) \
	CALL_FROM_OPS(host_unshare_hyp, x)

#endif /* defined(__KVM_NVHE_HYPERVISOR__) && defined(MODULE) */

#endif /* _SMMUV2_NESTING_H */
