// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <nvhe/iommu.h>
#include "smmuv2_nesting.h"

#ifdef MODULE
const struct pkvm_module_ops		*smmuv2_mod_ops;
#endif

static int smmuv2_nesting_init(void)
{
	return 0;
}

static struct kvm_hyp_iommu *smmuv2_nesting_id_to_iommu(pkvm_handle_t smmu_id)
{
	return NULL;
}

static int smmuv2_nesting_alloc_domain(struct kvm_hyp_iommu_domain *domain, int type)
{
	return 0;
}

static void smmuv2_nesting_free_domain(struct kvm_hyp_iommu_domain *domain)
{
}

static int smmuv2_nesting_detach_dev(struct kvm_hyp_iommu *iommu,
				     struct kvm_hyp_iommu_domain *domain,
				     u32 sid, u32 pasid)
{
	return 0;
}

static int smmuv2_nesting_attach_dev(struct kvm_hyp_iommu *iommu,
				     struct kvm_hyp_iommu_domain *domain,
				     u32 endpoint_id, u32 pasid, u32 pasid_bits,
				     unsigned long flags)
{
	return 0;
}

static int smmuv2_nesting_dabt_handler(struct user_pt_regs *regs, u64 esr, u64 addr)
{
	return 0;
}

static int smmuv2_nesting_suspend(struct kvm_hyp_iommu *iommu)
{
	return 0;
}

static int smmuv2_nesting_resume(struct kvm_hyp_iommu *iommu)
{
	return 0;
}

static void smmuv2_nesting_idmap(struct kvm_hyp_iommu_domain *domain,
				 phys_addr_t start, phys_addr_t end, int prot)
{
}

#ifdef MODULE
int smmuv2_nesting_init_module(const struct pkvm_module_ops *ops)
{
	if (!ops)
		return -EINVAL;

	smmuv2_mod_ops = ops;
	ops->register_host_dabt_fault_handler(smmuv2_nesting_dabt_handler);

	return 0;
}
#endif

struct kvm_iommu_ops smmuv2_hyp_nesting_ops = {
	.init				= smmuv2_nesting_init,
	.get_iommu_by_id		= smmuv2_nesting_id_to_iommu,
	.alloc_domain			= smmuv2_nesting_alloc_domain,
	.free_domain			= smmuv2_nesting_free_domain,
	.attach_dev			= smmuv2_nesting_attach_dev,
	.detach_dev			= smmuv2_nesting_detach_dev,
	.suspend			= smmuv2_nesting_suspend,
	.resume				= smmuv2_nesting_resume,
	.host_stage2_idmap		= smmuv2_nesting_idmap,
};
