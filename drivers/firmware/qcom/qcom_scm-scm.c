// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/arm-smccc.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/firmware/qcom/qcom_scm.h>

#include "qcom_scm.h"

/**
 * qcom_scm_pas_supported() - Check if the peripheral authentication service is
 *			      available for the given peripheral
 * @peripheral:	peripheral id
 *
 * Returns true if PAS is supported for this peripheral, otherwise false.
 */
bool qcom_scm_pas_supported(u32 peripheral)
{
	int ret;
	struct device *scm_dev = NULL;

	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_PIL,
		.cmd = QCOM_SCM_PIL_PAS_IS_SUPPORTED,
		.arginfo = QCOM_SCM_ARGS(1),
		.args[0] = peripheral,
		.owner = ARM_SMCCC_OWNER_SIP,
	};
	struct qcom_scm_res res;

	scm_dev = qcom_scm_get_dev();
	if (!scm_dev)
		return false;

	if (!__qcom_scm_is_call_available(scm_dev, QCOM_SCM_SVC_PIL,
					  QCOM_SCM_PIL_PAS_IS_SUPPORTED))
		return false;

	ret = qcom_scm_call(scm_dev, &desc, &res);

	return ret ? false : !!res.result[0];
}
EXPORT_SYMBOL_GPL(qcom_scm_pas_supported);

/**
 * qcom_scm_pas_init_image() - Initialize peripheral authentication service
 *			       state machine for a given peripheral, using the
 *			       metadata
 * @peripheral: peripheral id
 * @metadata:	pointer to memory containing ELF header, program header table
 *		and optional blob of data used for authenticating the metadata
 *		and the rest of the firmware
 * @size:	size of the metadata
 * @ctx:	optional metadata context
 * @dev_32bit: if not NULL, memory need to be allocated from lower 4G
 *
 * Return: 0 on success.
 *
 * Upon successful return, the PAS metadata context (@ctx) will be used to
 * track the metadata allocation, this needs to be released by invoking
 * qcom_scm_pas_metadata_release() by the caller.
 */
int qcom_scm_pas_init_image(u32 peripheral, const void *metadata, size_t size,
			    struct qcom_scm_pas_metadata *ctx, struct device *dev_32bit)
{
	struct device *dma_dev = NULL, *scm_dev = NULL;
	dma_addr_t mdata_phys;
	void *mdata_buf;
	int ret;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_PIL,
		.cmd = QCOM_SCM_PIL_PAS_INIT_IMAGE,
		.arginfo = QCOM_SCM_ARGS(2, QCOM_SCM_VAL, QCOM_SCM_RW),
		.args[0] = peripheral,
		.owner = ARM_SMCCC_OWNER_SIP,
	};
	struct qcom_scm_res res;

	scm_dev = qcom_scm_get_dev();
	if (!scm_dev)
		return -ENODEV;
	dma_dev = scm_dev;

	/*
	 * Only use 32bit dma device for dma memory allocation but use
	 * Scm device for any scm calls.
	 */
	if (dev_32bit)
		dma_dev = dev_32bit;

	/*
	 * During the scm call memory protection will be enabled for the
	 * metadata blob, so make sure it's physically contiguous, 4K aligned
	 * and non-cachable to avoid XPU violations.
	 *
	 * For PIL calls the hypervisor creates SHM Bridges for the blob
	 * buffers on behalf of Linux so we must not do it ourselves hence
	 * not using the TZMem allocator here.
	 *
	 * If we pass a buffer that is already part of an SHM Bridge to this
	 * call, it will fail.
	 */
	mdata_buf = dma_alloc_coherent(dma_dev, size, &mdata_phys,
				       GFP_KERNEL);
	if (!mdata_buf)
		return -ENOMEM;
	memcpy(mdata_buf, metadata, size);

	ret = qcom_scm_clk_enable();
	if (ret)
		goto out;

	ret = qcom_scm_bw_enable();
	if (ret)
		goto disable_clk;

	desc.args[1] = mdata_phys;

	ret = qcom_scm_call(scm_dev, &desc, &res);
	qcom_scm_bw_disable();

disable_clk:
	qcom_scm_clk_disable();

out:
	if (ret < 0 || !ctx) {
		dma_free_coherent(dma_dev, size, mdata_buf, mdata_phys);
	} else if (ctx) {
		ctx->ptr = mdata_buf;
		ctx->phys = mdata_phys;
		ctx->size = size;
	}

	return ret ? : res.result[0];
}
EXPORT_SYMBOL_GPL(qcom_scm_pas_init_image);

/**
 * qcom_scm_pas_shutdown() - Shut down the remote processor
 * @peripheral: peripheral id
 *
 * Returns 0 on success.
 */
int qcom_scm_pas_shutdown(u32 peripheral)
{
	int ret;
	struct device *scm_dev = NULL;

	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_PIL,
		.cmd = QCOM_SCM_PIL_PAS_SHUTDOWN,
		.arginfo = QCOM_SCM_ARGS(1),
		.args[0] = peripheral,
		.owner = ARM_SMCCC_OWNER_SIP,
	};
	struct qcom_scm_res res;

	scm_dev = qcom_scm_get_dev();
	if (!scm_dev)
		return -ENODEV;

	ret = qcom_scm_clk_enable();
	if (ret)
		return ret;

	ret = qcom_scm_bw_enable();
	if (ret)
		goto disable_clk;

	ret = qcom_scm_call(scm_dev, &desc, &res);
	qcom_scm_bw_disable();

