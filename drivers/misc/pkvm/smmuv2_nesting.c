// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <asm/kvm_mmu.h>
#include <asm/kvm_pkvm.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include "smmuv2_nesting.h"
#include "arm-smmuv2-defs.h"

extern unsigned long kvm_nvhe_sym(smmu_v2_nested_count);
#define smmu_v2_nested_count kvm_nvhe_sym(smmu_v2_nested_count)
extern struct smmu_v2_nested *kvm_nvhe_sym(smmu_v2_nested_base);
#define smmu_v2_nested_base kvm_nvhe_sym(smmu_v2_nested_base)

extern struct kvm_iommu_ops kvm_nvhe_sym(smmuv2_hyp_nesting_ops);

int smmuv2_describe_smmuv2(void)
{
	struct device_node *np;
	struct resource res;
	int total_smmus = 0;
	int ret;
	int i;
	u32 irq;
	static const char * const compatible_devices[] = {
		"qcom,glymur-smmu-500",
		"qcom,adreno-smmu"
	};

	/* Pre-allocate memory for the maximum number of SMMUs we'll handle */
	smmu_v2_nested_base =
		(struct smmu_v2_nested *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
							  get_order(sizeof(struct smmu_v2_nested) *
							  ARRAY_SIZE(compatible_devices)));

	if (!smmu_v2_nested_base)
		return -ENOMEM;

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

