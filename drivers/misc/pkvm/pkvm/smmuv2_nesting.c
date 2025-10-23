// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <nvhe/iommu.h>
#include <nvhe/trace.h>
#include "smmuv2_nesting.h"
#include "arm-smmuv2-defs.h"
#include "qcom_smmu_dispatcher.h"

static struct smmu_vendor_data data;
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
	int i;

	/* Validate CBAR offset and convert to index */
	if (!ARM_SMMU_GR1_CBAR_VALID(offset, smmu->num_cbar)) {
		*buf = 0;
		return;
	}
	i = ARM_SMMU_GR1_CBAR_INDEX(offset);

	/* Read current hardware value */
	*buf = arm_smmu_gr1_read(smmu, offset);

	/* Extract current type from hardware */
	u32 current_type = FIELD_GET(ARM_SMMU_CBAR_TYPE, *buf);

	/* If hardware has S1_TRANS_S2_TRANS, convert back to S1_TRANS_S2_BYPASS for EL1 */
	if (current_type == CBAR_TYPE_S1_TRANS_S2_TRANS) {
		/* Clear TYPE and VMID fields, and bits [8:15] */
		*buf &= ~(ARM_SMMU_CBAR_TYPE | ARM_SMMU_CBAR_VMID | 0xFF00);

		/* Set BYPASS type */
		*buf |= FIELD_PREP(ARM_SMMU_CBAR_TYPE, CBAR_TYPE_S1_TRANS_S2_BYPASS);

		/* Restore original VMID that EL1 wrote */
		u32 original_vmid = smmu->cbar_pool[i].val & ARM_SMMU_CBAR_VMID;
		*buf |= original_vmid;
	}

	smmu_v2_debug_print("cbar_read: idx: %d, regoffset: %llx, HW_val: 0x%x, EL1_val: 0x%llx\n",
			    i, smmu->base_pa + offset, (u32)arm_smmu_gr1_read(smmu, offset), *buf);
}

static int smmuv2_smr_write(struct smmu_v2_nested *smmu, u32 offset, u32 val)
{
	int i;

	/* Validate SMR offset and convert to index */
	if (!ARM_SMMU_GR0_SMR_VALID(offset, smmu->num_smr)) {
		smmu_v2_debug_print("smmu_v2_smr_write: invalid offset: %llx\n", offset);
		return -EINVAL;
	}
	i = ARM_SMMU_GR0_SMR_INDEX(offset);

	/* TBD: should we check valid bit before allowing to write? */
	smmu->smr_pool[i].val = val;
	smmu->smr_pool[i].idx = i;
	arm_smmu_gr0_write(smmu, offset, val);
	smmu_v2_debug_print("smmu_v2_virt_smr_write: write to is: %llx, val is: %llx\n",
			    (u64)arm_smmu_gr0_read(smmu, offset), (u64)val);
	return 0;
}

static int smmuv2_s2cr_write(struct smmu_v2_nested *smmu, u32 offset, u32 val)
{
	int i;
	u32 hw_val = val;  /* Value to write to hardware */
	u32 current_type;

	/* Validate S2CR offset and convert to index */
	if (!ARM_SMMU_GR0_S2CR_VALID(offset, smmu->num_s2cr)) {
		smmu_v2_debug_print("smmu_v2_s2cr_write: invalid offset: %llx\n", offset);
		return -EINVAL;
	}
	i = ARM_SMMU_GR0_S2CR_INDEX(offset);

	/*
	 * Save the ORIGINAL value for later read operations.
	 * This is what EL1 will see on reads.
	 */
	smmu->s2cr_pool[i].val = val;
	smmu->s2cr_pool[i].idx = i;

	/* Extract current S2CR type from the original value */
	current_type = (val & ARM_SMMU_S2CR_TYPE) >> 16;

	/* If type is BYPASS, modify hardware value to TRANS with host CB */
	if (current_type == S2CR_TYPE_BYPASS) {
		/* Clear the type and CBNDX fields in hardware value */
		hw_val &= ~(ARM_SMMU_S2CR_TYPE | ARM_SMMU_S2CR_CBNDX);
		/* Set type to TRANS and CBNDX to host_s2_cb_idx */
		hw_val |= FIELD_PREP(ARM_SMMU_S2CR_TYPE, S2CR_TYPE_TRANS);
		hw_val |= FIELD_PREP(ARM_SMMU_S2CR_CBNDX, smmu->host_s2_cb_idx);
	}

	/* Write the (possibly modified) value to hardware */
	arm_smmu_gr0_write(smmu, offset, hw_val);

	smmu_v2_debug_print("s2cr_write: idx: %d, EL1_val: 0x%x, HW_val: 0x%x, stored: 0x%x\n",
			    i, val, hw_val, smmu->s2cr_pool[i].val);

	return 0;
}

