// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <asm/kvm_mmu.h>
#include <asm/kvm_pkvm.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include "smmuv2_nesting.h"
#include "arm-smmuv2-defs.h"

extern unsigned long kvm_nvhe_sym(smmu_v2_nested_count);
#define smmu_v2_nested_count kvm_nvhe_sym(smmu_v2_nested_count)
extern struct smmu_v2_nested *kvm_nvhe_sym(smmu_v2_nested_base);
#define smmu_v2_nested_base kvm_nvhe_sym(smmu_v2_nested_base)
struct smmu_v2_nested *smmu_v2_host_nested_base; /* Host kernel's view of nested SMMU base */

extern struct kvm_iommu_ops kvm_nvhe_sym(smmuv2_hyp_nesting_ops);

static const char * const compatible_devices[] = {
		"qcom,qsmmu-v500"
	};

static irqreturn_t smmuv2_cb_fault_handler(int irq, void *dev)
{
	struct smmu_v2_nested *smmu = (struct smmu_v2_nested *)dev;
	u32 fsr, fsynr0, fsynr1;
	u64 far, ipafar;
	u32 sctlr, tcr;
	u64 ttbr0, ttbr1;
	void __iomem *cb_base;

	cb_base = (void __iomem *)smmu->host_cb_base;

	/* Read fault status register */
	fsr = readl_relaxed(cb_base + ARM_SMMU_CB_FSR);

	/* Read all fault-related registers from context bank */
	far = readq_relaxed(cb_base + ARM_SMMU_CB_FAR);
	ipafar = readq_relaxed(cb_base + ARM_SMMU_CB_IPAFAR);
	fsynr0 = readl_relaxed(cb_base + ARM_SMMU_CB_FSYNR0);
	fsynr1 = readl_relaxed(cb_base + ARM_SMMU_CB_FSYNR1);

	/* Read context bank configuration registers */
	sctlr = readl_relaxed(cb_base + ARM_SMMU_CB_SCTLR);
	tcr = readl_relaxed(cb_base + ARM_SMMU_CB_TCR);
	ttbr0 = readq_relaxed(cb_base + ARM_SMMU_CB_TTBR0);
	ttbr1 = readq_relaxed(cb_base + ARM_SMMU_CB_TTBR1);

	/* Print comprehensive fault information */
	pr_err("========================================\n");
	pr_err("SMMU Context Bank Fault Detected\n");
	pr_err("========================================\n");
	pr_err("SMMU Base Address: 0x%llx\n", smmu->base_pa);
	pr_err("Context Bank: %d\n", smmu->host_s2_cb_idx);
	pr_err("----------------------------------------\n");
	pr_err("Fault Information:\n");
	pr_err("  FSR:        0x%08x\n", fsr);
	pr_err("  FAR:        0x%016llx\n", far);
	pr_err("  IPAFAR:     0x%016llx\n", ipafar);
	pr_err("  FSYNR0:     0x%08x\n", fsynr0);
	pr_err("  FSYNR1:     0x%08x\n", fsynr1);
	pr_err("----------------------------------------\n");
	pr_err("Context Bank Configuration:\n");
	pr_err("  SCTLR:      0x%08x\n", sctlr);
	pr_err("  TCR:        0x%08x\n", tcr);
	pr_err("  TTBR0:      0x%016llx\n", ttbr0);
	pr_err("  TTBR1:      0x%016llx\n", ttbr1);
	pr_err("========================================\n");

	/* Clear the fault */
	writel_relaxed(fsr, cb_base + ARM_SMMU_CB_FSR);

	return IRQ_HANDLED;
}

