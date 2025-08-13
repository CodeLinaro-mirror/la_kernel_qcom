// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#define pr_fmt(fmt) "si-ffa: %s: " fmt, __func__

#include "si_core.h"

static struct ffa_device *qtee_ffa_dev;

int qtee_ffa_mem_share(struct sg_table *sgt, uint64_t tag, uint64_t *ffa_handle)
{
	int rc = 0;

	if (!qtee_ffa_dev)
		return -ENODEV;

	struct ffa_mem_region_attributes mem_attr = {
		.receiver = qtee_ffa_dev->vm_id,
		.attrs = FFA_MEM_RW,
		.flag = 0,
	};

	struct ffa_mem_ops_args mem_args = {
		.attrs = &mem_attr,
		.use_txbuf = true,
		.nattrs = 1,
		.flags = 0,
		.tag = tag,
		.sg = sgt->sgl,
	};

	rc = qtee_ffa_dev->ops->mem_ops->memory_share(&mem_args);
	if (rc) {
		pr_err("memory_share failed: %d\n", rc);
		return rc;
	}

	*ffa_handle = mem_args.g_handle;

	return 0;
}

int qtee_ffa_mem_lend(struct sg_table *sgt, uint64_t tag, uint64_t *ffa_handle)
{
	int rc = 0;

	if (!qtee_ffa_dev)
		return -ENODEV;

	struct ffa_mem_region_attributes mem_attr = {
		.receiver = qtee_ffa_dev->vm_id,
		.attrs = FFA_MEM_RW,
		.flag = 0,
	};

	struct ffa_mem_ops_args mem_args = {
		.attrs = &mem_attr,
		.use_txbuf = true,
		.nattrs = 1,
		.flags = 0,
		.tag = tag,
		.sg = sgt->sgl,
	};

	rc = qtee_ffa_dev->ops->mem_ops->memory_lend(&mem_args);
	if (rc) {
		pr_err("memory_lend failed: %d\n", rc);
		return rc;
	}

	*ffa_handle = mem_args.g_handle;

	return 0;
}

int qtee_ffa_mem_reclaim(uint64_t ffa_handle)
{
	int rc = 0;

	if (!qtee_ffa_dev)
		return -ENODEV;

	/* We assume that QTEE has already called MEM_RELINQUISH.
	 * And so, we do not need to send a DIRECT_REQ message first.
	 */
	rc = qtee_ffa_dev->ops->mem_ops->memory_reclaim(ffa_handle, 0);
	if (rc) {
		pr_err("mem_reclaim failed: 0x%llx %d\n", ffa_handle, rc);
		return rc;
	}

	return 0;
}

static int si_core_ffa_probe(struct ffa_device *ffa_dev)
{
	/* There is no possibility of QTEE SP going down and
	 * coming back online which could require re-binding
	 * to the driver. If probe is called multiple times,
	 * this is an error.
	 */
	if (qtee_ffa_dev)
		return -EEXIST;

	qtee_ffa_dev = ffa_dev;
	return 0;
}

static void si_core_ffa_remove(struct ffa_device *ffa_dev)
{
	qtee_ffa_dev = NULL;
}

static const struct ffa_device_id qtee_ffa_device_id[] = {
	{ QTEE_SP_FFA_UUID },
	{}
};

static struct ffa_driver si_core_ffa_driver = {
	.name = "si_core",
	.probe = si_core_ffa_probe,
	.remove = si_core_ffa_remove,
	.id_table = qtee_ffa_device_id,
};

int si_core_ffa_driver_register(void)
{
	return ffa_register(&si_core_ffa_driver);
}

void si_core_ffa_driver_unregister(void)
{
	ffa_unregister(&si_core_ffa_driver);
}
