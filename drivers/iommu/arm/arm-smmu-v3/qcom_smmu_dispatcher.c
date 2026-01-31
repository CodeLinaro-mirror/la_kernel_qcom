// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries. */

#include <asm/arm-smmu-v3-common.h>
#include <asm/kvm_pkvm_module.h>
#include <asm/kvm_mmu.h>
#include <asm/kvm_pkvm.h>

#include <linux/atomic.h>
#include <linux/moduleparam.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include "smmuv2_nesting.h"

#ifdef MODULE
static unsigned long                   pkvm_module_token;

#define ksym_ref_addr_nvhe(x) \
	((typeof(kvm_nvhe_sym(x))*)(pkvm_el2_mod_va(&kvm_nvhe_sym(x), pkvm_module_token)))
#else
#define ksym_ref_addr_nvhe(x) \
	((typeof(kvm_nvhe_sym(x))*)(kern_hyp_va(lm_alias(&kvm_nvhe_sym(x)))))
#endif

int kvm_nvhe_sym(qcom_smmu_nesting_init_module)(const struct pkvm_module_ops *ops);
extern struct kvm_iommu_ops kvm_nvhe_sym(qcom_smmu_hyp_nesting_ops);

static int qcom_smmu_nesting_init(void)
{
	int ret = 0;
	struct kvm_hyp_memcache atomic_mc = {};
	int atomic_pages = 6000; /* arbitrary for now. */
	int nr_pages = 0;

	/*For L2 ptrs */;
	nr_pages += 50;
	ret = topup_hyp_memcache(&atomic_mc, 50, 2);
	if (ret)
		return ret;
	nr_pages += atomic_pages;
	ret = topup_hyp_memcache(&atomic_mc, atomic_pages, 0);
	if (ret)
		return ret;
	pr_info("smmu-dispactger: Allocated %d MiB for atomic usage\n",
		(nr_pages + (1 << 3)) >> 8);

	/* For io-pgtable struct*/
	__pkvm_topup_hyp_alloc(1);

	smmuv2_nesting_init();

#ifdef MODULE
	ret = pkvm_load_el2_module(kvm_nvhe_sym(qcom_smmu_nesting_init_module),
				   &pkvm_module_token);

	if (ret) {
		pr_err("Failed to load SMMUv3 IOMMU EL2 module: %d\n", ret);
		return ret;
	}
#endif
	pr_info("nr_pages %lu\n", atomic_mc.nr_pages);

	ret = kvm_iommu_init_hyp(ksym_ref_addr_nvhe(qcom_smmu_hyp_nesting_ops),
				 &atomic_mc);
	if (ret) {
		pr_err("Failed to init hyp iommu ops: %d\n", ret);
		return ret;
	}
	ret = smmuv2_post_boot_init();
	if (ret) {
		pr_err("Failed to initialize SMMUv2 post boot: %d\n", ret);
		return ret;
	}

	return ret;
}

static pkvm_handle_t qcom_smmu_get_iommu_id(struct device_node *np)
{
	return (pkvm_handle_t)0;
}

static void qcom_smmu_nesting_remove(void)
{
}

struct kvm_iommu_driver qcom_smmu_nesting_ops = {
	.init_driver = qcom_smmu_nesting_init,
	.remove_driver = qcom_smmu_nesting_remove,
	.get_iommu_id_by_of = qcom_smmu_get_iommu_id,
};

static int qcom_smmu_nesting_register(void)
{
	return kvm_iommu_register_driver(&qcom_smmu_nesting_ops);
}

/*
 * Register must be run before de-privliage before kvm_iommu_init_driver
 * for module case, it should be loaded using pKVM early loading which
 * loads it before this point.
 * For builtin drivers we use core_initcall
 */
subsys_initcall(qcom_smmu_nesting_register);
MODULE_LICENSE("GPL");
