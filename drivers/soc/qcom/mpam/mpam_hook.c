// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/io.h>
#include <linux/string.h>
#include <linux/bitmap.h>
#include <soc/qcom/mpam.h>
#include <linux/mmu_context.h>

struct mpam_callback {
	struct list_head list;
	mpam_schedule_func_t *func;
};

static LIST_HEAD(callback_head);
static DEFINE_SPINLOCK(callback_head_lock);

void cpu_mpam_callback(struct task_struct *task)
{
	struct mpam_callback *node, *tmp;

	list_for_each_entry_safe(node, tmp, &callback_head, list)
		node->func(task);
}

int cpu_mpam_callback_register(mpam_schedule_func_t *func)
{
	struct mpam_callback *node;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	node->func = func;

	spin_lock(&callback_head_lock);
	list_add_tail(&node->list, &callback_head);
	spin_unlock(&callback_head_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(cpu_mpam_callback_register);

int cpu_mpam_callback_unregister(mpam_schedule_func_t *func)
{
	struct mpam_callback *node, *tmp;
	bool found = false;

	spin_lock(&callback_head_lock);
	list_for_each_entry_safe(node, tmp, &callback_head, list) {
		if (node->func == func) {
			list_del(&node->list);
			kfree(node);
			found = true;
			break;
		}
	}
	spin_unlock(&callback_head_lock);

	return found ? 0 : -ENOENT;
}
EXPORT_SYMBOL_GPL(cpu_mpam_callback_unregister);
