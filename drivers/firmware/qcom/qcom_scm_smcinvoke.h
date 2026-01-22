/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __QCOM_SMC_INVOKE_H
#define __QCOM_SMC_INVOKE_H

#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/firmware/qcom/si_object.h>
#include <linux/firmware/qcom/si_core_xts.h>
#include <linux/qtee_shmbridge.h>

#include "qcom_scm.h"

#define SMCI_DT_UID 445
#define SMCI_PILOBJECT_UID 446

#define SMCI_DT_OP_SET 0
#define SMCI_PIL_OP_INITIMAGE 0
#define SMCI_PILIMAGE_OP_VERIFYMETADATA 0
#define SMCI_PILIMAGE_OP_SETUPMEMAREA 1
#define SMCI_PILIMAGE_OP_AUTHRESET 2
#define SMCI_PILIMAGE_OP_UNLOCKAREA 3

struct smci_image_service_info {
	struct list_head list;

	/* peripheral for getting the image service for specific subsystem */
	int peripheral;
	phys_addr_t addr;
	size_t size;

	struct si_object *smo;
	struct si_object *smci_image_service;
};

struct smci_service_info {
	struct list_head list;

	/* uid for opening ClientEnv OP */
	u32 uid;
	struct si_object *smo;
	struct si_object *service;
	struct list_head image_service_list;
};

struct smo_buffer_info {
	struct device *dev;
	struct sg_table *sgt;
	struct si_object *object;
	void *vaddr;
	phys_addr_t paddr;
	size_t size;
};

void qcom_smci_store_memory(u32 uid, u32 peripheral, phys_addr_t addr, size_t size);
void qcom_smci_get_memory(u32 uid, u32 peripheral, phys_addr_t *addr, size_t *size);
void qcom_smci_store_smo(u32 uid, u32 peripheral, struct si_object *smo);
void qcom_smci_release_smo(u32 uid, u32 peripheral);
void qcom_smci_store_client_smo(u32 uid, struct si_object *smo);
int qcom_smci_init_client_service(u32 uid, struct si_object **service);
int qcom_scm_pas_pil_service_init(u32 peripheral, struct si_object **pil_image_service);
int qcom_smci_call(struct si_object *object, unsigned long op,
		struct si_arg args[], int *result);
int qcom_smci_smo_call(struct si_object *image_service, struct si_object *smo,
		unsigned long op);
int32_t qcom_smci_pil_init_smobject(const void *metadata, size_t metadata_len,
		struct si_object **smo, struct qcom_scm_pas_metadata *ctx,
		struct device *dev_32bit, uint32_t flags);
int32_t qcom_smci_init_smobject(dma_addr_t dma_addr, void *vaddr, size_t size,
		struct si_object **smo, uint32_t flags);

/* SMCInvoke common error codes */
#define QCOM_SCMI_INVALID_INPUT_PARAM		10
#define QCOM_SCMI_INVALID_OBJECT		11
#define QCOM_SCMI_INVALID_INFO			12
#define QCOM_SCMI_INVALID_REGION_INFO		13
#define QCOM_SCMI_INVALID_METADATA		14
#define QCOM_SCMI_MEM_ALLOCATION		15
#define QCOM_SCMI_MAP_REGION			16
#define QCOM_SCMI_UNMAP_REGION			17
#define QCOM_SCMI_METADATA_VERIFY_FAILED	18
#define QCOM_SCMI_SET_MEM_AREA_FAILED		19
#define QCOM_SCMI_AUTH_RESET_FAILED		20
#define QCOM_SCMI_PIL_UNLOCK_AREA_FAILED	21
#define QCOM_SCMI_AC_UNLOCK_AREA_FAILED		22
#define QCOM_SCMI_LEAKING_MEM_INTENTIONALLY	23
#define QCOM_SCMI_INVALID_NUM_REGION_INFO	24

static inline int qcom_scmi_remap_error(int err)
{
	if (!err)
		return err;

	switch (err) {
	case QCOM_SCMI_INVALID_INPUT_PARAM:
	case QCOM_SCMI_INVALID_OBJECT:
	case QCOM_SCMI_INVALID_INFO:
	case QCOM_SCMI_INVALID_REGION_INFO:
	case QCOM_SCMI_INVALID_METADATA:
	case QCOM_SCMI_INVALID_NUM_REGION_INFO:
		return -EINVAL;
	case QCOM_SCMI_MEM_ALLOCATION:
		return -ENOMEM;
	case QCOM_SCMI_MAP_REGION:
	case QCOM_SCMI_UNMAP_REGION:
		return -EFAULT;
	case QCOM_SCMI_METADATA_VERIFY_FAILED:
		return -EKEYREJECTED;
	case QCOM_SCMI_SET_MEM_AREA_FAILED:
		return -EACCES;
	case QCOM_SCMI_AUTH_RESET_FAILED:
	case QCOM_SCMI_PIL_UNLOCK_AREA_FAILED:
	case QCOM_SCMI_AC_UNLOCK_AREA_FAILED:
		return -EIO;
	case QCOM_SCMI_LEAKING_MEM_INTENTIONALLY:
		return -EBUSY;
	}
	return -EINVAL;
}

#endif /* __QCOM_SMC_INVOKE_H */
