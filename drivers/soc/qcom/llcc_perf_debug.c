// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/soc/qcom/llcc-qcom.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/init.h>

#define PCB_COUNT                     320
#define PCBs_PER_REG                  16
#define REG_COUNT                     (PCB_COUNT/PCBs_PER_REG)
#define LLCC_PCB_STATUS               0x25AA6698
#define LLCC_PCB_WAKEUP               0x25AA6400
#define LLCC_PCB_SLEEP                0x25AA6300
#define LLCC_PCB_CMD                  0x25AA6018
#define MY_MMIO_SIZE                  0x400000
#define LLCC_TRP_ADR_CTRL_CFG         0x25A452A8
#define LLCC_TRP_ADR_CTRL_IDLE_CNTR_CFG  0x25A452AC
#define LLCC_TRP_ADR_PCB_SLEEP_CFG    0x25A452B0
#define LLCC_TRP_ADR_PCB_WAKEUP_CFG   0x25A452D8
#define PAZE_SIZE                     4096
#define WAKE_CMD                      (1<<1)
#define SLEEP_CMD                     (1<<0)

static struct dentry *dir_entry, *dir_adr_entry;
static void __iomem  *status_reg_base, *adr_cmd, *adr_idle_thres_cmd;
static struct regmap *regmap, *adr_idle_thres_regmap_cmd, *adr_regmap_cmd;
u8 status[PCB_COUNT];
struct regmap_config regmap_debug_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
};

static void status_reg_fun(void)
{
	int index = 0, offset = 0, ret, i, j, reg_val = 0;

	for (i = 0; i < REG_COUNT; i++) {
		offset = i*4;
		ret = regmap_read(regmap, offset, &reg_val);
		if (ret) {
			pr_err("failed to read register at offset 0x%x\n", LLCC_PCB_STATUS+offset);
			continue;
		}
		for (j = 0; j < PCBs_PER_REG; j++) {
			index = i*PCBs_PER_REG+j;
			status[index] = (reg_val>>(j*2))&0x3;
		}
	}
}

static ssize_t get_pcb_status(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	int pcb_index, len;
	char status_buf[40];

	pcb_index = (int)(uintptr_t)file_inode(file)->i_private;
	if ((pcb_index < 0) || (pcb_index >= PCB_COUNT))
		return -EINVAL;

	status_reg_fun();
	len = snprintf(status_buf, sizeof(status_buf), "Status of  pcb%d : %d\n",  pcb_index,
	status[pcb_index]);

	return simple_read_from_buffer(user_buf, count, ppos, status_buf, len);
}

static ssize_t get_status_all(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	int i, len = 0;
	static char *pcb_buf;

	pcb_buf = kmalloc(PAZE_SIZE, GFP_KERNEL);
	if (!pcb_buf)
		return -ENOMEM;
	status_reg_fun();
	for (i = 0; i < PCB_COUNT && len < PAZE_SIZE-32; i++)
		len += snprintf(pcb_buf+len, PAZE_SIZE-len, "pcb %d:%d\n", i, status[i]);

	return simple_read_from_buffer(user_buf, count, ppos, pcb_buf, len);
}

static ssize_t set_adr_idle_thres(struct file *file, const char __user *user_buf,
					size_t count, loff_t *ppos)
{
	unsigned long  value = 0;
	int reg_val = 1;
	char kbuf[10];

	if (copy_from_user(kbuf, user_buf, count))
		return -EFAULT;

	kbuf[count] = '\0';
	if (kstrtoul(kbuf, 0, &value))
		return -EINVAL;

	regmap_write(adr_regmap_cmd, 0, reg_val);
	regmap_write(adr_idle_thres_regmap_cmd, 0, value);
	return count;
}

static ssize_t get_adr_idle_thres(struct file *file, char __user *user_buf,
					size_t count, loff_t *ppos)
{
	int len, reg_val;
	char buf[40];

	regmap_read(adr_idle_thres_regmap_cmd, 0, &reg_val);
	len = snprintf(buf, sizeof(buf), "thres value : 0x%x\n", reg_val);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static const struct file_operations status_fops = {
	.open = simple_open,
	.read = get_status_all,
};

static const struct file_operations fops = {
	.open = simple_open,
	.read = get_pcb_status,
};

static const struct file_operations adr_fops = {
	.open = simple_open,
	.write = set_adr_idle_thres,
	.read = get_adr_idle_thres,
};

static int llcc_qcom_create_fs_entries(void)
{
	static struct dentry *ret, *pcb_dir;
	int i;
	char *pcb_name;

	dir_entry = debugfs_create_dir("llcc_power_debug", NULL);
	if (IS_ERR(dir_entry))
		return PTR_ERR(dir_entry);

	for (i = 0; i < PCB_COUNT; i++) {
		pcb_name = kmalloc(20, GFP_KERNEL);
		if (!pcb_name)
			goto error_cleanup;

		snprintf(pcb_name, 20, "PCB%d", i);
		pcb_dir = debugfs_create_dir(pcb_name, dir_entry);
		kfree(pcb_name);
		if (IS_ERR(pcb_dir))
			goto error_cleanup;

		ret = debugfs_create_file("pcb_status", 0664, pcb_dir, (void *)(uintptr_t)i, &fops);
		if (IS_ERR(ret))
			goto error_cleanup;
	}
	ret = debugfs_create_file("status_all", 0664, dir_entry, NULL, &status_fops);
		if (IS_ERR(ret))
			goto error_cleanup;

	dir_adr_entry = debugfs_create_dir("adr", dir_entry);
	ret = debugfs_create_file("idle-threshold", 0664, dir_adr_entry, NULL, &adr_fops);
		if (IS_ERR(ret))
			goto error_cleanup;
	return 0;

error_cleanup:
	return -ENOENT;
}

static int __init qcom_llcc_debug_init(void)
{
	int ret;

	status_reg_base = ioremap(LLCC_PCB_STATUS, MY_MMIO_SIZE);
	if (!status_reg_base) {
		pr_err("failed to map physical region\n");
		return -ENOMEM;
	}

	regmap = regmap_init_mmio(NULL, status_reg_base, &regmap_debug_config);
	if (IS_ERR(regmap)) {
		pr_err("failed to initialize regmap\n");
		iounmap(status_reg_base);
		return PTR_ERR(regmap);
	}

	adr_cmd = ioremap(LLCC_TRP_ADR_CTRL_CFG, MY_MMIO_SIZE);
	if (!adr_cmd)
		return -ENOMEM;

	adr_regmap_cmd = regmap_init_mmio(NULL, adr_cmd, &regmap_debug_config);
	if (IS_ERR(adr_regmap_cmd)) {
		iounmap(adr_cmd);
		return PTR_ERR(adr_regmap_cmd);
	}

	adr_idle_thres_cmd = ioremap(LLCC_TRP_ADR_CTRL_IDLE_CNTR_CFG, MY_MMIO_SIZE);
	if (!adr_idle_thres_cmd)
		return -ENOMEM;

	adr_idle_thres_regmap_cmd = regmap_init_mmio(NULL, adr_idle_thres_cmd,
							&regmap_debug_config);
	if (IS_ERR(adr_idle_thres_regmap_cmd)) {
		iounmap(adr_idle_thres_cmd);
		return PTR_ERR(adr_idle_thres_regmap_cmd);
	}

	ret = llcc_qcom_create_fs_entries();
	pr_info("module loaded for debug entries\n");
	return 0;
}
module_init(qcom_llcc_debug_init);

MODULE_DESCRIPTION("Qualcomm LLCC Debug Driver");
MODULE_LICENSE("GPL");