static int smmuv2_cbar_write(struct smmu_v2_nested *smmu, u32 offset, u32 val)
{
	int i;
	u32 hw_val = val;  /* Value to write to hardware */
	u32 current_type;

	/* Validate CBAR offset and convert to index */
	if (!ARM_SMMU_GR1_CBAR_VALID(offset, smmu->num_cbar)) {
		smmu_v2_debug_print("smmu_v2_cbar_write: invalid offset: %llx\n", offset);
		return -EINVAL;
	}
	i = ARM_SMMU_GR1_CBAR_INDEX(offset);

	/*
	 * Save the ORIGINAL value for later read operations.
	 * This is what EL1 will see on reads.
	 */
	smmu->cbar_pool[i].val = val;
	smmu->cbar_pool[i].idx = i;

	/* Extract current CBAR type from the original value */
	current_type = FIELD_GET(ARM_SMMU_CBAR_TYPE, val);

	/* If type is S1_TRANS_S2_BYPASS, modify hardware value for nested translation */
	if (current_type == CBAR_TYPE_S1_TRANS_S2_BYPASS) {
		/* Clear the TYPE, VMID, and S1-specific fields (bits [8:15]) in hardware value */
		hw_val &= ~(ARM_SMMU_CBAR_TYPE | ARM_SMMU_CBAR_VMID |
			    ARM_SMMU_CBAR_S1_MEMATTR | ARM_SMMU_CBAR_S1_BPSHCFG);

		/* Set type to S1_TRANS_S2_TRANS for nested translation */
		hw_val |= FIELD_PREP(ARM_SMMU_CBAR_TYPE, CBAR_TYPE_S1_TRANS_S2_TRANS);

		/* Set VMID to HOST_S2_VMID (0x3) */
		hw_val |= FIELD_PREP(ARM_SMMU_CBAR_VMID, HOST_S2_VMID);

		/* Set bits [8:15] to host_s2_cb_idx (S2 host context bank) */
		hw_val |= (smmu->host_s2_cb_idx << 8);
	}

	/* Write the (possibly modified) value to hardware */
	arm_smmu_gr1_write(smmu, offset, hw_val);

	smmu_v2_debug_print("cbar_write: idx: %d, EL1_val: 0x%x, HW_val: 0x%x, stored: 0x%x\n",
			    i, val, hw_val, smmu->cbar_pool[i].val);

	return 0;
}

