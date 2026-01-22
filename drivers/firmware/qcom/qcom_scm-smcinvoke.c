// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/delay.h>
#include <linux/qtee_shmbridge.h>
#include <linux/firmware/qcom/qcom_scm.h>

#include "qcom_scm_smcinvoke.h"

bool qcom_scm_pas_supported(u32 peripheral)
{
	struct si_object *pil_image_service = NULL;
	int ret;

	ret = qcom_scm_pas_pil_service_init(peripheral, &pil_image_service);
	return ret ? false : true;
}
EXPORT_SYMBOL_GPL(qcom_scm_pas_supported);

int qcom_scm_pas_init_image(u32 peripheral, const void *metadata, size_t size,
		struct qcom_scm_pas_metadata *ctx, struct device *dev_32bit)
{
	struct si_object *pil_image_service = NULL;
	struct si_object *smo = NULL;
	int ret;

	ret = qcom_scm_pas_pil_service_init(peripheral, &pil_image_service);
	if (ret)
		return ret;

	/*
	 * Initialize shared object for metadata and its size
	 * If return value is 0, smo will not be NULL_SI_OBJECT
	 */
	ret = qcom_smci_pil_init_smobject(metadata, size, &smo, ctx, dev_32bit,
			SI_CORE_MEM_OBJ_SHARE | SI_CORE_MEM_OBJ_UNCACHED);
	if (ret) {
		pr_err("Failed to create shared object for metadata and its size (%d)\n", ret);
		return ret;
	}

	ret = qcom_smci_smo_call(pil_image_service, smo, SMCI_PILIMAGE_OP_VERIFYMETADATA);
	if (ret)
		pr_err("Failed to verify pil image metadata (%d)\n", ret);

	/* Release smo */
	put_si_object(smo);
	return ret;
}
EXPORT_SYMBOL_GPL(qcom_scm_pas_init_image);

int qcom_scm_pas_shutdown(u32 peripheral)
{
	struct si_object *pil_image_service = NULL;
	struct si_arg args[1] = { 0 };
	int ret, result;

	ret = qcom_scm_pas_pil_service_init(peripheral, &pil_image_service);
	if (ret)
		return ret;

	args[0].type = SI_AT_END;

	/*
	 * Shutdown/teardown the specified peripheral and unlock the memory area
	 * occupied by that region.
	 */
	ret = qcom_smci_call(pil_image_service, SMCI_PILIMAGE_OP_UNLOCKAREA, args, &result);
	if (ret)
		return ret;

	/* Release the smo */
	qcom_smci_release_smo(SMCI_PILOBJECT_UID, peripheral);

	return ret;
}
EXPORT_SYMBOL_GPL(qcom_scm_pas_shutdown);

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

int qcom_scm_pas_mem_setup(u32 peripheral, phys_addr_t addr, phys_addr_t size)
{
	struct si_object *pil_image_service = NULL;
	int ret, result;

	struct {
		uint64_t m_address;
		uint64_t m_size;
	} __packed buf = {addr, size};

	struct si_arg args[] = {
		{
			.type = SI_AT_IB,
			.b = { .addr = &buf, .size = sizeof(buf) },
		},
		{
			.type = SI_AT_END,
		}
	};

	ret = qcom_scm_pas_pil_service_init(peripheral, &pil_image_service);
	if (ret)
		return ret;

	ret = qcom_smci_call(pil_image_service, SMCI_PILIMAGE_OP_SETUPMEMAREA, args, &result);
	if (ret)
		pr_err("memory setup failed with result %d: %d\n", result, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(qcom_scm_pas_mem_setup);

int qcom_scm_pas_auth_and_reset(u32 peripheral)
{
	struct si_object *pil_image_service = NULL;
	struct si_object *smo = NULL;
	phys_addr_t addr = 0;
	size_t size = 0;
	int ret;

	ret = qcom_scm_pas_pil_service_init(peripheral, &pil_image_service);
	if (ret)
		return ret;

	/* Retrieve Memory Physical Address and Size */
	qcom_smci_get_memory(SMCI_PILOBJECT_UID, peripheral, &addr, &size);
	if (!addr || !size) {
		pr_err("Failed to get memory region address and size\n");
		return -EINVAL;
	}

	/*
	 * Initialize shared object with physical address and size
	 * If return value is 0, smo will not be NULL_SI_OBJECT.
	 */
	ret = qcom_smci_init_smobject(addr, phys_to_virt(addr), size, &smo,
		SI_CORE_MEM_OBJ_LEND);
	if (ret) {
		pr_err("Failed to initialize shared memory object (%d)\n", ret);
		return ret;
	}

	/*
	 * The memory area will be locked after auth reset; SMO cannot be released
	 * while in the memory lock state.
	 */
	ret = qcom_smci_smo_call(pil_image_service, smo, SMCI_PILIMAGE_OP_AUTHRESET);
	if (ret) {
		pr_err("Failed to auth and reset for %d\n", peripheral);

		/* Release smo */
		put_si_object(smo);
		return ret;
	}

	/*
	 * The memory area will be unlocked during PIL shutdown; store the SMO
	 * to be released at that time.
	 * qcom_smci_release_smo will be called in PIL shutdown to release SMO.
	 */
	qcom_smci_store_smo(SMCI_PILOBJECT_UID, peripheral, smo);

	return 0;
}
EXPORT_SYMBOL_GPL(qcom_scm_pas_auth_and_reset);

void qcom_scm_pas_store_memoryinfo(u32 peripheral, phys_addr_t addr,
		phys_addr_t size)
{
	qcom_smci_store_memory(SMCI_PILOBJECT_UID, peripheral, addr, size);
}
EXPORT_SYMBOL_GPL(qcom_scm_pas_store_memoryinfo);

int qcom_scm_assign_dump_table_region(bool __always_unused is_assign,
				      phys_addr_t addr, size_t size)
{
	struct si_object *dt_service = NULL;
	struct si_object *smo = NULL;
	int ret;

	ret = qcom_smci_init_client_service(SMCI_DT_UID, &dt_service);
	if (ret)
		return ret;

	ret = qcom_smci_init_smobject(addr, phys_to_virt(addr), size, &smo,
				      SI_CORE_MEM_OBJ_SHARE);
	if (ret) {
		pr_err("Failed to initialize shared memory object (%d)\n", ret);
		return ret;
	}

	ret = qcom_smci_smo_call(dt_service, smo, SMCI_DT_OP_SET);
	if (ret) {
		pr_err("Failed to set dump table (ret = %d).\n", ret);
		put_si_object(smo);
		return ret;
	}

	/*
	 * The memory area will remain locked until system crash, and the smo
	 * is released only when the module exits.
	 */
	qcom_smci_store_client_smo(SMCI_DT_UID, smo);
	return 0;
}
EXPORT_SYMBOL_GPL(qcom_scm_assign_dump_table_region);
