// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/arm-smccc.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/namei.h>
#include <linux/stat.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/qtee_shmbridge.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/firmware/qcom/si_core_xts.h>
#include <linux/firmware/qcom/si_object.h>

#include "qcom_scm_smcinvoke.h"

static struct si_object_invoke_ctx oic;
static struct si_object *client_env;
static struct device *smo_buffer_dev;
static DEFINE_MUTEX(service_list_mutex);
static LIST_HEAD(smci_list);

static bool pil_smcinvoke_supported;

/* This function must be invoked with the mutex held */
static void __qcom_smci_find_service(u32 uid, u32 peripheral,
		struct smci_service_info **service,
		struct smci_image_service_info **image_service)
{
	struct smci_image_service_info *image_service_info = NULL;
	struct smci_service_info *service_info = NULL;

	if (!service || !image_service)
		return;

	list_for_each_entry(service_info, &smci_list, list) {
		if (service_info->uid != uid)
			continue;

		list_for_each_entry(image_service_info,
				&service_info->image_service_list, list) {
			if (image_service_info->peripheral == peripheral) {
				*image_service = image_service_info;
				break;
			}
		}
		*service = service_info;
		break;
	}
}

int qcom_smci_call(struct si_object *service, unsigned long op,
	struct si_arg args[], int *result)
{
	int ret;

	if (!service || !result)
		return -EINVAL;

	*result = -EINVAL;
	ret = si_object_do_invoke(&oic, service, op, args, result);
	if (ret || *result)
		pr_err("Failed to do invoke for %lu with result %d(ret = %d)\n",
			op, *result, ret);

	return ret ? ret : qcom_scmi_remap_error(*result);
}

int qcom_smci_smo_call(struct si_object *image_service, struct si_object *smo,
		unsigned long op)
{
	struct si_arg args[2] = { 0 };
	int ret, result = -EINVAL;

	if (!image_service)
		return -EINVAL;

	/* Hold a reference to it */
	ret = get_si_object(smo);
	if (!ret)
		return -EINVAL;

	args[0].o = smo;
	args[0].type = SI_AT_IO;
	args[1].type = SI_AT_END;

	ret = si_object_do_invoke(&oic, image_service, op, args, &result);
	if (ret || result)
		pr_err("Failed to do invoke for %lu with result %d(ret = %d)\n",
			op, result, ret);

	/*
	 * On si_object_do_invoke failure with these conditions, use put_si_object
	 * to balance the preceding get_si_object.
	 */
	if (((ret == -EINVAL || ret == -ENOMEM || ret == -EFAULT) && result)
			|| ret == -ENOSPC)
		put_si_object(smo);

	return ret ? ret : qcom_scmi_remap_error(result);
}

static int qcom_smci_init_client_service(u32 uid)
{
	struct smci_service_info *service_info = NULL;
	struct si_object *service = NULL;
	int ret;

	mutex_lock(&service_list_mutex);
	list_for_each_entry(service_info, &smci_list, list) {
		if (service_info->uid == uid) {
			mutex_unlock(&service_list_mutex);
			return 0;
		}
	}

	/* Initialize client service; it will be released when the module exits */
	ret = si_core_client_env_open(&oic, client_env, uid, &service);
	if (ret || service == NULL_SI_OBJECT) {
		pr_err("Failed to get client service for %d: (%d)\n", uid, ret);
		mutex_unlock(&service_list_mutex);
		return ret ? ret : -EINVAL;
	}

	service_info = kzalloc(sizeof(*service_info), GFP_KERNEL);
	if (!service_info) {
		put_si_object(service);
		mutex_unlock(&service_list_mutex);
		return -ENOMEM;
	}

	service_info->uid = uid;
	service_info->service = service;
	INIT_LIST_HEAD(&service_info->image_service_list);
	list_add_tail(&service_info->list, &smci_list);
	mutex_unlock(&service_list_mutex);

	return 0;
}

static int qcom_smci_get_client_image_service(u32 peripheral, unsigned long op,
		struct si_object *service, struct si_object **smci_image_service)
{
	struct si_arg args[3] = { 0 };
	int ret, result;

	args[0].b = (struct si_buffer) { {&peripheral}, sizeof(peripheral) };
	args[0].type = SI_AT_IB;
	args[1].type = SI_AT_OO;
	args[2].type = SI_AT_END;

	ret = qcom_smci_call(service, op, args, &result);
	if (ret)
		return ret;

	/* When return 0, it means smci_image_service will not be NULL_SI_OBJECT */
	if (args[1].o != NULL_SI_OBJECT) {
		*smci_image_service = args[1].o;
		return 0;
	}

	return -EINVAL;
}

static int qcom_smci_get_image_service(u32 uid, unsigned long op, u32 peripheral,
	struct si_object **smci_image_service)
{
	struct smci_image_service_info *image_service_info = NULL;
	struct smci_service_info *client_service = NULL;
	int ret = -EINVAL;

