// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#define pr_fmt(fmt) "%s: " fmt,  __func__

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/qcom_dpd_proxy.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/of.h>

struct dpd_proxy {
	struct device *dev;
	struct si_object *env;
	struct si_object *service;
	/* Legacy ID of the current linux instance */
	int vmid;
};

static struct dpd_proxy *__dpd_priv;

/*
 * TEE Service Calls
 * -----------------
 */
int dpd_svc_unmap(struct dpd_scatterlist *dpd_sg, u32 domain_id, u64 iova)
{
	struct si_arg args[3] = { 0 };
	int result, ret;
	struct si_object_invoke_ctx oic;
	struct imm_unmap_cmd cmd = {0};

	cmd.eVM = domain_id;
	cmd.iova = iova;
	cmd.size = dpd_sg->size;

	args[0].b = (struct si_buffer) { {&cmd}, sizeof(cmd) };
	args[0].type = SI_AT_IB;

	/* si_object_do_invoke takes away 1 refcount */
	get_si_object(dpd_sg->shm);
	args[1].o = dpd_sg->shm;
	args[1].type = SI_AT_IO;
	args[2].type = SI_AT_END;

	ret = si_object_do_invoke(&oic, __dpd_priv->service, IMM_SVC_UNMAP, args, &result);
	if (ret) {
		pr_err("Invoke failed with %d\n", ret);
		return ret;
	}
	if (result) {
		pr_err("Unmap service call failed with %d for domain:%d\n",
			result, domain_id);
		return -EINVAL;
	}

	/* warn if already unmapped */
	WARN(atomic_dec_return(&dpd_sg->mapcount) < 0, "Mapcount underflow");
	return 0;
}
EXPORT_SYMBOL_GPL(dpd_svc_unmap);

int dpd_svc_map(struct dpd_scatterlist *dpd_sg, u32 domain_id, u32 flags, u64 iova)
{
	struct si_arg args[3] = { 0 };
	int result, ret;
	struct si_object_invoke_ctx oic;
	struct imm_map_cmd cmd = {0};

	if (flags & ~IMM_VALID_FLAGS)
		return -EINVAL;

	cmd.eVM = domain_id;
	cmd.flags = flags;
	cmd.iova = iova;
	cmd.size = dpd_sg->size;
	cmd.offset = 0;

	args[0].b = (struct si_buffer) { {&cmd}, sizeof(cmd) };
	args[0].type = SI_AT_IB;

	/* si_object_do_invoke takes away 1 refcount */
	get_si_object(dpd_sg->shm);
	args[1].o = dpd_sg->shm;
	args[1].type = SI_AT_IO;
	args[2].type = SI_AT_END;

	ret = si_object_do_invoke(&oic, __dpd_priv->service, IMM_SVC_MAP, args, &result);
	if (ret) {
		pr_err("Invoke failed with %d\n", ret);
		return ret;
	}
	if (result) {
		pr_err("Map service call failed with %d for domain %d\n",
			result, domain_id);
		return -EINVAL;
	}

	atomic_inc(&dpd_sg->mapcount);
	return 0;
}
EXPORT_SYMBOL_GPL(dpd_svc_map);


int dpd_proxy_available(void)
{
	if (!__dpd_priv)
		return -EPROBE_DEFER;
	return 0;
}
EXPORT_SYMBOL_GPL(dpd_proxy_available);

static int dpd_proxy_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct dpd_proxy *dpd;
	struct si_object_invoke_ctx oic;

	dpd = devm_kzalloc(dev, sizeof(*dpd), GFP_KERNEL);
	if (!dpd)
		return -ENOMEM;

	/* If TUIVM support is required, this can be added to DT */
	dpd->vmid = VMID_HLOS;
	dpd->dev = dev;

	ret = si_core_get_client_env(&oic, &dpd->env);
	if (ret) {
		dev_err_probe(dev, ret, "si_core_get_client_env\n");
		return ret;
	}

	ret = si_core_client_env_open(&oic, dpd->env, CSecureMemoryManager_UID, &dpd->service);
	if (ret) {
		dev_err_probe(dev, ret, "si_core_client_env_open\n");
		goto err_get_service;
	}

	dev_set_drvdata(dev, dpd);
	__dpd_priv = dpd;

	return 0;

err_get_service:
	put_si_object(dpd->env);
	return ret;
}

static void dpd_proxy_remove(struct platform_device *pdev)
{
	struct dpd_proxy *dpd = platform_get_drvdata(pdev);

	put_si_object(dpd->service);
	put_si_object(dpd->env);

	__dpd_priv = NULL;
}

static const struct of_device_id dpd_proxy_of_match[] = {
	{ .compatible = "qcom,dpd-proxy" },
	{}
};

static struct platform_driver dpd_proxy_driver = {
	.driver	= {
		.name			= "dpd-proxy",
		.of_match_table		= dpd_proxy_of_match,
	},
	.probe	= dpd_proxy_probe,
	.remove = dpd_proxy_remove,
};

module_platform_driver(dpd_proxy_driver);
MODULE_DESCRIPTION("QTI DPD Proxy Driver");
MODULE_LICENSE("GPL");
