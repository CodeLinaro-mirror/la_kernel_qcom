// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/debugfs.h>
#include <linux/dma-direct.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/seq_file.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include "qcom-iommu-debug.h"

/**
 * test_iommu_bypass_vms_loop - Test secure memory allocation for a specific VMID and size
 * @s: sequence file for output
 * @ddev: debug device
 * @vmid: secure memory VMID to test
 * @size: size of memory to allocate
 *
 * Returns 0 on success, negative error code on failure
 */
static int test_iommu_bypass_vms_loop(struct seq_file *s,
				      struct iommu_debug_device *ddev,
				      u32 vmid,
				      size_t size)
{
	struct qcom_scm_vmperm vmperm_set[] = {
		{.vmid = vmid, .perm = QCOM_SCM_PERM_READ | QCOM_SCM_PERM_WRITE},
	};
	struct qcom_scm_vmperm vmperm_unset[] = {
		{.vmid = QCOM_SCM_VMID_HLOS, .perm = QCOM_SCM_PERM_READ | QCOM_SCM_PERM_WRITE
		| QCOM_SCM_PERM_EXEC},
	};
	u64 srcvm_set = BIT(QCOM_SCM_VMID_HLOS);
	u64 srcvm_unset = BIT(vmid);
	dma_addr_t dma_handle;
	void *cpu_addr;
	phys_addr_t phys;
	int ret;
	const char *vmid_name;
	const char *size_str;

	/* Get human-readable names */
	switch (vmid) {
	case QCOM_SCM_VMID_ADSP_HEAP:
		vmid_name = "ADSP_HEAP";
		break;
	case QCOM_SCM_VMID_LPASS:
		vmid_name = "LPASS";
		break;
	default:
		vmid_name = "UNKNOWN";
		break;
	}

	switch (size) {
	case SZ_8K:
		size_str = "8K";
		break;
	case SZ_64K:
		size_str = "64K";
		break;
	case SZ_2M:
		size_str = "2M";
		break;
	default:
		size_str = "UNKNOWN";
		break;
	}

	seq_printf(s, "Testing %s allocation for VMID %s (%u): ", size_str, vmid_name, vmid);
	pr_info("Testing %s allocation for VMID %s (%u)\n", size_str, vmid_name, vmid);

	/* Allocate contiguous memory using DMA API */
	cpu_addr = dma_alloc_attrs(ddev->self, size, &dma_handle, GFP_KERNEL, 0);
	if (!cpu_addr) {
		seq_puts(s, "FAILED - dma_alloc_attrs failed\n");
		return -ENOMEM;
	}

	phys = dma_to_phys(ddev->self, dma_handle);
	/* Make memory secure */
	ret = qcom_scm_assign_mem(phys, size, &srcvm_set, vmperm_set, 1);
	if (ret) {
		seq_printf(s, "FAILED - qcom_scm_assign_mem to secure failed: %d\n", ret);
		dma_free_attrs(ddev->self, size, cpu_addr, dma_handle, 0);
		return ret;
	}

	/* Return memory to HLOS */
	ret = qcom_scm_assign_mem(phys, size, &srcvm_unset, vmperm_unset, 1);
	if (ret) {
		seq_printf(s, "FAILED - qcom_scm_assign_mem to HLOS failed: %d\n", ret);
		/* Intentional leak on failure */
		return ret;
	}

	/* Free the memory */
	dma_free_attrs(ddev->self, size, cpu_addr, dma_handle, 0);
	seq_puts(s, "SUCCESS\n");
	return 0;
}

/* Suspect this vmids require 32 bit addresses */
static int test_iommu_bypass_vms(struct seq_file *s, struct iommu_debug_device *ddev,
				u32 *vmids, u32 nr)
{
	size_t sizes[] = {SZ_8K, SZ_64K, SZ_2M};
	int i, j;
	int ret = 0;
	int total_tests = 0;
	int passed_tests = 0;

	seq_puts(s, "Iommu Bypass Secure Memory Test\n");
	seq_puts(s, "========================================\n\n");

	for (i = 0; i < nr; i++) {
		u32 vmid = vmids[i];

		for (j = 0; j < ARRAY_SIZE(sizes); j++) {
			size_t size = sizes[j];

			total_tests++;

			ret = test_iommu_bypass_vms_loop(s, ddev, vmid, size);
			if (!ret)
				passed_tests++;
		}
		seq_puts(s, "\n");
	}

	seq_printf(s, "Test completed - %d/%d tests passed\n", passed_tests, total_tests);

	return total_tests == passed_tests ? 0 : -EINVAL;
}

/* Individual test show functions */
static int iommu_bypass_vms_show(struct seq_file *s, void *ignored)
{
	struct iommu_debug_device *ddev = s->private;
	int ret;
	u32 vmids[] = {QCOM_SCM_VMID_ADSP_HEAP, QCOM_SCM_VMID_LPASS};

	ret = test_iommu_bypass_vms(s, ddev, vmids, ARRAY_SIZE(vmids));
	seq_printf(s, "\nTest result: %s\n", ret == 0 ? "PASSED" : "FAILED");
	return 0;
}
/* Individual test file operations */
static int iommu_bypass_vms_open(struct inode *inode, struct file *file)
{
	return single_open(file, iommu_bypass_vms_show, inode->i_private);
}

static const struct file_operations iommu_bypass_vms_fops = {
	.open	 = iommu_bypass_vms_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = single_release,
};

void iommu_debug_debugfs_setup_dpd(struct iommu_debug_device *ddev)
{
	struct dentry *dpd_proxy2_dir;

	/* Create dpd-proxy2 directory */
	dpd_proxy2_dir = debugfs_create_dir("dpd-proxy2", ddev->root_dir);
	if (IS_ERR_OR_NULL(dpd_proxy2_dir))
		return;

	/* Create individual test files within the directory */
	debugfs_create_file("iommu_bypass_vms", 0400, dpd_proxy2_dir, ddev, &iommu_bypass_vms_fops);
}
