// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#define pr_fmt(fmt) "%s: " fmt,  __func__

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/maple_tree.h>
#include <linux/of.h>
#include <linux/qtee_shmbridge.h>
#include <linux/firmware/qcom/si_object.h>
#include <linux/iommu.h>
#include <linux/qcom_dpd_proxy.h>
#include <linux/pci.h>
#include "drivers/iommu/dma-iommu.h"

#define MSI_IOVA_BASE                   0x8000000
#define MSI_IOVA_LENGTH                 0x100000

struct dpd_smmu {
	struct device *dev;
	struct iommu_device iommu;
	struct mutex streams_lock;
	struct xarray streams;
	struct iommu_domain_geometry geometry;
	u64 pgsize_bitmap;
	struct si_object *service;
	struct si_object *env;
};

struct dpd_smmu_domain {
	struct dpd_smmu *smmu;

	/* Protected by mappings_lock */
	struct mutex mappings_lock;
	struct maple_tree mappings;

	bool attached;
	struct device *dev;
	/* Sharing domains across multiple devices is not supported */
	u32 si_domain_id;
	struct iommu_domain domain;
};

/*
 * Used primarily for iommu_iova_to_phys.
 * Also for attaching/detaching domains.
 */
struct dpd_mapping {
	struct dpd_scatterlist *dpd_sg;
	unsigned long iova;
	u32 prot;
};

#define to_smmu_domain(d) container_of((d), struct dpd_smmu_domain, domain)

/* from arm-smmu-v3 */
static struct dpd_smmu_domain *
to_smmu_domain_devices(struct iommu_domain *domain)
{
	/* The domain can be NULL only when processing the first attach */
	if (!domain)
		return NULL;
	if (domain->type & __IOMMU_DOMAIN_PAGING)
		return to_smmu_domain(domain);
	return NULL;
}

static struct dpd_mapping __maybe_unused *add_mapping(struct dpd_smmu_domain *smmu_domain,
					struct dpd_scatterlist *dpd_sg,
					unsigned long iova, int prot)
{
	struct dpd_mapping *mapping;
	int ret;

	mapping = kzalloc(sizeof(*mapping), GFP_KERNEL);
	if (!mapping)
		return NULL;

	get_si_object(dpd_sg->shm);
	mapping->dpd_sg = dpd_sg;
	mapping->iova = iova;
	mapping->prot = prot;

	mutex_lock(&smmu_domain->mappings_lock);
	ret = mtree_insert_range(&smmu_domain->mappings, iova,
			iova + dpd_sg->size - 1, mapping, GFP_KERNEL);
	mutex_unlock(&smmu_domain->mappings_lock);
	if (ret) {
		dev_err(smmu_domain->smmu->dev, "Domain %u: mtree_insert_range failed: %d. start=%lx, end=%zx\n",
			smmu_domain->si_domain_id, ret,
			iova, iova + dpd_sg->size);
		put_si_object(dpd_sg->shm);
		kfree(mapping);
		return NULL;
	}

	return mapping;
}

static void __free_dpd_mapping(struct dpd_mapping *mapping)
{
	put_si_object(mapping->dpd_sg->shm);
	kfree(mapping);
}

/* Caller must hold mappings lock */
static int detach_mappings(struct dpd_smmu_domain *smmu_domain, unsigned long iova_end)
{
	MA_STATE(mas, &smmu_domain->mappings, 0, 0);
	struct dpd_mapping *mapping;
	int ret = 0;

	if (!smmu_domain->attached)
		return 0;

	mas_for_each(&mas, mapping, iova_end) {
		ret = dpd_svc_unmap(mapping->dpd_sg, smmu_domain->si_domain_id,
				mapping->iova);
		if (ret) {
			dev_warn(smmu_domain->smmu->dev, "%s: %s failed\n",
				 dev_name(smmu_domain->dev), __func__);
			return ret;
		}
	}

	smmu_domain->attached = false;
	return 0;
}