int smmuv2_post_boot_init(void)
{
	struct device_node *np;
	struct platform_device *pdev;
	int i, ret;
	int virq;
	u32 cb_irq;

	if (!smmu_v2_host_nested_base || smmu_v2_nested_count == 0) {
		pr_err("No SMMU info available from hypervisor\n");
		return -EINVAL;
	}

	/* Register IRQ handler for each SMMU's host S2 context bank */
	for (i = 0; i < smmu_v2_nested_count; i++) {
		struct smmu_v2_nested *smmu = &smmu_v2_host_nested_base[i];
		int j;
		bool found = false;
		u64 cb_base_pa;
		void __iomem *cb_base;
		u32 cb_size;

		/* Calculate CB size from SMMU page shift (4KB or 64KB) */
		cb_size = 1 << smmu->pgshift;

		/* Calculate CB IRQ for this SMMU */
		cb_irq = smmu->host_s2_cb_idx;

		/* Find the device node for this SMMU by physical address */
		for (j = 0; j < ARRAY_SIZE(compatible_devices); j++) {
			for_each_compatible_node(np, NULL, compatible_devices[j]) {
				struct resource res;

				if (of_address_to_resource(np, 0, &res))
					continue;

				if (res.start == smmu->base_pa) {
					/* Found matching SMMU */
					pdev = of_find_device_by_node(np);

					if (!pdev) {
						pr_err("Failed to find platform device for SMMU at 0x%llx\n",
						       smmu->base_pa);
						break;
					}

					/* Convert physical IRQ to Linux virtual IRQ */
					virq = platform_get_irq(pdev, cb_irq);
					if (virq < 0) {
						pr_err("Failed to get virtual IRQ for SMMU at 0x%llx, phys IRQ %d\n",
						       smmu->base_pa, cb_irq);
						platform_device_put(pdev);
						break;
					}

					/* Register the IRQ handler */
					ret = request_irq(virq, smmuv2_cb_fault_handler,
							  IRQF_SHARED, "smmuv2-cb-fault", smmu);
					if (ret) {
						pr_err("Failed to register IRQ handler for SMMU at 0x%llx, IRQ %d: %d\n",
						       smmu->base_pa, virq, ret);
						platform_device_put(pdev);
						break;
					}

					pr_info("Registered CB fault IRQ handler for SMMU at 0x%llx: phys_irq=%d, virt_irq=%d, CB=%d\n",
						smmu->base_pa, cb_irq,
						virq, smmu->host_s2_cb_idx);

					platform_device_put(pdev);
					found = true;
				}
			}
		}

		if (!found) {
			pr_warn("Could not find device node for SMMU at 0x%llx\n",
				smmu->base_pa);
		}

		/* Calculate CB base PA using SMMU page calculation */
		pr_info("SMMU CB Base PA calculated: 0x%llx\n", smmu->base_pa);
		cb_base_pa = smmu->base_pa + (smmu->host_s2_cb_idx << smmu->pgshift);

		pr_info("SMMU CB Base PA: 0x%llx, size: 0x%x\n", cb_base_pa, cb_size);
		/* ioremap the context bank to read fault registers */
		cb_base = ioremap(cb_base_pa, cb_size);
		if (!cb_base) {
			pr_err("SMMU CB Fault: Failed to ioremap CB at 0x%llx\n",
			       cb_base_pa);
			continue;
		}
		pr_info("SMMU CB Base mapped at 0x%llx\n", (u64)cb_base);

		/* Store the mapped CB base for fault handler use */
		smmu->host_cb_base = (u64)cb_base;
	}

	return 0;
}

int smmuv2_describe_smmuv2(void)
{
	struct device_node *np;
	struct resource res;
	int total_smmus = 0;
	int ret;
	int i;
	u32 irq;

	/* Pre-allocate memory for the maximum number of SMMUs we'll handle */
	smmu_v2_nested_base =
		(struct smmu_v2_nested *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
							  get_order(sizeof(struct smmu_v2_nested) *
							  ARRAY_SIZE(compatible_devices)));

	if (!smmu_v2_nested_base)
		return -ENOMEM;

	smmu_v2_host_nested_base = smmu_v2_nested_base;
	/* Process each compatible device */
	for (i = 0; i < ARRAY_SIZE(compatible_devices); i++) {
		for_each_compatible_node(np, NULL, compatible_devices[i]) {
			ret = of_address_to_resource(np, 0, &res);
			if (ret)
				continue;

			if (of_device_is_compatible(np, compatible_devices[i])) {
				struct irq_desc *desc;

				smmu_v2_nested_base[smmu_v2_nested_count].base_pa =
					res.start;
				smmu_v2_nested_base[smmu_v2_nested_count].size =
					ALIGN((res.end - res.start), PAGE_SIZE);

				pr_info("smmu_v2_nested_base[%lx].base_pa: %pS, size %x\n",
					smmu_v2_nested_count,
					(void *)smmu_v2_nested_base[smmu_v2_nested_count].base_pa,
					smmu_v2_nested_base[smmu_v2_nested_count].size);

				/* second one is cb fault IRQ to start with */
				irq = of_irq_get(np, 1);
				smmu_v2_nested_base[smmu_v2_nested_count].irq_s2_cb = irq;
				pr_info("smmu_v2_nested_base[%lx].irq_s2_cb: %d\n",
					smmu_v2_nested_count,
					smmu_v2_nested_base[smmu_v2_nested_count].irq_s2_cb);
				smmu_v2_nested_count++;
				total_smmus++;
				desc = irq_to_desc(irq);
				if (desc)
					pr_info("SMMU IRQ: %d\n", (int)desc->irq_data.hwirq);
			}
		}
	}

	pr_info("Total Num of SMMU will be used for nesting: %d\n",
		total_smmus);
	pr_info("smmu_v2_nested_base: %llx\n", (u64)smmu_v2_nested_base);

	return total_smmus;
}

int __maybe_unused smmuv2_nesting_init(void)
{
	int nr_smmus;
	int ret = -ENODEV;

	nr_smmus = smmuv2_describe_smmuv2();
	if (nr_smmus == 0)
		return 0;

	return ret;
}

MODULE_LICENSE("GPL");

