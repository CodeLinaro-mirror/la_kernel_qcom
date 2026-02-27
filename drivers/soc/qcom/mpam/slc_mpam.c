// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
 #define pr_fmt(fmt) "mpam_slc: " fmt

#include <linux/io.h>
#include <linux/of.h>
#include <linux/configfs.h>
#include <linux/string.h>
#include <soc/qcom/mpam.h>
#include <soc/qcom/mpam_msc.h>
#include <soc/qcom/mpam_slc.h>

#define NO_PARTID		(-1)
#define MAX_RETRY_CNT		500

struct slc_mpam_item {
	struct config_group group;
	int part_id;
	int client_id;
	bool cap_mon_support;
	bool cap_mon_enabled;
	bool miss_mon_support;
	bool miss_mon_enabled;
	bool fe_mon_support;
	bool fe_mon_enabled;
	bool be_mon_support;
	bool be_mon_enabled;
};

static char gear_index[][25] = {
	"SLC_GEAR_HIGH",
	"SLC_GEAR_LOW",
	"SLC_GEAR_BYPASS",
	"",
};

static struct config_group *root_group;

static inline struct slc_mpam_item *get_pm_item(
					   struct config_item *item)
{
	return container_of(to_config_group(item),
				struct slc_mpam_item, group);
}

static inline int set_msc_query(struct msc_query *query,
					   struct slc_mpam_item *pm_item)
{
	struct qcom_mpam_msc *qcom_mpam_msc;

	qcom_mpam_msc = qcom_msc_lookup(SLC);
	if (!qcom_mpam_msc)
		return -ENODEV;

	query->qcom_msc_id.qcom_msc_type =
		qcom_mpam_msc->qcom_msc_id.qcom_msc_type;
	query->qcom_msc_id.qcom_msc_class =
		qcom_mpam_msc->qcom_msc_id.qcom_msc_class;
	query->qcom_msc_id.idx =
		qcom_mpam_msc->qcom_msc_id.idx;

	query->client_id = pm_item->client_id;
	query->part_id = pm_item->part_id;

	return 0;
}

static inline union slc_partid_capability_def *get_partid_cap(
		struct slc_mpam_item *pm_item)
{
	struct qcom_mpam_msc *qcom_mpam_msc;
	struct qcom_slc_capability *slc_capability;
	struct slc_client_capability *client_cap;
	union slc_partid_capability_def *partid_cap = NULL;
	uint32_t slc_firmware_ver = 0;
	int i;

	qcom_mpam_msc = qcom_msc_lookup(SLC);
	if (!qcom_mpam_msc)
		return NULL;

	msc_system_get_mpam_version(SLC, &slc_firmware_ver);

	slc_capability = (struct qcom_slc_capability *)
		qcom_mpam_msc->msc_capability;
	if (!slc_capability)
		return NULL;

	for (i = 0; i < slc_capability->num_clients; i++) {
		client_cap = &slc_capability->slc_client_cap[i];
		if (client_cap->client_info.client_id == pm_item->client_id) {
			partid_cap = client_cap->slc_partid_cap;
			break;
		}
		client_cap = NULL;
	}

	if (client_cap && partid_cap) {
		for (i = 0; i < client_cap->client_info.num_part_id; i++) {
			if (slc_firmware_ver == SLC_MPAM_VERSION_0) {
				if (partid_cap[i].v0_cap.part_id == pm_item->part_id)
					return (union slc_partid_capability_def *)
						&partid_cap[i].v0_cap;
			} else {
				if (partid_cap[i].v1_cap.part_id == pm_item->part_id)
					return (union slc_partid_capability_def *)
						&partid_cap[i].v1_cap;
			}
		}
	}

	return NULL;
}

static ssize_t slc_mpam_schemata_show(struct config_item *item,
		char *page)
{
	int ret;
	struct msc_query query;
	struct qcom_slc_gear_val gear_config;

	set_msc_query(&query, get_pm_item(item));

	ret = msc_system_get_partition(SLC, &query, &gear_config);
	if (ret)
		return scnprintf(page, PAGE_SIZE,
			"failed to get schemata %d\n", ret);

	return scnprintf(page, PAGE_SIZE, "gear=%d\n",
		gear_config.gear_val);
}