/* Core register access functions */
static int smmuv2_read_global_region_0(struct smmu_v2_nested *smmu, u64 offset, u32 len, u64 *buf)
{
	/* Read the register value first, then handle specific cases */
	*buf = arm_smmu_gr0_read(smmu, offset);
	/* Use specific log messages for special registers */
	if (offset == ARM_SMMU_GR0_ID0) {
		*buf = (*buf & ~ARM_SMMU_ID0_NUMSMRG) | (smmu->num_smr & ARM_SMMU_ID0_NUMSMRG);
		smmu_v2_debug_print("smmu_v2_id0_read, addr: %llx, buf: %llx\n",
				    smmu->base_pa + offset, *buf);
	} else if (offset == ARM_SMMU_GR0_ID1) {
		*buf = (*buf & ~ARM_SMMU_ID1_NUMCB) | (smmu->num_cb & ARM_SMMU_ID1_NUMCB);
		smmu_v2_debug_print("smmu_v2_id1_read, addr: %llx, buf: %llx\n",
				    smmu->base_pa + offset, *buf);
	} else if (offset >= ARM_SMMU_GR0_SMR(0) &&
		   offset <= ARM_SMMU_GR0_SMR(ARM_SMMU_MAX_SMRS - 1)) {
		/* SMR register range */
		smmu_v2_debug_print("smmu_v2_smr_read, addr: %llx, buf: %llx\n",
				    smmu->base_pa + offset, *buf);
	} else if (offset >= ARM_SMMU_GR0_S2CR(0) &&
		   offset <= ARM_SMMU_GR0_S2CR(ARM_SMMU_MAX_S2CRS - 1)) {
		/* S2CR register range - read from hardware and virtualize type + CBNDX */
		int i;

		/* Validate S2CR offset and convert to index */
		if (!ARM_SMMU_GR0_S2CR_VALID(offset, smmu->num_s2cr))
			return -EINVAL;  /* Invalid S2CR offset */
		i = ARM_SMMU_GR0_S2CR_INDEX(offset);

		/* Read current hardware value */
		*buf = arm_smmu_gr0_read(smmu, offset);

		/* Extract current type */
		u32 current_type = (*buf & ARM_SMMU_S2CR_TYPE) >> 16;

		/* If hardware has TRANS, convert to BYPASS for EL1 */
		if (current_type == S2CR_TYPE_TRANS) {
			/* Clear type and CBNDX fields */
			*buf &= ~(ARM_SMMU_S2CR_TYPE | ARM_SMMU_S2CR_CBNDX);

			/* Set BYPASS type */
			*buf |= FIELD_PREP(ARM_SMMU_S2CR_TYPE, S2CR_TYPE_BYPASS);

			/* Restore original CBNDX that EL1 wrote */
			u32 original_cbndx = smmu->s2cr_pool[i].val & ARM_SMMU_S2CR_CBNDX;
			*buf |= original_cbndx;
		}

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
	    offset <= ARM_SMMU_GR1_CBAR(ARM_SMMU_MAX_CBARS - 1)) {
		/* TBD need to maskout "nested" here. */
		smmuv2_cbar_read(smmu, offset, buf);
	} else {
		*buf = arm_smmu_gr1_read(smmu, offset);
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
	if (offset >= ARM_SMMU_GR0_SMR(0) && offset <= ARM_SMMU_GR0_SMR(ARM_SMMU_MAX_SMRS - 1))
		return smmuv2_smr_write(smmu, offset, (u32)val);
	if (offset >= ARM_SMMU_GR0_S2CR(0) && offset <= ARM_SMMU_GR0_S2CR(ARM_SMMU_MAX_S2CRS - 1))
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
	    offset <= ARM_SMMU_GR1_CBAR(ARM_SMMU_MAX_CBARS - 1)) {
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
	}
	return 0;
}

static int update_s2cr_profile(struct smmu_v2_nested *smmu)
{
	int i;
	u32 s2cr_val;

	for (i = smmu->num_s2cr - 1; i >= 0; i--) {
		s2cr_val = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_S2CR(i));
		if ((s2cr_val & ARM_SMMU_S2CR_TYPE) >> 16 == S2CR_TYPE_FAULT)
			break; /* Skip invalid entries */
		smmu_v2_debug_print("S2CR[%d]: val: %llx\n",
				    i, s2cr_val);
	}
	smmu->num_s2cr = i + 1;
	smmu->num_smr = smmu->num_s2cr; /* smr == s2cr */
	WARN_ON(smmu->num_s2cr == 0);
	return 0;
}

static int update_cbar_profile(struct smmu_v2_nested *smmu)
{
	int i;
	/* Get available number of context banks from cbar register */
	for (i = smmu->num_cbar - 1; i >= 0; i--) {
		u32 cbar_val = arm_smmu_gr1_read(smmu, ARM_SMMU_GR1_CBAR(i));

		if (((cbar_val & ARM_SMMU_CBAR_TYPE) >> 16) ==
		    CBAR_TYPE_S1_TRANS_S2_FAULT) {
			break; /* Skip invalid entries */
		}
		smmu_v2_debug_print("CBAR[%d]: val: %llx\n",
				    i, cbar_val);
	}
	smmu->num_cbar = i + 1;
	smmu->num_cb = smmu->num_cbar; /* cbar == cb */
	return 0;
}

