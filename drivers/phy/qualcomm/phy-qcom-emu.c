// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/* QSCRATCH registers */
#define HS_PHY_CTRL_REG		0x10
#define SW_SESSVLD_SEL		BIT(28)

/* HSPHY registers */
#define HS2_LOCAL_RESET_REG_ADDR	0x04
#define HS2_CLK_STATUS_ADDR		0x10
#define HS2_CLK_STATUS_SEL_ADDR		0x14
#define HS2_USB30_CTRL_ADDR		0x34
#define HS2_USB30_PHY_POWER_OFF		BIT(25)

struct qcusb_emu_phy {
	struct phy	*phy;
	struct device	*dev;
	void __iomem	*base;
	void __iomem	*qscratch_base;
	u32		*emu_init_seq;
	int		emu_init_seq_len;
};

static int qusb_emu_phy_init(struct phy *phy)
{
	struct qcusb_emu_phy *qphy = phy_get_drvdata(phy);
	u32 tmp;
	int i;

	// Marker for emulation!!
	pr_info("%s:%d phy init start\n", __func__, __LINE__);
	/* reset everything */
	writel_relaxed(0xffffffff, qphy->base + HS2_LOCAL_RESET_REG_ADDR);
	usleep_range(10000, 12000);

	/* power down HS phy */
	tmp = readl_relaxed(qphy->base + HS2_USB30_CTRL_ADDR) |
				HS2_USB30_PHY_POWER_OFF;
	writel_relaxed(tmp, qphy->base + HS2_USB30_CTRL_ADDR);
	usleep_range(10000, 12000);

	/* power up HS phy */
	tmp = readl_relaxed(qphy->base + HS2_USB30_CTRL_ADDR) &
				(~HS2_USB30_PHY_POWER_OFF);
	writel_relaxed(tmp, qphy->base + HS2_USB30_CTRL_ADDR);
	usleep_range(10000, 12000);

	writel_relaxed(0xfffffff3, qphy->base + HS2_LOCAL_RESET_REG_ADDR);
	usleep_range(10000, 12000);

	/* put phy out of reset */
	writel_relaxed(0xfffffff0, qphy->base + HS2_LOCAL_RESET_REG_ADDR);
	usleep_range(10000, 12000);

	/* selection of HS phy clock MMCM value */
	for (i = 0; i < qphy->emu_init_seq_len - 1; i = i+2) {
		pr_debug("write 0x%02x to 0x%02x\n",
				qphy->emu_init_seq[i], qphy->emu_init_seq[i+1]);
		writel_relaxed(qphy->emu_init_seq[i],
				qphy->base + qphy->emu_init_seq[i+1]);
		/* 10ms to ensure write propagates across bus */
		usleep_range(10000, 12000);
	}

	/* clear other reset */
	writel_relaxed(0x0, qphy->base + HS2_LOCAL_RESET_REG_ADDR);
	usleep_range(10000, 12000);

	/* clock select to read UTMI/ULPI clock */
	writel_relaxed(0x9, qphy->base + HS2_CLK_STATUS_SEL_ADDR);
	usleep_range(10000, 12000);
	pr_info("PHY UTMI/ULPI CLK frequency:%d MHz\n",
		(readl_relaxed(qphy->base + HS2_CLK_STATUS_ADDR) / 1000));

	if (qphy->qscratch_base) {
		/* Use UTMI VBUS signal from HW */
		tmp = readl_relaxed(qphy->qscratch_base + HS_PHY_CTRL_REG);
		tmp &= ~SW_SESSVLD_SEL;
		writel_relaxed(tmp, qphy->qscratch_base + HS_PHY_CTRL_REG);
	}

	//Marker for emulation !!
	pr_info("%s:%d phy init end\n", __func__, __LINE__);
	return 0;
}

static const struct phy_ops qphy_usb_phy_ops = {
	.init		= qusb_emu_phy_init,
	.owner		= THIS_MODULE,
};

static int qcusb_emu_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct qcusb_emu_phy *qphy;
	struct phy_provider *phy_provider;
	int ret, size;

	qphy = devm_kzalloc(dev, sizeof(*qphy), GFP_KERNEL);
	if (!qphy)
		return -ENOMEM;

	qphy->dev = dev;

	/* Map the primary PHY register base */
	qphy->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(qphy->base))
		return PTR_ERR(qphy->base);

	ret = of_get_property(dev->of_node, "qcom,emu-init-seq", &size);
	if (!ret || !size) {
		dev_err(dev, "emu-init-seq not specified\n");
		return -EINVAL;
	}

	if (size > PAGE_SIZE) {
		dev_err(dev, "emu-init-seq too large (%d bytes)\n", size);
		return -EINVAL;
	}

	qphy->emu_init_seq = devm_kzalloc(dev, size, GFP_KERNEL);
	if (!qphy->emu_init_seq)
		return -ENOMEM;

	qphy->emu_init_seq_len = (size / sizeof(*qphy->emu_init_seq));
	if (qphy->emu_init_seq_len % 2) {
		dev_err(dev, "invalid emu_init_seq_len, must be in <data,addr> pairs\n");
		return -EINVAL;
	}

	ret = of_property_read_u32_array(dev->of_node, "qcom,emu-init-seq",
			qphy->emu_init_seq, qphy->emu_init_seq_len);
	if (ret) {
		dev_err(dev, "could not read emu-init-seq, returned %d\n", ret);
		return ret;
	}

	qphy->qscratch_base = devm_platform_ioremap_resource_byname(pdev,
								    "qscratch_base");
	if (IS_ERR(qphy->qscratch_base)) {
		dev_dbg(dev, "qscratch_base not available: %ld\n",
			PTR_ERR(qphy->qscratch_base));
		qphy->qscratch_base = NULL;
	}

	qphy->phy = devm_phy_create(dev, NULL, &qphy_usb_phy_ops);
	if (IS_ERR(qphy->phy)) {
		ret = PTR_ERR(qphy->phy);
		dev_err(dev, "failed to create PHY: %d\n", ret);
		return ret;
	}

	phy_set_drvdata(qphy->phy, qphy);
	dev_set_drvdata(dev, qphy);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider)) {
		ret = PTR_ERR(phy_provider);
		dev_err(dev, "failed to register PHY provider: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct of_device_id emu_phy_dt_ids[] = {
	{ .compatible = "qcom,usb-emu-phy" },
	{ }
};

MODULE_DEVICE_TABLE(of, emu_phy_dt_ids);

static struct platform_driver qcusb_emu_phy_driver = {
	.probe		= qcusb_emu_phy_probe,
	.driver		= {
		.name	= "usb_emu_phy",
		.of_match_table = emu_phy_dt_ids,
	},
};

module_platform_driver(qcusb_emu_phy_driver);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. USB Emulation PHY driver");
MODULE_LICENSE("GPL");