static ssize_t slc_mpam_schemata_store(struct config_item *item,
		const char *page, size_t count)
{
	int ret, input;
	char *token, *param_name;
	struct msc_query query;
	struct qcom_slc_gear_val gear_config;

	set_msc_query(&query, get_pm_item(item));

	while ((token = strsep((char **)&page, ",")) != NULL) {
		param_name = strsep(&token, "=");
		if (param_name == NULL || token == NULL)
			continue;
		if (kstrtouint(token, 0, &input) < 0) {
			pr_err("invalid argument for %s\n", param_name);
			return -EINVAL;
		}

		if (!strcmp("gear", param_name))
			gear_config.gear_val = input;
	}

	ret = msc_system_set_partition(SLC, &query, &gear_config);
	if (ret) {
		pr_err("set slc cache partition failed, ret=%d\n", ret);
		return -EBUSY;
	}

	return count;
}
CONFIGFS_ATTR(slc_mpam_, schemata);

static ssize_t slc_mpam_monitor_schemata_show(struct config_item *item,
		char *page)
{
	size_t len = 0;
	struct slc_mpam_item *pm_item = get_pm_item(item);

	if (pm_item->cap_mon_support)
		len += scnprintf(page + len, PAGE_SIZE - len,
			"cap=%d,", pm_item->cap_mon_enabled);
	if (pm_item->miss_mon_support)
		len += scnprintf(page + len, PAGE_SIZE - len,
			"miss=%d,", pm_item->miss_mon_enabled);
	if (pm_item->fe_mon_support)
		len += scnprintf(page + len, PAGE_SIZE - len,
			"fe=%d,", pm_item->fe_mon_enabled);
	if (pm_item->be_mon_support)
		len += scnprintf(page + len, PAGE_SIZE - len,
			"be=%d,", pm_item->be_mon_enabled);
	if (len)
		len += scnprintf(page + len - 1, PAGE_SIZE - len, "\n");

	return len;
}

static ssize_t slc_mpam_monitor_schemata_store(struct config_item *item,
		const char *page, size_t count)
{
	int ret, need_set;
	bool input;
	char *token, *param_name;
	struct msc_query query;
	struct slc_mon_config_val mon_cfg_val;
	struct slc_mpam_item *pm_item = get_pm_item(item);

	set_msc_query(&query, pm_item);

	while ((token = strsep((char **)&page, ",")) != NULL) {
		need_set = 0;
		param_name = strsep(&token, "=");
		if (param_name == NULL || token == NULL)
			continue;

		if (kstrtobool(token, &input) < 0) {
			pr_err("invalid argument for %s\n", param_name);
			return -EINVAL;
		}

		if (!strcmp("cap", param_name) && pm_item->cap_mon_support &&
				(pm_item->cap_mon_enabled != input)) {
			mon_cfg_val.slc_mon_function = CACHE_CAPACITY_CONFIG;
			mon_cfg_val.enable = input;
			need_set = 1;
		} else if (!strcmp("miss", param_name) && pm_item->miss_mon_support &&
				(pm_item->miss_mon_enabled != input)) {
			mon_cfg_val.slc_mon_function = CACHE_READ_MISS_CONFIG;
			mon_cfg_val.enable = input;
			need_set = 1;
		} else if (!strcmp("fe", param_name) && pm_item->fe_mon_support  &&
				(pm_item->fe_mon_enabled != input)) {
			mon_cfg_val.slc_mon_function = CACHE_FE_MON_CONFIG;
			mon_cfg_val.enable = input;
			need_set = 1;
		} else if (!strcmp("be", param_name)  && pm_item->be_mon_support  &&
				(pm_item->be_mon_enabled != input)) {
			mon_cfg_val.slc_mon_function = CACHE_BE_MON_CONFIG;
			mon_cfg_val.enable = input;
			need_set = 1;
		}

		if (need_set) {
			ret = msc_system_mon_config(SLC, &query, &mon_cfg_val);
			if (ret) {
				pr_err("%s monitor %s failed %d\n", param_name,
					(input) ? "enable" : "disable", ret);
				return ret;
			}

			if (mon_cfg_val.slc_mon_function == CACHE_CAPACITY_CONFIG)
				pm_item->cap_mon_enabled = input;
			else if (mon_cfg_val.slc_mon_function == CACHE_READ_MISS_CONFIG)
				pm_item->miss_mon_enabled = input;
			else if (mon_cfg_val.slc_mon_function == CACHE_FE_MON_CONFIG)
				pm_item->fe_mon_enabled = input;
			else if (mon_cfg_val.slc_mon_function == CACHE_BE_MON_CONFIG)
				pm_item->be_mon_enabled = input;
		}
	}

	return count;
}
CONFIGFS_ATTR(slc_mpam_, monitor_schemata);

