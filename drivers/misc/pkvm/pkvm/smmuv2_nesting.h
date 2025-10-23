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

/* Global address offset mask for SMMUv2 */
#define SMMU_V2_GLB_ADDR_OFFSET_MASK   0x0000FFFFU
/* Maximum number of context banks and CBARs per SMMU */
#define ARM_SMMU_MAX_CBS 128
#define ARM_SMMU_MAX_CBARS ARM_SMMU_MAX_CBS
/* Maximum number of SMRs and S2CRs per SMMU */
#define ARM_SMMU_MAX_SMRS 256
#define ARM_SMMU_MAX_S2CRS ARM_SMMU_MAX_SMRS
/**
 * struct smmu_v2_cbar_info - Context Bank information for SMMUv2
 */
struct smmu_v2_cbar_info {
	u32 val;
	u32 idx;
};

struct smmu_v2_s2cr_info {
	u32 val;
	u32 idx;
};
/**
 * struct smmu_v2_smr_info - Stream Matching Register information for SMMUv2
 * @sid_and_mask: Stream ID and mask value
 * @idx: SMR index
 */
struct smmu_v2_smr_info {
	u32 val; /* [31]:valid, [30:16]:mask, [15:0]:stream_id */
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
	u32 irq_s2_cb; /* Context bank s2 fault irq */
	u32 num_smr;  /* SMR allocation for NS */
	u32 num_s2cr; /* S2CR allocation for NS */
	u32 num_cbar; /* CBAR allocation for NS */
	u32 num_cb;   /* CB allocation for NS */
	u32 pgshift;  /* Page size 4KB or 64KB  */
	u32 numpage;
	u32 host_s2_cb_idx;  /* Index of reserved host S2 context bank */
	struct smmu_v2_smr_info smr_pool[ARM_SMMU_MAX_SMRS];
	struct smmu_v2_s2cr_info s2cr_pool[ARM_SMMU_MAX_S2CRS];
	struct smmu_v2_cbar_info cbar_pool[ARM_SMMU_MAX_CBS];
};

int smmuv2_hyp_nesting_init(void);
int smmuv2_nesting_init(void);

#endif /* _SMMUV2_NESTING_H */
