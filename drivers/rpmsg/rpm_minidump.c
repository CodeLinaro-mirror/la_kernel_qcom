// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/device.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/remoteproc/qcom_rproc.h>
#include <soc/qcom/qcom_ramdump.h>
#include <linux/types.h>
#include <soc/qcom/rpm-smd.h>

#define RPM_MINIDUMP_REQ	0x6373696d
#define RPM_MINIDUMP_ID		0x0
#define RPM_MINIDUMP_KEY	0x64676F6C
#define RPM_MINIDUMP_ENTER	0x1
#define RPM_MINIDUMP_EXIT	0x0

struct minidump_data {
	struct device *dev;
	struct resource *res;
	void *minidump_handler;
	struct notifier_block minidump_modem_ssr_nb;
};
static struct minidump_data minidump_data;

static int minidump_modem_ssr_notifier_nb(struct notifier_block *nb,
					unsigned long code, void *data)
{
	struct qcom_dump_segment ramdump_entry;
	struct qcom_ssr_notify_data *notif_data = data;
	struct list_head head;
	struct msm_rpm_kvp kvp_req;
	static bool crashed;
	int ret = 0;
	u32 val;

	if (!notif_data)
		return NOTIFY_DONE;

	if (code == QCOM_SSR_BEFORE_SHUTDOWN && notif_data->crashed) {
		val = RPM_MINIDUMP_ENTER;
		kvp_req.key = RPM_MINIDUMP_KEY;
		kvp_req.data = (void *)&val;
		kvp_req.length = sizeof(val);

		ret = msm_rpm_send_message(MSM_RPM_CTX_ACTIVE_SET, RPM_MINIDUMP_REQ,
								RPM_MINIDUMP_ID, &kvp_req, 1);
		if (ret) {
			dev_err(minidump_data.dev,
					"Failed to send RPM message before SSR shutdown\n");
			return NOTIFY_BAD;
		}

		crashed = notif_data->crashed;
	} else if (code == QCOM_SSR_AFTER_SHUTDOWN && crashed) {
		crashed = notif_data->crashed;
		memset(&ramdump_entry, 0, sizeof(ramdump_entry));

		ramdump_entry.da = minidump_data.res->start;
		ramdump_entry.size = resource_size(minidump_data.res);
		ramdump_entry.name = "rpm";

		INIT_LIST_HEAD(&head);
		list_add(&ramdump_entry.node, &head);

		ret = qcom_elf_dump_using_section(&head, minidump_data.dev, ELF_CLASS);
		if (ret)
			dev_err(minidump_data.dev,
					"Failed to collect RPM minidump after SSR: %d\n", ret);

		val = RPM_MINIDUMP_EXIT;
		kvp_req.key = RPM_MINIDUMP_KEY;
		kvp_req.data = (void *)&val;
		kvp_req.length = sizeof(val);

		ret = msm_rpm_send_message(MSM_RPM_CTX_ACTIVE_SET, RPM_MINIDUMP_REQ,
								RPM_MINIDUMP_ID, &kvp_req, 1);
		if (ret) {
			dev_err(minidump_data.dev,
					"Failed to send RPM message after SSR shutdown\n");
			return NOTIFY_BAD;
		}
	}
	return NOTIFY_OK;
}


static int rpm_minidump_driver_probe(struct platform_device *pdev)
{
	struct device_node *node;
	int ret;

	minidump_data.dev = &pdev->dev;
	minidump_data.minidump_modem_ssr_nb.notifier_call = minidump_modem_ssr_notifier_nb;

	minidump_data.res = devm_kzalloc(minidump_data.dev, sizeof(struct resource), GFP_KERNEL);
	if (!minidump_data.res)
		return -ENOMEM;

	node = of_parse_phandle(minidump_data.dev->of_node, "memory-region", 0);
	if (!node) {
		dev_err(minidump_data.dev, "missing shareable memory-region\n");
		return -EINVAL;
	}

	ret = of_address_to_resource(node, 0, minidump_data.res);
	of_node_put(node);
	if (ret)
		return ret;

	minidump_data.minidump_handler =
		qcom_register_ssr_notifier("mpss", &minidump_data.minidump_modem_ssr_nb);
	if (IS_ERR(minidump_data.minidump_handler)) {
		ret = PTR_ERR(minidump_data.minidump_handler);
		dev_err(minidump_data.dev, "Modem register notifier failed: %d\n", ret);
		return ret;
	}
	return 0;
}

static void rpm_minidump_driver_remove(struct platform_device *pdev)
{
	qcom_unregister_ssr_notifier(minidump_data.minidump_handler,
			&minidump_data.minidump_modem_ssr_nb);
}

static const struct of_device_id rpm_minidump_of_match[] = {
	{ .compatible = "qcom,rpm-minidump", },
	{},
};

static struct platform_driver rpm_minidump_driver = {
	.probe = rpm_minidump_driver_probe,
	.remove = rpm_minidump_driver_remove,
	.driver  = {
		.name   = "rpm-minidump",
		.of_match_table = rpm_minidump_of_match,
		.suppress_bind_attrs = true,
	},
};

static int __init msm_rpm_minidump_driver_init(void)
{
	return platform_driver_register(&rpm_minidump_driver);
}

module_init(msm_rpm_minidump_driver_init);
MODULE_DESCRIPTION("Qualcomm Technologies, Inc. RPM Minidump Driver");
MODULE_LICENSE("GPL");