static int smmu_attach_stage_2(void)
{
	struct smmu_v2_nested *smmu;
	struct io_pgtable_cfg *pt_cfg;
	u64 vttbr;
	u32 vtcr;

	if (!data.vdomain)
		return -EINVAL;

	pt_cfg = &data.vdomain->pgtable->cfg;

	/* Extract stage-2 configuration from idmapped domain */
	vttbr = pt_cfg->arm_lpae_s2_cfg.vttbr;

	/* Build VTCR from page table configuration */
	vtcr = ARM_SMMU_VTCR_RES1 |
	       FIELD_PREP(ARM_SMMU_VTCR_PS, pt_cfg->arm_lpae_s2_cfg.vtcr.ps) |
	       FIELD_PREP(ARM_SMMU_VTCR_TG0, pt_cfg->arm_lpae_s2_cfg.vtcr.tg) |
	       FIELD_PREP(ARM_SMMU_VTCR_SH0, pt_cfg->arm_lpae_s2_cfg.vtcr.sh) |
	       FIELD_PREP(ARM_SMMU_VTCR_ORGN0, pt_cfg->arm_lpae_s2_cfg.vtcr.orgn) |
	       FIELD_PREP(ARM_SMMU_VTCR_IRGN0, pt_cfg->arm_lpae_s2_cfg.vtcr.irgn) |
	       FIELD_PREP(ARM_SMMU_VTCR_SL0, pt_cfg->arm_lpae_s2_cfg.vtcr.sl) |
	       FIELD_PREP(ARM_SMMU_VTCR_T0SZ, pt_cfg->arm_lpae_s2_cfg.vtcr.tsz);

	for_each_smmu(smmu) {
		u64 page_pa;
		u32 sctlr_val;
		int ret;

		page_pa = (u64)arm_smmu_page_pa(smmu, smmu->host_s2_cb_idx);

		/* Donate the S2 context bank page to hypervisor */
		ret = ___pkvm_host_donate_hyp(page_pa >> PAGE_SHIFT, 1, true);
		if (ret) {
			smmu_v2_debug_print("Failed to donate CB page: %d\n",
					    ret);
			return ret;
		}

		/* Configure the stage-2 translation registers */
		arm_smmu_cb_writeq(smmu, smmu->host_s2_cb_idx,
				   ARM_SMMU_CB_TTBR0, vttbr);
		arm_smmu_cb_write(smmu, smmu->host_s2_cb_idx,
				  ARM_SMMU_CB_TCR, vtcr);

		/* Configure CBA2R - set VA64 bit for 64-bit virtual addressing */
		u32 cba2r_val = arm_smmu_gr1_read(smmu, ARM_SMMU_GR1_CBA2R(smmu->host_s2_cb_idx));

		cba2r_val |= ARM_SMMU_CBA2R_VA64;
		arm_smmu_gr1_write(smmu, ARM_SMMU_GR1_CBA2R(smmu->host_s2_cb_idx), cba2r_val);

		/* Configure SCTLR for the host S2 context bank (0xCF0061) */
		sctlr_val = ARM_SMMU_SCTLR_M |
			    ARM_SMMU_SCTLR_CFRE |
			    ARM_SMMU_SCTLR_CFIE |
			    (0xF << 16) |   /* Bits 16-19 */
			    (0x3 << 22);    /* Bits 22-23 */
		arm_smmu_cb_write(smmu, smmu->host_s2_cb_idx,
				  ARM_SMMU_CB_SCTLR, sctlr_val);

		/* Configure ACTLR - set CPRE and CMTLB bits */
		arm_smmu_cb_write(smmu, smmu->host_s2_cb_idx,
				  ARM_SMMU_CB_ACTLR, 0x3);

		/* Configure CONTEXTIDR */
		arm_smmu_cb_write(smmu, smmu->host_s2_cb_idx,
				  ARM_SMMU_CB_CONTEXTIDR, 0x0);

		smmu_v2_debug_print("Configured S2, CB[%d] VTCR=0x%x, VTTBR=0x%llx, CBA2R=0x%x\n",
				    smmu->host_s2_cb_idx, vtcr, vttbr, cba2r_val);
		smmu_v2_debug_print("Configured S2, CB[%d] SCTLR=0x%x, ACTLR=0x3, CONTEXTIDR=0x0\n",
				    smmu->host_s2_cb_idx, sctlr_val);
	}

	return 0;
}

static int reserve_host_s2_context_bank(struct smmu_v2_nested *smmu)
{
	u32 cbar_val;
	u32 cb_irq;
	u64 cb_base_pa;

	/* S2 Default to last available CB */
	smmu->host_s2_cb_idx = smmu->num_cb - 1;
	smmu->num_cbar--; /* Reduce available CBARs for host */
	smmu->num_cb = smmu->num_cbar; /* Reduce available CBs for host */

	/* Calculate IRQ number for this context bank */
	cb_irq = smmu->irq_s2_cb + smmu->host_s2_cb_idx;
	smmu->irq_s2_cb = cb_irq;

	/* Calculate physical address of the context bank */
	cb_base_pa = (u64)arm_smmu_page_pa(smmu, smmu->host_s2_cb_idx);

	/* Read current CBAR value from hardware */
	cbar_val = arm_smmu_gr1_read(smmu, ARM_SMMU_GR1_CBAR(smmu->host_s2_cb_idx));
	smmu_v2_debug_print("Reserving CB[%d], original CBAR: 0x%x\n",
			    smmu->host_s2_cb_idx, cbar_val);

	/* Clear TYPE, VMID, and IRPTNDX fields, then set new values */
	cbar_val &= ~(ARM_SMMU_CBAR_TYPE | ARM_SMMU_CBAR_VMID | ARM_SMMU_CBAR_IRPTNDX);
	cbar_val |= FIELD_PREP(ARM_SMMU_CBAR_TYPE, CBAR_TYPE_S2_TRANS) |
		    FIELD_PREP(ARM_SMMU_CBAR_VMID, HOST_S2_VMID) |
		    FIELD_PREP(ARM_SMMU_CBAR_IRPTNDX, cb_irq);
	smmu_v2_debug_print("Modified CBAR for host S2: 0x%x\n", cbar_val);

	/* Write back the modified CBAR value */
	arm_smmu_gr1_write(smmu, ARM_SMMU_GR1_CBAR(smmu->host_s2_cb_idx), cbar_val);

	smmu_v2_debug_print("Reserved CB[%d] for host S2: CBAR: 0x%x Total CB: %d\n",
			    smmu->host_s2_cb_idx, cbar_val, smmu->num_cb);
	return 0;
}