disable_clk:
	qcom_scm_clk_disable();

	return ret ? : res.result[0];
}
EXPORT_SYMBOL_GPL(qcom_scm_pas_shutdown);

/**
 * qcom_scm_pas_shutdown_retry() - Shut down the remote processor by retrying
 * @peripheral: peripheral id
 *
 * Returns 0 on success.
 */
int qcom_scm_pas_shutdown_retry(u32 peripheral)
{
	unsigned int pas_shutdown_retry_delay_ms = 0;
	int ret;

	ret = qcom_scm_pas_shutdown(peripheral);
	/* No need to retry if the first try worked */
	if (!ret)
		return ret;

	pas_shutdown_retry_delay_ms = qcom_scm_pas_get_shutdown_retry_delay_ms();
	if (pas_shutdown_retry_delay_ms > 0) {
		pr_err("PAS Shutdown: First call to shutdown failed with error: %d\n", ret);
		pr_err("PAS Shutdown: Sleeping for: %u\n", pas_shutdown_retry_delay_ms);
		msleep(pas_shutdown_retry_delay_ms);

		pr_err("PAS Shutdown: Attempting to shutdown peripheral again\n");
		return qcom_scm_pas_shutdown(peripheral);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(qcom_scm_pas_shutdown_retry);

/**
 * qcom_scm_pas_mem_setup() - Prepare the memory related to a given peripheral
 *			      for firmware loading
 * @peripheral:	peripheral id
 * @addr:	start address of memory area to prepare
 * @size:	size of the memory area to prepare
 *
 * Returns 0 on success.
 */
int qcom_scm_pas_mem_setup(u32 peripheral, phys_addr_t addr, phys_addr_t size)
{
	int ret;
	struct device *scm_dev = NULL;

	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_PIL,
		.cmd = QCOM_SCM_PIL_PAS_MEM_SETUP,
		.arginfo = QCOM_SCM_ARGS(3),
		.args[0] = peripheral,
		.args[1] = addr,
		.args[2] = size,
		.owner = ARM_SMCCC_OWNER_SIP,
	};
	struct qcom_scm_res res;

	scm_dev = qcom_scm_get_dev();
	if (!scm_dev)
		return -ENODEV;

	ret = qcom_scm_clk_enable();
	if (ret)
		return ret;

	ret = qcom_scm_bw_enable();
	if (ret)
		goto disable_clk;

	ret = qcom_scm_call(scm_dev, &desc, &res);
	qcom_scm_bw_disable();

disable_clk:
	qcom_scm_clk_disable();

	return ret ? : res.result[0];
}
EXPORT_SYMBOL_GPL(qcom_scm_pas_mem_setup);

/**
 * qcom_scm_pas_auth_and_reset() - Authenticate the given peripheral firmware
 *				   and reset the remote processor
 * @peripheral:	peripheral id
 *
 * Return 0 on success.
 */
int qcom_scm_pas_auth_and_reset(u32 peripheral)
{
	int ret;
	struct device *scm_dev = NULL;

	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_PIL,
		.cmd = QCOM_SCM_PIL_PAS_AUTH_AND_RESET,
		.arginfo = QCOM_SCM_ARGS(1),
		.args[0] = peripheral,
		.owner = ARM_SMCCC_OWNER_SIP,
	};
	struct qcom_scm_res res;

	scm_dev = qcom_scm_get_dev();
	if (!scm_dev)
		return -ENODEV;

	ret = qcom_scm_clk_enable();
	if (ret)
		return ret;

	ret = qcom_scm_bw_enable();
	if (ret)
		goto disable_clk;

	ret = qcom_scm_call(scm_dev, &desc, &res);
	qcom_scm_bw_disable();

disable_clk:
	qcom_scm_clk_disable();

	return ret ? : res.result[0];
}
EXPORT_SYMBOL_GPL(qcom_scm_pas_auth_and_reset);

/**
 * Placeholder API
 * Currently no-op, safe to call
 */
void qcom_scm_pas_store_memoryinfo(u32 peripheral, phys_addr_t addr,
		phys_addr_t size)
{
	/* Nothing to do for legacy SCM */
}
EXPORT_SYMBOL_GPL(qcom_scm_pas_store_memoryinfo);

/**
 * qcom_scm_assign_dump_table_region() - Assign a memory region to the
 *                                       dump table
 * @is_assign:  1 = assign, 0 = unassign
 * @addr:       start address of memory region
 * @size:       size of the memory region
 *
 * Returns 0 on success.
 */
int qcom_scm_assign_dump_table_region(bool is_assign, phys_addr_t addr, size_t size)
{
	struct device *scm_dev = NULL;
	struct qcom_scm_desc desc = {
		.svc = QCOM_SCM_SVC_UTIL,
		.cmd = QCOM_SCM_UTIL_DUMP_TABLE_ASSIGN,
		.arginfo = QCOM_SCM_ARGS(3),
		.owner = ARM_SMCCC_OWNER_SIP,
		.args[0] = is_assign,
		.args[1] = addr,
		.args[2] = size,
	};

	scm_dev = qcom_scm_get_dev();
	if (!scm_dev)
		return -ENODEV;

	return qcom_scm_call(scm_dev, &desc, NULL);
}
EXPORT_SYMBOL_GPL(qcom_scm_assign_dump_table_region);
