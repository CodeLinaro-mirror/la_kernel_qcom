// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */


#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/pm.h>
#include <linux/delay.h>
#include <linux/of_address.h>
#include <linux/panic_notifier.h>

#include <linux/cacheflush.h>
#include <asm/system_misc.h>
#if IS_ENABLED(CONFIG_QCOM_WDT_CORE)
#include <soc/qcom/watchdog.h>
#endif

#define REBOOT_BOOTLOADER_MAGIC 0x77665500

static int in_panic;
static struct notifier_block restart_nb;
static void *restart_reason = NULL;

static int panic_prep_restart(struct notifier_block *this,
			      unsigned long event, void *ptr)
{
	in_panic = 1;
	return NOTIFY_DONE;
}

static struct notifier_block panic_blk = {
	.notifier_call	= panic_prep_restart,
};

static int do_vm_restart(struct notifier_block *unused, unsigned long action,
						void *arg)
{
	pr_notice("Going down for vm restart now\n");

	if (arg != NULL) {
		if (!strncmp(arg, "bootloader", 10)) {
			pr_notice("reboot bootloader requested\n");
			__raw_writel(REBOOT_BOOTLOADER_MAGIC, restart_reason);
		}
	}

#if IS_ENABLED(CONFIG_QCOM_WDT_CORE)
	if (in_panic)
		qcom_wdt_trigger_bite();
#endif

	return NOTIFY_DONE;
}

static int vm_restart_probe(struct platform_device *pdev)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL,
				"qcom,msm-imem-vm-restart_reason");
	if (!np) {
		pr_err("unable to find DT imem restart reason node\n");
		return -ENODEV;
	} else {
		restart_reason = of_iomap(np, 0);
		if (!restart_reason) {
			pr_err("unable to map imem restart reason offset\n");
			return -ENOMEM;
		}
	}

	atomic_notifier_chain_register(&panic_notifier_list, &panic_blk);

	restart_nb.notifier_call = do_vm_restart;
	restart_nb.priority = 200;
	register_restart_handler(&restart_nb);

	return 0;
}

static const struct of_device_id of_vm_restart_match[] = {
	{ .compatible = "qcom,vm-restart", },
	{},
};
MODULE_DEVICE_TABLE(of, of_vm_restart_match);

static struct platform_driver vm_restart_driver = {
	.probe = vm_restart_probe,
	.driver = {
		.name = "msm-vm-restart",
		.of_match_table = of_match_ptr(of_vm_restart_match),
	},
};

static int __init vm_restart_init(void)
{
	return platform_driver_register(&vm_restart_driver);
}

#if IS_MODULE(CONFIG_POWER_RESET_QCOM_VM)
module_init(vm_restart_init);
#else
pure_initcall(vm_restart_init);
#endif

static __exit void vm_restart_exit(void)
{
	platform_driver_unregister(&vm_restart_driver);
	if (restart_reason)
		iounmap(restart_reason);
}
module_exit(vm_restart_exit);

MODULE_DESCRIPTION("MSM VM Poweroff Driver");
MODULE_LICENSE("GPL");