	mutex_lock(&service_list_mutex);
	__qcom_smci_find_service(uid, peripheral, &client_service, &image_service_info);
	if (!client_service) {
		pr_err("Failed to find client service for %d\n", uid);
		mutex_unlock(&service_list_mutex);
		return ret;
	}

	if (image_service_info) {
		*smci_image_service =
				image_service_info->smci_image_service;
		mutex_unlock(&service_list_mutex);
		return 0;
	}

	/*
	 * Initialize client image service; it will be released when the module exits.
	 * If return value is 0, *smci_image_service will not be NULL_SI_OBJECT.
	 */
	ret = qcom_smci_get_client_image_service(peripheral, op,
		client_service->service, smci_image_service);
	if (ret) {
		mutex_unlock(&service_list_mutex);
		return ret;
	}

	image_service_info = kzalloc(sizeof(*image_service_info), GFP_KERNEL);
	if (!image_service_info) {
		put_si_object(*smci_image_service);
		*smci_image_service = NULL_SI_OBJECT;
		mutex_unlock(&service_list_mutex);
		return -ENOMEM;
	}

	image_service_info->peripheral = peripheral;
	image_service_info->smci_image_service = *smci_image_service;
	list_add_tail(&image_service_info->list, &client_service->image_service_list);
	mutex_unlock(&service_list_mutex);

	return 0;
}

static void qcom_smci_smo_release(void *private)
{
	struct smo_buffer_info *buf_info = private;

	if (!buf_info)
		return;

	if (buf_info->sgt) {
		sg_free_table(buf_info->sgt);
		kfree(buf_info->sgt);
	}

	kfree(buf_info);
}

void qcom_smci_store_memory(u32 uid, u32 peripheral, phys_addr_t addr, size_t size)
{
	struct smci_image_service_info *image_service_info = NULL;
	struct smci_service_info *service_info = NULL;

	mutex_lock(&service_list_mutex);
	__qcom_smci_find_service(uid, peripheral, &service_info, &image_service_info);
	if (image_service_info) {
		image_service_info->addr = addr;
		image_service_info->size = size;
	}
	mutex_unlock(&service_list_mutex);
}

void qcom_smci_get_memory(u32 uid, u32 peripheral, phys_addr_t *addr, size_t *size)
{
	struct smci_image_service_info *image_service_info = NULL;
	struct smci_service_info *service_info = NULL;

	if (!addr || !size)
		return;

	mutex_lock(&service_list_mutex);
	__qcom_smci_find_service(uid, peripheral, &service_info, &image_service_info);
	if (image_service_info) {
		*addr = image_service_info->addr;
		*size = image_service_info->size;
	}
	mutex_unlock(&service_list_mutex);
}

void qcom_smci_store_smo(u32 uid, u32 peripheral, struct si_object *smo)
{
	struct smci_image_service_info *image_service_info = NULL;
	struct smci_service_info *service_info = NULL;

	mutex_lock(&service_list_mutex);
	__qcom_smci_find_service(uid, peripheral, &service_info, &image_service_info);
	if (image_service_info)
		image_service_info->smo = smo;
	mutex_unlock(&service_list_mutex);
}

void qcom_smci_release_smo(u32 uid, u32 peripheral)
{
	struct smci_image_service_info *image_service_info = NULL;
	struct smci_service_info *service_info = NULL;

	mutex_lock(&service_list_mutex);
	__qcom_smci_find_service(uid, peripheral, &service_info, &image_service_info);
	if (image_service_info && image_service_info->smo != NULL_SI_OBJECT) {
		put_si_object(image_service_info->smo);
		image_service_info->smo = NULL_SI_OBJECT;
	}
	mutex_unlock(&service_list_mutex);
}

int32_t qcom_smci_init_smobject(dma_addr_t dma_addr, void *vaddr, size_t size,
		struct si_object **smo, uint32_t flags)
{
	struct smo_buffer_info *buf_info = NULL;
	int ret = 0;

	if (!vaddr || !smo)
		return -EINVAL;

	if (!smo_buffer_dev) {
		pr_err("SMO buffer device not initialized\n");
		return -ENODEV;
	}

	buf_info = kzalloc(sizeof(*buf_info), GFP_KERNEL);
	if (!buf_info)
		return -ENOMEM;

	buf_info->dev = smo_buffer_dev;
	buf_info->vaddr = vaddr;
	buf_info->paddr = dma_addr;
	buf_info->size = size;

	buf_info->sgt = kzalloc(sizeof(*buf_info->sgt), GFP_KERNEL);
	if (!buf_info->sgt) {
		ret = -ENOMEM;
		goto exit_free_buf;
	}

	ret = dma_get_sgtable(smo_buffer_dev, buf_info->sgt, buf_info->vaddr, dma_addr, size);
	if (ret)
		goto exit_free_sgt;

	buf_info->object = init_si_mem_object_sg(buf_info->sgt, 0,
			flags, qcom_smci_smo_release, buf_info);
	if (buf_info->object == NULL_SI_OBJECT) {
		ret = -EINVAL;
		goto exit_free_sgtable;
	}