static ssize_t slc_mpam_monitor_data_show(struct config_item *item,
		char *page)
{
	ssize_t len = 0;
	bool is_captured = false;
	uint32_t retry_cnt = 0;
	struct msc_query query;
	union mon_values mon_data;
	struct slc_mpam_item *pm_item = get_pm_item(item);
	uint64_t last_capture_time;
	uint32_t num_cache_lines = 0;
	uint64_t num_rd_misses = 0, slc_fe_bytes = 0, slc_be_bytes = 0;

	set_msc_query(&query, get_pm_item(item));

	if (!pm_item->cap_mon_enabled && !pm_item->miss_mon_enabled &&
			!pm_item->fe_mon_enabled && !pm_item->be_mon_enabled)
		return 0;

	do {
		last_capture_time = 0;

		if (pm_item->cap_mon_enabled) {
			msc_system_mon_alloc_info(SLC, &query, &mon_data);
			last_capture_time = mon_data.capacity.last_capture_time;
			num_cache_lines = mon_data.capacity.num_cache_lines;
			is_captured = true;
		}

		if (pm_item->miss_mon_enabled) {
			msc_system_mon_read_miss_info(SLC, &query, &mon_data);
			if (!last_capture_time)
				last_capture_time = mon_data.misses.last_capture_time;
			else if (last_capture_time != mon_data.misses.last_capture_time)
				continue;
			num_rd_misses = mon_data.misses.num_rd_misses;
			is_captured = true;
		}

		if (pm_item->fe_mon_enabled) {
			msc_system_mon_fe_bw_info(SLC, &query, &mon_data);
			if (!last_capture_time)
				last_capture_time = mon_data.fe_stats.last_capture_time;
			else if (last_capture_time != mon_data.fe_stats.last_capture_time)
				continue;
			slc_fe_bytes = mon_data.fe_stats.slc_fe_bytes;
			is_captured = true;
		}

		if (pm_item->be_mon_enabled) {
			msc_system_mon_be_bw_info(SLC, &query, &mon_data);
			if (!last_capture_time)
				last_capture_time = mon_data.be_stats.last_capture_time;
			else if (last_capture_time != mon_data.be_stats.last_capture_time)
				continue;
			slc_be_bytes = mon_data.be_stats.slc_be_bytes;
			is_captured = true;
		}

		if (is_captured)
			break;

	} while (retry_cnt++ < MAX_RETRY_CNT);

	if (retry_cnt >= MAX_RETRY_CNT)
		return scnprintf(page, PAGE_SIZE, "Failed to get consistent monitor data\n");

	len = scnprintf(page, PAGE_SIZE,
			"timestamp=%llu,", last_capture_time);
	if (pm_item->cap_mon_enabled)
		len += scnprintf(page + len, PAGE_SIZE - len,
			"cap=%u,", num_cache_lines);
	if (pm_item->miss_mon_enabled)
		len += scnprintf(page + len, PAGE_SIZE - len,
			"miss=%llu,", num_rd_misses);
	if (pm_item->fe_mon_enabled)
		len += scnprintf(page + len, PAGE_SIZE - len,
			"fe=%llu,", slc_fe_bytes);
	if (pm_item->be_mon_enabled)
		len += scnprintf(page + len, PAGE_SIZE - len,
			"be=%llu,", slc_be_bytes);

	len -= 1;
	len += scnprintf(page + len, PAGE_SIZE - len, "\n");

	return len;
}
CONFIGFS_ATTR_RO(slc_mpam_, monitor_data);