static u32 prot_to_svc_flags(u32 prot)
{
	u32 flags = 0;

	if (prot & IOMMU_READ)
		flags |= IMM_F_READ;
	if (prot & IOMMU_WRITE)
		flags |= IMM_F_WRITE;

	return flags;
}

/* Caller must hold mappings lock */
static int attach_mappings(struct dpd_smmu_domain *smmu_domain)
{
	MA_STATE(mas, &smmu_domain->mappings, 0, 0);
	struct dpd_mapping *mapping;
	int ret = 0;

	if (smmu_domain->attached)
		return 0;

	/* Set early for detach_mappings error case */
	smmu_domain->attached = true;
	mas_for_each(&mas, mapping, ULONG_MAX) {
		ret = dpd_svc_map(mapping->dpd_sg, smmu_domain->si_domain_id,
				  prot_to_svc_flags(mapping->prot),
				  mapping->iova);
		if (ret) {
			detach_mappings(smmu_domain, mapping->iova - 1);
			return ret;
		}
	}

	return 0;
}

static struct iommu_domain *dpd_smmu_domain_alloc_paging(struct device *dev)
{
	struct dpd_smmu_domain *smmu_domain;
	struct dpd_smmu *smmu = container_of(dev->iommu->iommu_dev, struct dpd_smmu, iommu);
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);

	smmu_domain = kzalloc(sizeof(*smmu_domain), GFP_KERNEL);
	if (!smmu_domain)
		return ERR_PTR(-ENOMEM);

	mutex_init(&smmu_domain->mappings_lock);
	mt_init(&smmu_domain->mappings);
	smmu_domain->smmu = smmu;
	smmu_domain->dev = dev;
	smmu_domain->si_domain_id = fwspec->ids[0];
	smmu_domain->domain.pgsize_bitmap = smmu->pgsize_bitmap;
	smmu_domain->domain.geometry = smmu->geometry;

	return &smmu_domain->domain;
}

static void dpd_smmu_domain_free_paging(struct iommu_domain *domain)
{
	struct dpd_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct dpd_mapping *mapping;

	MA_STATE(mas, &smmu_domain->mappings, 0, 0);

	mutex_lock(&smmu_domain->mappings_lock);
	if (smmu_domain->attached)
		detach_mappings(smmu_domain, ULONG_MAX);

	mas_for_each(&mas, mapping, ULONG_MAX) {
		dev_dbg(smmu_domain->dev, "Cleanup mappings @ %lx\n", mas.index);
		__free_dpd_mapping(mapping);
	}
	mtree_destroy(&smmu_domain->mappings);
	mutex_unlock(&smmu_domain->mappings_lock);

	kfree(smmu_domain);
}

static struct dpd_smmu *dpd_smmu_get_by_fwnode(struct fwnode_handle *fwnode)
{
	struct device *dev = bus_find_device_by_fwnode(&platform_bus_type, fwnode);

	put_device(dev);
	return dev ? dev_get_drvdata(dev) : NULL;
}

static struct iommu_device *dpd_smmu_probe_device(struct device *dev)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct dpd_smmu *smmu;
	int ret;

	smmu = dpd_smmu_get_by_fwnode(fwspec->iommu_fwnode);
	if (!smmu)
		return ERR_PTR(-ENODEV);

	if (fwspec->num_ids > 1) {
		dev_err(dev, "Max one iommu-id\n");
		return ERR_PTR(-EINVAL);
	}

	mutex_lock(&smmu->streams_lock);
	ret = xa_insert(&smmu->streams, fwspec->ids[0], dev, GFP_KERNEL);
	mutex_unlock(&smmu->streams_lock);
	if (ret) {
		dev_err(dev, "ID %d is already registered\n", fwspec->ids[0]);
		return ERR_PTR(ret);
	}

	return &smmu->iommu;
}

static void dpd_smmu_release_device(struct device *dev)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct dpd_smmu *smmu = container_of(dev->iommu->iommu_dev, struct dpd_smmu, iommu);
	int i;

	mutex_lock(&smmu->streams_lock);
	for (i = 0; i < fwspec->num_ids; i++)
		xa_erase(&smmu->streams, fwspec->ids[i]);
	mutex_unlock(&smmu->streams_lock);
}