	if (flags == SI_CORE_MEM_OBJ_LEND) {
		/*
		 * The early_map_memory_obj() API is only used to signify that FFA lend is
		 * explicit. When the last reference is destroyed, the memory will be reclaimed.
		 * The reclaim operation drives the underlying system to revoke the mapping and
		 * access permissions, without requiring an explicit unmap beforehand.
		 */
		ret = early_map_memory_obj(buf_info->object);
		if (ret)
			goto exit_free_object;
	}

	*smo = buf_info->object;
	return ret;

exit_free_object:
	put_si_object(buf_info->object);
exit_free_sgtable:
	sg_free_table(buf_info->sgt);
exit_free_sgt:
	kfree(buf_info->sgt);
exit_free_buf:
	kfree(buf_info);

	return ret;
}

int32_t qcom_smci_pil_init_smobject(const void *metadata, size_t metadata_len,
	struct si_object **smo, struct qcom_scm_pas_metadata *ctx,
	struct device *dev_32bit, uint32_t flags)
{
	struct device *dma_dev = smo_buffer_dev;
	dma_addr_t mdata_phys;
	void *mdata_buf;
	int ret;

	if (!ctx || !metadata || metadata_len == 0)
		return -EINVAL;

	if (dev_32bit)
		dma_dev = dev_32bit;

	mdata_buf = dma_alloc_coherent(dma_dev, metadata_len, &mdata_phys, GFP_KERNEL);
	if (!mdata_buf)
		return -ENOMEM;
	memcpy(mdata_buf, metadata, metadata_len);

	ret = qcom_smci_init_smobject(mdata_phys, mdata_buf, metadata_len, smo, flags);
	if (ret) {
		dev_err(dma_dev, "Failed to get share memory-object: %d\n", ret);
		dma_free_coherent(dma_dev, metadata_len, mdata_buf, mdata_phys);
		return ret;
	}

	ctx->ptr = mdata_buf;
	ctx->phys = mdata_phys;
	ctx->size = metadata_len;

	return ret;
}

int qcom_scm_pas_pil_service_init(u32 peripheral, struct si_object **pil_image_service)
{
	int ret;

	if (!pil_image_service) {
		pr_err("PIL SMCInvoke: Invalid output parameter\n");
		return -EINVAL;
	}

	if (!pil_smcinvoke_supported) {
		pr_err("PIL SMCInvoke is not supported\n");
		return -ENODEV;
	}

	ret = qcom_smci_get_image_service(SMCI_PILOBJECT_UID, SMCI_PIL_OP_INITIMAGE,
		peripheral, pil_image_service);
	if (ret)
		pr_err("Failed to create a new PIL instance for particular Subsystem (%d)\n", ret);

	return ret;
}

static int qcom_smci_service_probe(struct platform_device *pdev)
{
	int ret;

	ret = si_core_get_client_env(&oic, &client_env);
	if (ret || !client_env) {
		pr_err("Failed to get client_env (%d)\n", ret);
		return ret ? ret : -ENODEV;
	}

	ret = qcom_smci_init_client_service(SMCI_PILOBJECT_UID);
	if (!ret) {
		pr_info("PIL SMC invocation is supported\n");
		pil_smcinvoke_supported = true;
	}

	smo_buffer_dev = &pdev->dev;

	return 0;
}

static void qcom_smci_service_remove(struct platform_device *pdev)
{
	struct smci_image_service_info *image_service_info, *image_tmp;
	struct smci_service_info *service_info, *tmp;

	mutex_lock(&service_list_mutex);
	if (list_empty(&smci_list)) {
		mutex_unlock(&service_list_mutex);
		return;
	}

	/* Free registered service information */
	list_for_each_entry_safe(service_info, tmp, &smci_list, list) {
		list_for_each_entry_safe(image_service_info, image_tmp,
					&service_info->image_service_list, list) {
			if (image_service_info->smci_image_service)
				put_si_object(image_service_info->smci_image_service);

			if (image_service_info->smo)
				put_si_object(image_service_info->smo);

			list_del(&image_service_info->list);
			kfree(image_service_info);
		}

		if (service_info->service)
			put_si_object(service_info->service);

		list_del(&service_info->list);
		kfree(service_info);
	}

	mutex_unlock(&service_list_mutex);
	put_si_object(client_env);
	pr_debug("Service removed\n");
}

static const struct of_device_id qcom_scm_smci_match[] = {
	{ .compatible = "qcom,scm-smcinvoke", },
	{}
};

static struct platform_driver qcom_scm_smcinvoke_driver = {
	.probe = qcom_smci_service_probe,
	.remove = qcom_smci_service_remove,
	.driver = {
		.name = "qcom-smc-invoke-driver",
		.of_match_table = qcom_scm_smci_match,
	},
};
module_platform_driver(qcom_scm_smcinvoke_driver);

MODULE_DESCRIPTION("Qualcomm SCM SMCInvoke driver");
MODULE_LICENSE("GPL");