static ssize_t slc_mpam_available_gear_show(struct config_item *item,
		char *page)
{
	int i, gear_num;
	ssize_t len = 0;
	union slc_partid_capability_def *partid_cap;
	uint32_t slc_firmware_ver = 0;
	struct slc_partid_capability *v0_cap = NULL;
	struct slc_partid_capability_v1 *v1_cap = NULL;

	partid_cap = get_partid_cap(get_pm_item(item));
	if (!partid_cap)
		return -EINVAL;

	msc_system_get_mpam_version(SLC, &slc_firmware_ver);

	if (slc_firmware_ver == SLC_MPAM_VERSION_0) {
		v0_cap = &partid_cap->v0_cap;
		for (i = 0; i < v0_cap->num_gears; i++) {
			gear_num = v0_cap->part_id_gears[i];
			len += scnprintf(page + len, PAGE_SIZE - len,
				"%d - %s\n", gear_num, gear_index[gear_num]);
		}
	} else {
		v1_cap = &partid_cap->v1_cap;
		for (gear_num = 0, i = 0; gear_num < v1_cap->num_gears &&
				i < sizeof(v1_cap->cap_cfg.gear_flds_bitmap) * 8; i++) {
			if ((BIT(i) & v1_cap->cap_cfg.gear_flds_bitmap) == 0)
				continue;

			len += scnprintf(page + len, PAGE_SIZE - len,
					"%d - %d\n", gear_num++,
					i * v1_cap->cap_cfg.slc_bitfield_capacity);
		}
	}

	return len;
}
CONFIGFS_ATTR_RO(slc_mpam_, available_gear);

static struct configfs_attribute *slc_mpam_attrs[] = {
	&slc_mpam_attr_schemata,
	&slc_mpam_attr_monitor_data,
	&slc_mpam_attr_monitor_schemata,
	&slc_mpam_attr_available_gear,
	NULL,
};

static const struct config_item_type slc_mpam_item_type = {
	.ct_attrs	= slc_mpam_attrs,
};

static struct slc_mpam_item *slc_mpam_make_group(
		struct device *dev, const char *name)
{
	struct slc_mpam_item *item;

	item = devm_kzalloc(dev, sizeof(struct slc_mpam_item), GFP_KERNEL);
	if (!item)
		return ERR_PTR(-ENOMEM);

	config_group_init_type_name(&item->group, name,
				   &slc_mpam_item_type);

	return item;
}

static const struct config_item_type slc_mpam_base_type = {
	.ct_owner	= THIS_MODULE,
};

static int create_config_node(const char *name,
		struct device *dev,
		int client_id, int part_id,
		struct config_group *parent_group)
{
	int ret;
	struct slc_mpam_item *new_item;
	struct device_node *np = dev->of_node;
	uint32_t slc_firmware_ver = 0;
	struct slc_partid_capability_v1 *v1_cap = NULL;

	new_item = slc_mpam_make_group(dev, name);
	if (IS_ERR(new_item)) {
		pr_err("Error create group %s\n", name);
		return PTR_ERR(new_item);
	}
	new_item->client_id = client_id;
	new_item->part_id = part_id;

	msc_system_get_mpam_version(SLC, &slc_firmware_ver);
	if (slc_firmware_ver != SLC_MPAM_VERSION_0) {

		if (part_id != NO_PARTID) {
			v1_cap = (struct slc_partid_capability_v1 *)get_partid_cap(new_item);
			if (!v1_cap)
				return -EINVAL;

			if (v1_cap->mon_support & (1 << cap_mon_support))
				new_item->cap_mon_support = true;
			if (v1_cap->mon_support & (1 << read_miss_mon_support))
				new_item->miss_mon_support = true;
			if (v1_cap->mon_support & (1 << fe_mon_support))
				new_item->fe_mon_support = true;
			if (v1_cap->mon_support & (1 << be_mon_support))
				new_item->be_mon_support = true;
		}
	} else {
		new_item->cap_mon_support = true;
		new_item->miss_mon_support = true;
	}