static struct iommu_group *dpd_smmu_device_group(struct device *dev)
{
	struct iommu_group *group;

	if (dev_is_pci(dev))
		group = pci_device_group(dev);
	else
		group = generic_device_group(dev);

	return group;
}

static int dpd_smmu_of_xlate(struct device *dev, const struct of_phandle_args *args)
{
	return iommu_fwspec_add_ids(dev, args->args, 1);
}

static void dpd_smmu_get_resv_regions(struct device *dev,
			       struct list_head *head)
{
	struct iommu_resv_region *region;
	int prot = IOMMU_WRITE | IOMMU_NOEXEC | IOMMU_MMIO;

	region = iommu_alloc_resv_region(MSI_IOVA_BASE, MSI_IOVA_LENGTH,
					 prot, IOMMU_RESV_SW_MSI, GFP_KERNEL);
	if (!region)
		return;

	list_add_tail(&region->list, head);

	iommu_dma_get_resv_regions(dev, head);
}

static int dpd_smmu_attach_dev(struct iommu_domain *domain, struct device *dev)
{
	struct dpd_smmu_domain *prev_domain =
		to_smmu_domain_devices(iommu_get_domain_for_dev(dev));
	struct dpd_smmu_domain *smmu_domain = to_smmu_domain(domain);
	int ret;

	if (prev_domain) {
		mutex_lock(&prev_domain->mappings_lock);
		ret = detach_mappings(prev_domain, ULONG_MAX);
		mutex_unlock(&prev_domain->mappings_lock);
		if (ret)
			return ret;
	}

	mutex_lock(&smmu_domain->mappings_lock);
	ret = attach_mappings(smmu_domain);
	mutex_unlock(&smmu_domain->mappings_lock);
	return ret;
}

static int dpd_smmu_map_pages(struct iommu_domain *domain, unsigned long iova,
			      phys_addr_t paddr, size_t pgsize, size_t pgcount,
			      int prot, gfp_t gfp, size_t *_mapped)
{
	return -EINVAL;
}

static size_t dpd_smmu_unmap_pages(struct iommu_domain *domain, unsigned long iova,
				   size_t pgsize, size_t pgcount,
				   struct iommu_iotlb_gather *gather)
{
	return 0;
}

static struct iommu_map_cookie_sg *
dpd_alloc_cookie_sg(unsigned long iova, int prot, unsigned int nents, gfp_t gfp)
{
	return NULL;
}

static int dpd_add_deferred_map_sg(struct iommu_map_cookie_sg *cookie,
				   phys_addr_t paddr, size_t pgsize, size_t pgcount)
{
	return -EINVAL;
}

static size_t dpd_consume_deferred_map_sg(struct iommu_map_cookie_sg *cookie)
{
	return 0;
}

static phys_addr_t
dpd_smmu_iova_to_phys(struct iommu_domain *domain, dma_addr_t iova)
{
	struct dpd_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct dpd_mapping *mapping;
	struct sg_page_iter piter;
	unsigned long pgoffset;
	phys_addr_t phys = 0;

	mutex_lock(&smmu_domain->mappings_lock);
	mapping = mtree_load(&smmu_domain->mappings, iova);
	if (!mapping)
		goto out;

	/*
	 * hyp_assign() requires the segments of its sg_table to be aligned to the
	 * iommu-granularity. Therefore, dma-iommu did not modify the offset/sizes
	 * of the sg_table before passing it to the iommu framework as it might do
	 * in the general case.
	 */
	pgoffset = (iova - mapping->iova) >> PAGE_SHIFT;
	__sg_page_iter_start(&piter, mapping->dpd_sg->sgt.sgl,
		mapping->dpd_sg->sgt.orig_nents, pgoffset);

	if (__sg_page_iter_next(&piter))
		phys = page_to_phys(sg_page_iter_page(&piter)) +
		       (iova & (PAGE_SIZE - 1));
out:
	mutex_unlock(&smmu_domain->mappings_lock);
	return phys;
}