static int hw_profile_init(void)
{
	struct smmu_v2_nested *smmu;
	int ret;

	for_each_smmu(smmu) {
		u32 id0 = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_ID0);
		u32 id1 = arm_smmu_gr0_read(smmu, ARM_SMMU_GR0_ID1);

		smmu->num_smr = id0 & ARM_SMMU_ID0_NUMSMRG;
		smmu->num_s2cr = smmu->num_smr; /* smr == s2cr */
		smmu->num_cb = id1 & ARM_SMMU_ID1_NUMCB;
		smmu->num_cbar = smmu->num_cb; /* cbar == cb */
		/* ID1 */
		smmu->pgshift = (id1 & ARM_SMMU_ID1_PAGESIZE) ? 16 : 12;
		smmu->numpage = 1 << (FIELD_GET(ARM_SMMU_ID1_NUMPAGENDXB, id1) + 1);

		smmu_v2_debug_print("SMMU ID0: %llx, ID1: %llx, num_smr: %d\n",
				    id0, id1, smmu->num_smr);
		smmu_v2_debug_print("SMMU num_cb: %d, pgshift: %d, numpage: %d\n",
				    smmu->num_cb, smmu->pgshift, smmu->numpage);

		ret = update_s2cr_profile(smmu);
		if (ret) {
			smmu_v2_debug_print("Failed to update S2CR profile!\n");
			return ret;
		}

		ret = update_cbar_profile(smmu);
		if (ret) {
			smmu_v2_debug_print("Failed to update CBAR profile!\n");
			return ret;
		}

		smmu_v2_debug_print("num_smr: %d, num_cb: %d\n",
				    smmu->num_smr, smmu->num_cb);

		/* Reserve the bottom available context bank for host S2 */
		ret = reserve_host_s2_context_bank(smmu);
		if (ret) {
			smmu_v2_debug_print("Failed to reserve host S2 context bank: %d\n", ret);
			return ret;
		}

		smmu_v2_debug_print("After reservation, available guests' CBs: %d & CBARs: %d\n",
				    smmu->num_cb, smmu->num_cbar);
	}

	return 0;
}

const struct smmu_vendor_callbacks v2callbacks = {
	.tlb_ops = NULL, /* fix me */
	.get_cfg = NULL, /* fix me */
	.post_init = smmu_attach_stage_2,
};

static struct smmu_vendor_driver smmuv2_driver = {
	.name = "smmuv2_nesting",
	.callbacks = &v2callbacks,
	.vdata = &data,
};

int smmuv2_hyp_nesting_init(void)
{
	int smmu_arr_size = PAGE_ALIGN(sizeof(*smmu_v2_nested_base) * smmu_v2_nested_count);
	int ret;

	smmu_v2_nested_base = kern_hyp_va(smmu_v2_nested_base);

	u32 page_count = smmu_arr_size >> PAGE_SHIFT;

	for (int i = 0; i < page_count; i++) {
		ret = __pkvm_host_share_hyp((hyp_virt_to_phys(smmu_v2_nested_base) >> PAGE_SHIFT) +
					    i);
		if (ret)
			return ret;
	}

	ret = take_over_smmus();
	if (ret)
		return ret;

	ret = hw_profile_init();
	if (ret)
		return ret;

	/* Register this driver with the common vendor module */
	ret = smmu_vendor_register_driver(&smmuv2_driver);

	data.vops->register_host_dabt_fault_handler(smmuv2_nesting_dabt_handler);

	return ret;
}
