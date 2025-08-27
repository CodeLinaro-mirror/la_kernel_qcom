// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <nvhe/iommu.h>
#include <nvhe/trace.h>
#include "smmuv2_nesting.h"
#include "arm-smmuv2-defs.h"

#ifdef MODULE
const struct pkvm_module_ops *smmu_v2_ops;
#endif

unsigned long smmu_v2_nested_count;
struct smmu_v2_nested *smmu_v2_nested_base;

#define for_each_smmu(smmu) \
	for ((smmu) = smmu_v2_nested_base; \
	     (smmu) != &smmu_v2_nested_base[smmu_v2_nested_count]; \
	     (smmu)++)

#define SMMU_V2_DEBUG 0

static void trace___hyp_printk(u8 fmt_id, u64 a, u64 b, u64 c, u64 d)
{
	data.vops->tracing_mod_hyp_printk(fmt_id, a, b, c, d);
}

#define smmu_v2_debug_print(fmt, ...) \
	do { \
		if (SMMU_V2_DEBUG) \
			trace_hyp_printk(fmt, ##__VA_ARGS__); \
	} while (0)

/* Helper functions for register access */
static void smmuv2_cbar_read(struct smmu_v2_nested *smmu, u32 offset, u64 *buf)
{
	/* TBD need to maskout "nested" here. */
	*buf = arm_smmu_gr1_read(smmu->base_va, offset);
	smmu_v2_debug_print("smmu_v2_cbar_read: regoffset: %llx, buf: %llx\n",
			    smmu->base_pa + offset, *buf);
}

static int smmuv2_smr_write(struct smmu_v2_nested *smmu, u32 offset, u32 val)
{
	arm_smmu_gr0_write(smmu->base_va, offset, val);
	smmu_v2_debug_print("smmu_v2_virt_smr_write: write to is: %llx, val is: %llx\n",
			    arm_smmu_gr0_read(smmu->base_va, offset), val);
	return 0;
}

static int smmuv2_s2cr_write(struct smmu_v2_nested *smmu, u32 offset, u32 val)
{
	/* TBD need to write "nested" here. */
	arm_smmu_gr0_write(smmu->base_va, offset, val);
	smmu_v2_debug_print("smmu_v2_virt_s2cr_write: write to is: %llx, val is: %llx\n",
			    arm_smmu_gr0_read(smmu->base_va, offset), val);
	return 0;
}

static int smmuv2_cbar_write(struct smmu_v2_nested *smmu, u32 offset, u32 val)
{
	/* TBD need to attach "nested" here. */
	arm_smmu_gr1_write(smmu->base_va, offset, val);
	smmu_v2_debug_print("smmu_v2_virt_cbar_write: write to is: %llx, val is: %llx\n",
			    arm_smmu_gr1_read(smmu->base_va, offset), val);
	return 0;
}

/* Core register access functions */
static int smmuv2_read_global_region_0(struct smmu_v2_nested *smmu, u64 offset, u32 len, u64 *buf)
{
	/* Read the register value first, then handle specific cases */
	*buf = arm_smmu_gr0_read(smmu->base_va, offset);
	/* Use specific log messages for special registers */
	if (offset == ARM_SMMU_GR0_ID0) {
		smmu_v2_debug_print("smmu_v2_id0_read, addr: %llx, buf: %llx\n",
				    smmu->base_pa + offset, *buf);
	} else if (offset == ARM_SMMU_GR0_ID1) {
		smmu_v2_debug_print("smmu_v2_id1_read, addr: %llx, buf: %llx\n",
				    smmu->base_pa + offset, *buf);
	} else if (offset >= ARM_SMMU_GR0_S2CR(0) && offset <= ARM_SMMU_GR0_S2CR(MAXNUM_SMR)) {
		/* S2CR register range */
		smmu_v2_debug_print("smmu_v2_s2cr_read, addr: %llx, buf: %llx\n",
				    smmu->base_pa + offset, *buf);
	} else {
		/* Default case for all other registers */
		smmu_v2_debug_print("smmu_v2_read_global_region_0, addr: %llx, buf: %llx\n",
				    smmu->base_pa + offset, *buf);
	}
	return 0;
}

static int smmuv2_read_global_region_1(struct smmu_v2_nested *smmu, u64 offset, u32 len, u64 *buf)
{
	if (offset >= ARM_SMMU_GR1_CBAR(0) &&
	    offset <= ARM_SMMU_GR1_CBAR(MAXNUM_CBAR)) {
		/* TBD need to maskout "nested" here. */
		smmuv2_cbar_read(smmu, offset, buf);
	} else {
		*buf = arm_smmu_gr1_read(smmu->base_va, offset);
		smmu_v2_debug_print("smmu_v2_read_global_region_1, addr: %llx, buf: %llx\n",
				    smmu->base_pa + offset, *buf);
	}
	return 0;
}

static int smmuv2_write_global_region_0(struct smmu_v2_nested *smmu,
					u32 offset,
					unsigned int len,
					u64 val)
{
	/* Handle special register ranges first */
	if (offset >= ARM_SMMU_GR0_SMR(0) && offset <= ARM_SMMU_GR0_SMR(MAXNUM_SMR))
		return smmuv2_smr_write(smmu, offset, (u32)val);
	if (offset >= ARM_SMMU_GR0_S2CR(0) && offset <= ARM_SMMU_GR0_S2CR(MAXNUM_SMR))
		return smmuv2_s2cr_write(smmu, offset, (u32)val);
	/* Check if access is to fault registers which are handled by TZ */
	if ((offset >= ARM_SMMU_GFAR0 && offset <= ARM_SMMU_GFSR) ||
	    (offset >= ARM_SMMU_GR0_sGFSYNR0 && offset <= ARM_SMMU_GR0_sGFSYNR2) ||
	    (offset >= ARM_SMMU_GPAR0 && offset <= ARM_SMMU_GPAR1) ||
	    (offset >= ARM_SMMU_TRANSBUF_READSW && offset <= ARM_SMMU_TRANSBUF_DR2)) {
		return -EPERM;
	}
	/* Handle general register writes based on access size */
	void *reg_addr = (void *)((u64)smmu->base_va + offset);

	if (len == sizeof(u32)) {
		writel((u32)val, reg_addr);
		smmu_v2_debug_print("SMMU_EL2_WRITE32, offset: 0x%llx, val: 0x%x\n",
				    ((u64)smmu->base_pa + offset), (u32)val);
	} else if (len == sizeof(u64)) {
		writel((u64)val, reg_addr);
		smmu_v2_debug_print("SMMU_EL2_WRITE64, offset: 0x%llx, val: 0x%llx\n",
				    ((u64)smmu->base_pa + offset), val);
	} else {
		return -EINVAL; /* Invalid access size */
	}
	return 0;
}

static int smmuv2_write_global_region_1(struct smmu_v2_nested *smmu,
					u64 offset,
					unsigned int len,
					u64 val)
{
	/* Handle CBAR registers specially */
	if (offset >= ARM_SMMU_GR1_CBAR(0) &&
	    offset <= ARM_SMMU_GR1_CBAR(MAXNUM_CBAR)) {
		return smmuv2_cbar_write(smmu, offset, (u32)val);
	}
	/* Handle general register writes based on access size */
	void *reg_addr = (void *)((u64)smmu->base_va + ARM_SMMU_GLOBAL_REGION1_OFFSET + offset);

	if (len == sizeof(u32)) {
		writel((u32)val, reg_addr);
		smmu_v2_debug_print("SMMU_EL2_WRITE32, offset: 0x%llx, val: 0x%x\n",
				    ((u64)smmu->base_pa + offset), (u32)val);
	} else if (len == sizeof(u64)) {
		writel((u64)val, reg_addr);
		smmu_v2_debug_print("SMMU_EL2_WRITE64, offset: 0x%llx, val: 0x%llx\n",
				    ((u64)smmu->base_pa + offset), val);
	} else {
		return -EINVAL; /* Invalid access size */
	}
	return 0;
}

/* Device access and fault handling */
static int smmuv2_nesting_dabt_device(struct smmu_v2_nested *smmu,
				      struct user_pt_regs *regs,
				      u64 esr, u32 addr)
{
	bool is_write = esr & ESR_ELx_WNR;
	unsigned int len = BIT((esr & ESR_ELx_SAS) >> ESR_ELx_SAS_SHIFT);
	int rd = (esr & ESR_ELx_SRT_MASK) >> ESR_ELx_SRT_SHIFT;
	u64 val = regs->regs[rd];
	u32 offset;
	int ret;

	smmu_v2_debug_print("addr: 0x%llx, val: 0x%llx, esr: 0x%llx\n",
			    addr, regs->regs[rd], esr);
	smmu_v2_debug_print("size: %d, rd: %d, is_write: %d\n",
			    len, rd, is_write);

	kvm_iommu_lock(&smmu->iommu);
	offset = (u32)(addr & SMMU_V2_GLB_ADDR_OFFSET_MASK);

	if (offset < ARM_SMMU_GLOBAL_REGION1_OFFSET) {
		if (is_write) {
			ret = smmuv2_write_global_region_0(smmu, offset, len, val);
		} else {
			ret = smmuv2_read_global_region_0(smmu, offset, len, &val);
			regs->regs[rd] = val;
		}
	} else if (offset < ARM_SMMU_GLOBAL_REGION2_OFFSET) {
		offset -= ARM_SMMU_GLOBAL_REGION1_OFFSET;
		if (is_write) {
			ret = smmuv2_write_global_region_1(smmu, offset, len, val);
		} else {
			ret = smmuv2_read_global_region_1(smmu, offset, len, &val);
			regs->regs[rd] = val;
		}
	} else {
		ret = -EPERM;
	}
	kvm_iommu_unlock(&smmu->iommu);
	return ret;
}

static int smmuv2_nesting_dabt_handler(struct user_pt_regs *regs, u64 esr, u64 addr)
{
	struct smmu_v2_nested *smmu;
	u32 size = ARM_SMMU_GLOBAL_REGION2_OFFSET;

	for_each_smmu(smmu) {
		/* Check if address is within this SMMU's range */
		if (addr >= smmu->base_pa && addr < smmu->base_pa + size)
			return smmuv2_nesting_dabt_device(smmu, regs, esr, addr - smmu->base_pa);
	}
	return -EPERM; /* No matching SMMU found */
}

/* Initialization functions */
static int take_over_smmus(void)
{
	struct smmu_v2_nested *smmu;
	int ret;

	for_each_smmu(smmu) {
		ret = ___pkvm_host_donate_hyp(smmu->base_pa >> PAGE_SHIFT,
					      ARM_SMMU_GLOBAL_REGION2_OFFSET >> PAGE_SHIFT,
					      true);
		if (ret)
			return ret;

		smmu->base_va = (u64)hyp_phys_to_virt(smmu->base_pa);
		smmu_v2_debug_print("donate to hyp: base_pa: %llx, base_va: %llx, size: %llx\n",
				    smmu->base_pa, smmu->base_va, ARM_SMMU_GLOBAL_REGION2_OFFSET);
		smmu->base_va = (u64)hyp_phys_to_virt(smmu->base_pa);
	}
	return 0;
}

static int smmuv2_nesting_init(void)
{
	int smmu_arr_size = PAGE_ALIGN(sizeof(*smmu_v2_nested_base) * smmu_v2_nested_count);
	int ret;

	smmu_v2_debug_print("smmu_v2_nested_base: %llx\n", (u64)smmu_v2_nested_base);
	smmu_v2_nested_base = kern_hyp_va(smmu_v2_nested_base);
	smmu_v2_debug_print("smmu_v2_nested_base hyp va: %llx\n", (u64)smmu_v2_nested_base);

	ret = __pkvm_host_donate_hyp(hyp_virt_to_phys(smmu_v2_nested_base) >> PAGE_SHIFT,
				     smmu_arr_size >> PAGE_SHIFT);
	if (ret)
		return ret;
	ret = take_over_smmus();
	if (ret)
		return ret;

	return 0;
}

#ifdef MODULE
int smmuv2_nesting_init_module(const struct pkvm_module_ops *ops)
{
	if (!ops)
		return -EINVAL;
	smmu_v2_ops = ops;
	smmu_v2_ops->register_host_dabt_fault_handler(smmuv2_nesting_dabt_handler);

	return 0;
}
#endif

/* Empty/stub functions - placed at bottom per requirements */
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

static int smmuv2_nesting_attach_dev(struct kvm_hyp_iommu *iommu,
				     struct kvm_hyp_iommu_domain *domain,
				     u32 endpoint_id, u32 pasid, u32 pasid_bits,
				     unsigned long flags)
{
	return 0;
}

static int smmuv2_nesting_detach_dev(struct kvm_hyp_iommu *iommu,
				     struct kvm_hyp_iommu_domain *domain,
				     u32 sid, u32 pasid)
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

/* Public interface operations structure - at the very bottom */
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