static const struct iommu_ops dpd_smmu_ops = {
	.domain_alloc_paging    = dpd_smmu_domain_alloc_paging,
	.probe_device		= dpd_smmu_probe_device,
	.release_device		= dpd_smmu_release_device,
	.device_group		= dpd_smmu_device_group,
	.of_xlate		= dpd_smmu_of_xlate,
	.get_resv_regions	= dpd_smmu_get_resv_regions,
	.owner			= THIS_MODULE,
	.default_domain_ops = &(const struct iommu_domain_ops) {
		.attach_dev			= dpd_smmu_attach_dev,
		.map_pages			= dpd_smmu_map_pages,
		.unmap_pages			= dpd_smmu_unmap_pages,
		.alloc_cookie_sg		= dpd_alloc_cookie_sg,
		.add_deferred_map_sg		= dpd_add_deferred_map_sg,
		.consume_deferred_map_sg	= dpd_consume_deferred_map_sg,
		.iova_to_phys			= dpd_smmu_iova_to_phys,
		.free				= dpd_smmu_domain_free_paging,
	}
};

static int dpd_smmu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dpd_smmu *smmu;
	int ret;
	struct si_object_invoke_ctx oic;
	char *msg = "";

	smmu = devm_kzalloc(dev, sizeof(*smmu), GFP_KERNEL);
	if (!smmu)
		return dev_err_probe(dev, -ENOMEM, "Failed to allocate smmu structure");

	mutex_init(&smmu->streams_lock);
	xa_init(&smmu->streams);
	smmu->dev = dev;

	ret = si_core_get_client_env(&oic, &smmu->env);
	if (ret) {
		msg = "si_core_get_client_env failed";
		goto err_env;
	}

	ret = si_core_client_env_open(&oic, smmu->env, CSecureMemoryManager_UID, &smmu->service);
	if (ret) {
		msg = "si_core_client_env_open failed";
		goto err_service;
	}

	/* Hardcoded values. Ideally could query these from the TEE service */
	smmu->pgsize_bitmap = (1 << PAGE_SHIFT);
	smmu->geometry = (struct iommu_domain_geometry) {
		.aperture_start = 0,
		.aperture_end = 1ULL << 48,
		.force_aperture = true,
	};

	dev_set_drvdata(dev, smmu);

	ret = iommu_device_sysfs_add(&smmu->iommu, dev, NULL, "dpd_smmu");
	if (ret) {
		msg = "iommu_device_sysfs_add failed";
		goto err_sysfs;  /* Both service and env need cleanup */
	}

	ret = iommu_device_register(&smmu->iommu, &dpd_smmu_ops, dev);
	if (ret) {
		msg = "Failed to register iommu";
		goto err_register;  /* Cleanup sysfs, service, and env */
	}

	return 0;

err_register:
	iommu_device_sysfs_remove(&smmu->iommu);
err_sysfs:
	put_si_object(smmu->service);
err_service:
	put_si_object(smmu->env);
err_env:
	return dev_err_probe(dev, ret, "%s", msg);
}

static void dpd_smmu_remove(struct platform_device *pdev)
{
	struct dpd_smmu *smmu = platform_get_drvdata(pdev);

	iommu_device_unregister(&smmu->iommu);
	iommu_device_sysfs_remove(&smmu->iommu);

	put_si_object(smmu->service);
	put_si_object(smmu->env);
}

static const struct of_device_id dpd_smmu_of_match[] = {
	{ .compatible = "qcom,dpd-smmu" },
	{}
};

static struct platform_driver dpd_smmu_driver = {
	.driver	= {
		.name			= "dpd-smmu",
		.of_match_table		= dpd_smmu_of_match,
		.suppress_bind_attrs	= true,
	},
	.probe	= dpd_smmu_probe,
	.remove	= dpd_smmu_remove,
};

module_platform_driver(dpd_smmu_driver);
MODULE_DESCRIPTION("QTI DPD SMMU Driver");
MODULE_LICENSE("GPL");
