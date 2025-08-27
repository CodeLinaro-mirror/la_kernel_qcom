// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <asm/kvm_mmu.h>
#include <asm/kvm_pkvm.h>
#include <linux/of_address.h>
#include "smmuv2_nesting.h"
#include "arm-smmuv2-defs.h"

#ifdef MODULE
static unsigned long                   pkvm_module_token;

#define ksym_ref_addr_nvhe(x) \
	((typeof(kvm_nvhe_sym(x))*)pkvm_el2_mod_va(&kvm_nvhe_sym(x), \
						    pkvm_module_token))
#else
#define ksym_ref_addr_nvhe(x) \
	((typeof(kvm_nvhe_sym(x))*)kern_hyp_va(lm_alias(&kvm_nvhe_sym(x))))
#endif

int kvm_nvhe_sym(smmuv2_nesting_init_module)(const struct pkvm_module_ops *ops);
extern unsigned long kvm_nvhe_sym(smmu_v2_nested_count);
extern struct smmu_v2_nested *kvm_nvhe_sym(smmu_v2_nested_base);
#define smmu_v2_nested_base kvm_nvhe_sym(smmu_v2_nested_base)

extern struct kvm_iommu_ops kvm_nvhe_sym(smmuv2_hyp_nesting_ops);

int smmuv2_describe_smmuv2(void)
{
	struct device_node *np;
	struct resource res;
	int smmu_v2_nested_count = 0;
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

	kvm_nvhe_sym(smmu_v2_nested_count) = total_smmus;
	pr_info("Total Num of SMMU will be used for nesting: %d\n",
		total_smmus);
	pr_info("smmu_v2_nested_base: %llx\n", (u64)smmu_v2_nested_base);

	return total_smmus;
}

static int smmuv2_nesting_init(void)
{
	struct kvm_hyp_memcache atomic_mc = {};
	int nr_smmus;
	int ret;

	nr_smmus = smmuv2_describe_smmuv2();
	if (nr_smmus == 0)
		return 0;

#ifdef MODULE
	/* Load SMMU V2 el2 module */
	pr_info("Launching SMMU V2 el2 module\n");
	ret = pkvm_load_el2_module(kvm_nvhe_sym(smmuv2_nesting_init_module),
				   &pkvm_module_token);
	pr_info("SMMU V2 module load_address = 0x%lx\n", pkvm_module_token);
#endif

	return kvm_iommu_init_hyp(ksym_ref_addr_nvhe(smmuv2_hyp_nesting_ops),
				  &atomic_mc);
}

static pkvm_handle_t smmuv2_get_iommu_id(struct device_node *np)
{
	return (pkvm_handle_t)0;
}

static void smmuv2_nesting_remove(void)
{
}

struct kvm_iommu_driver smmuv2_nesting_ops = {
	.init_driver = smmuv2_nesting_init,
	.remove_driver = smmuv2_nesting_remove,
	.get_iommu_id_by_of = smmuv2_get_iommu_id,
};

static int smmuv2_nesting_register(void)
{
	return kvm_iommu_register_driver(&smmuv2_nesting_ops);
}

/*
 * Register must be run before de-privilege before kvm_iommu_init_driver
 * for module case, it should be loaded using pKVM early loading which
 * loads it before this point.
 * For builtin drivers we use core_initcall
 */
#ifdef MODULE
module_init(smmuv2_nesting_register);
#else
core_initcall(smmuv2_nesting_register);
#endif

MODULE_LICENSE("GPL");
