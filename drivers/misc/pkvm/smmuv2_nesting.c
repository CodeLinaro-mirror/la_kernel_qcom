// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <asm/kvm_mmu.h>
#include <asm/kvm_pkvm.h>

#include "smmuv2_nesting.h"

#ifdef MODULE
static unsigned long                   pkvm_module_token;

#define ksym_ref_addr_nvhe(x) \
	((typeof(kvm_nvhe_sym(x))*)(pkvm_el2_mod_va(&kvm_nvhe_sym(x), pkvm_module_token)))
#else
#define ksym_ref_addr_nvhe(x) \
	((typeof(kvm_nvhe_sym(x))*)(kern_hyp_va(lm_alias(&kvm_nvhe_sym(x)))))
#endif

int kvm_nvhe_sym(smmuv2_nesting_init_module)(const struct pkvm_module_ops *ops);
extern struct kvm_iommu_ops kvm_nvhe_sym(smmuv2_hyp_nesting_ops);

static int smmuv2_nesting_init(void)
{
	struct kvm_hyp_memcache atomic_mc = {};

#ifdef MODULE
	int ret;

	ret = pkvm_load_el2_module(kvm_nvhe_sym(smmuv2_nesting_init_module),
				   &pkvm_module_token);

	if (ret) {
		pr_err("Failed to load SMMUv3 IOMMU EL2 module: %d\n", ret);
		return ret;
	}
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
 * Register must be run before de-privliage before kvm_iommu_init_driver
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