	if (client_id == 0 && of_property_read_bool(np, "qcom,client-level-mon")) {
		if (part_id == NO_PARTID) {
			new_item->part_id = 0;
			new_item->fe_mon_support = true;
			new_item->be_mon_support = true;
		} else {
			new_item->fe_mon_support = false;
			new_item->be_mon_support = false;
		}
	}

	ret = configfs_register_group(parent_group, &new_item->group);
	if (ret) {
		pr_err("Error register group %s\n", name);
		return ret;
	}

	return 0;
}

static int slc_mpam_probe(struct platform_device *pdev)
{
	int ret, clientid, partid;
	char buf[CONFIGFS_ITEM_NAME_LEN];
	int client_cnt;
	const char *msc_name_dt;
	struct qcom_mpam_msc *qcom_mpam_msc;
	struct device_node *node, *sub_node;
	struct config_group *p_group, *sub_group;
	struct device_node *np = pdev->dev.of_node;

	qcom_mpam_msc = qcom_msc_lookup(SLC);
	if (!qcom_mpam_msc ||
		qcom_mpam_msc->mpam_available != MPAM_MONITRS_AVAILABLE)
		return -EPROBE_DEFER;

	p_group = platform_mpam_get_root_group();
	if (!p_group)
		return -EPROBE_DEFER;

	client_cnt = of_get_available_child_count(np);
	if (!client_cnt) {
		dev_err(&pdev->dev, "No client found\n");
		return -ENODEV;
	}

	ret = of_property_read_string(np, "qcom,msc-name", &msc_name_dt);
	if (ret) {
		dev_err(&pdev->dev, "MSC name not found\n");
		return ret;
	}

	root_group = configfs_register_default_group(p_group,
		msc_name_dt, &slc_mpam_base_type);
	if (IS_ERR(root_group)) {
		dev_err(&pdev->dev, "Error register group %s\n", msc_name_dt);
		return PTR_ERR(root_group);
	}

	for_each_available_child_of_node(np, node) {
		ret = of_property_read_u32(node, "qcom,client-id", &clientid);
		if (ret || clientid >= CLIENT_ID_MAX)
			continue;

		ret = of_property_read_string(node, "qcom,client-name", &msc_name_dt);
		if (ret || !msc_name_dt)
			continue;

		if (of_get_available_child_count(node) > 0) {
			sub_group = configfs_register_default_group(root_group,
				msc_name_dt, &slc_mpam_base_type);
			for_each_available_child_of_node(node, sub_node) {
				ret = of_property_read_u32(sub_node, "qcom,part-id", &partid);
				if (ret)
					continue;
				snprintf(buf, sizeof(buf), "partid%d", partid);
				if (create_config_node(buf, &pdev->dev, clientid,
						partid, sub_group))
					continue;
			}
		} else
			if (create_config_node(msc_name_dt, &pdev->dev,
					clientid, 0, root_group))
				continue;
	}

	return 0;
}

int slc_mpam_remove(struct platform_device *pdev)
{
	configfs_unregister_group(root_group);
	kfree(root_group);
	return 0;
}

static const struct of_device_id slc_mpam_table[] = {
	{ .compatible = "qcom,mpam-slc" },
	{}
};
MODULE_DEVICE_TABLE(of, slc_mpam_table);

static struct platform_driver slc_mpam_driver = {
	.driver = {
		.name = "mpam-slc",
		.of_match_table = slc_mpam_table,
		.suppress_bind_attrs = true,
	},
	.probe = slc_mpam_probe,
	.remove = slc_mpam_remove,
};

module_platform_driver(slc_mpam_driver);

MODULE_SOFTDEP("pre: mpam");
MODULE_DESCRIPTION("QCOM SLC MPAM driver");
MODULE_LICENSE("GPL");
