// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */


#include <linux/types.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/firmware.h>
#include <linux/string.h>
#include "fl7112.h"

#define FL7112_VERSION_NUM  ((uint32_t)0x0104410c)

static int fl7112_write(struct fl7112 *pdata, int reg,
		const u8 *buf, int size)
{
	struct i2c_client *client = pdata->i2c_client;
	u8 regAddr[2];

	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = 0,
		.len = size + 2,
		.buf = pdata->i2c_wbuf,
	};

	regAddr[0] = (reg >> 8) & 0xFF;
	regAddr[1] = reg & 0xFF;
	pdata->i2c_wbuf[0] = regAddr[0];
	pdata->i2c_wbuf[1] = regAddr[1];
	if (size > (FL7112_MTP_MAX_SIZE_OF_FIRMWARE_BLOCK - 1)) {
		pr_err("invalid write buffer size %d\n", size);
		return -EINVAL;
	}

	memcpy(pdata->i2c_wbuf + 2, buf, size);

	if (i2c_transfer(client->adapter, &msg, 1) < 1) {
		pr_err("i2c write failed\n");
		return -EIO;
	}

	return 0;
}

static int fl7112_write_byte(struct fl7112 *pdata, int reg, u8 value)
{
	struct i2c_client *client = pdata->i2c_client;
	u8 regAddr[2];

	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = 0,
		.len = 3,
		.buf = pdata->i2c_wbuf,
	};

	regAddr[0] = (reg >> 8) & 0xFF;
	regAddr[1] = reg & 0xFF;

	memset(pdata->i2c_wbuf, 0, FL7112_MTP_MAX_SIZE_OF_FIRMWARE_BLOCK);
	pdata->i2c_wbuf[0] = regAddr[0];
	pdata->i2c_wbuf[1] = regAddr[1];
	pdata->i2c_wbuf[2] = value;

	if (i2c_transfer(client->adapter, &msg, 1) < 1) {
		pr_err("i2c write failed\n");
		return -EIO;
	}

	return 0;
}

static int fl7112_read(struct fl7112 *pdata, int reg, char *buf, u32 size)
{
	struct i2c_client *client = pdata->i2c_client;
	u8 regAddr[2];

	struct i2c_msg msg[2] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = sizeof(regAddr),
			.buf = regAddr,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = size,
			.buf = pdata->i2c_rbuf,
		}
	};

	regAddr[0] = (reg >> 8) & 0xFF;
	regAddr[1] = reg & 0xFF;

	if (size > FL7112_MTP_MAX_SIZE_OF_FIRMWARE_BLOCK) {
		pr_err("invalid read buff size %d\n", size);
		return -EINVAL;
	}

	memset(pdata->i2c_wbuf, 0x0, FL7112_MTP_MAX_SIZE_OF_FIRMWARE_BLOCK);
	memset(pdata->i2c_rbuf, 0x0, FL7112_MTP_MAX_SIZE_OF_FIRMWARE_BLOCK);

	if (i2c_transfer(client->adapter, msg, 2) != 2) {
		pr_err("i2c read failed\n");
		return -EIO;
	}

	memcpy(buf, pdata->i2c_rbuf, size);

	return 0;
}

static bool i2cex_bitcheck(struct fl7112 *pdata, int i2coffset, int Bit, bool *IsOne)
{
	bool b_status = true;    /* error occur  */
	bool ret = true;
	u8 byteData = 0;

	b_status = fl7112_read(pdata, i2coffset, &byteData, 1);
	if (b_status) {
		/* read error */
		pr_err("fl7112 %s error line:%d\n", __func__, __LINE__);
		ret = false;
		goto Exit;
	}

	if (IS_BIT_SET(byteData, Bit))
		*IsOne = true;
	else
		*IsOne = false;
Exit:
	return ret;
}

static bool i2cex_writebytecheck(struct fl7112 *pdata, int i2coffset, u8 ByteData)
{
	bool b_status = true;
	bool ret = true;
	u8 byteDataReadBack;

	byteDataReadBack = 0;
	b_status = fl7112_read(pdata,
			i2coffset,
			&byteDataReadBack,
			1);
	if (b_status) {
		pr_err("fl7112 %s error line:%d\n", __func__, __LINE__);
		ret = false;
	}

	if (byteDataReadBack != ByteData) {
		b_status = fl7112_write_byte(pdata,
					i2coffset,
					ByteData);
		if (b_status) {
			pr_err("fl7112 %s error line:%d\n", __func__, __LINE__);
			ret = false;
		}

	}
	return ret;
}

static void fl7112_mtpwritelock(struct fl7112 *pdata)
{
	bool b_status;
	bool isOne;

	/* check if bit 3 is set */
	b_status = i2cex_bitcheck(pdata,
			FL7112_FIRMWARE_INTERNAL_REGISTER_0X9010,
			3,
			&isOne);
	/* bit is set */
	if (isOne) {
	/* write [0X9010] 0x0 to enable Write Protect */
		b_status = fl7112_write_byte(pdata,
					FL7112_FIRMWARE_INTERNAL_REGISTER_0X9010,
					0x00);
		if (b_status)
		/* write error */
			pr_err("fl7112 %s error line:%d write [0X9010] failed\n",
				__func__, __LINE__);
	}
}

static bool i2cex_bitset(struct fl7112 *pdata,
	int i2coffset, int bit)
{
	bool b_status = true;
	bool ret = true;
	u8 byteData = 0;

	b_status = fl7112_read(pdata,
			i2coffset,
			&byteData,
			1);
	if (b_status) {
		pr_err("fl7112 %s error line:%d read NG\n", __func__, __LINE__);
		ret = false;
		goto Exit;
	}

	BIT_SET(byteData, bit);

	b_status = fl7112_write_byte(pdata,
			i2coffset,
			byteData);
	if (b_status) {
		if (i2coffset != FL7112_FIRMWARE_INTERNAL_REGISTER_0XA003) {
			pr_err("fl7112 %s error line:%d write NG\n", __func__, __LINE__);
			ret = false;
		}
	}

Exit:
	return ret;
}

bool i2cex_bitclear(struct fl7112 *pdata,
	int i2coffset, int bit)
{
	bool b_status;
	u8 byteData;

	byteData = 0;

	b_status = fl7112_read(pdata,
			i2coffset,
			&byteData,
			1);
	if (b_status) {
		pr_err("fl7112 %s error line:%d read NG\n", __func__, __LINE__);
		b_status = false;
		goto Exit;
	}

	byteData &= ~(1 << bit);

	b_status = fl7112_write_byte(pdata,
			i2coffset,
			byteData);
	if (b_status) {
		pr_err("fl7112 %s error line:%d write NG\n", __func__, __LINE__);
	b_status = false;
	}

Exit:
	return b_status;
}

static bool fl7112_mtp_block_setup(struct fl7112 *pdata,
	u8 *block_current, int Address)
{
	bool b_status = true;
	bool ret = true;
	int block;

	block = (u8)(Address / FL7112_MTP_BLOCK_SIZE);
	if (*block_current != block) {
		u8 byteData;
		*block_current = block;
		byteData = 0;
		b_status = fl7112_read(pdata,
					FL7112_FIRMWARE_INTERNAL_REGISTER_0X9003, &byteData, 1);
		if (b_status) {
			pr_err("fl7112 %s error line:%d\n", __func__, __LINE__);
			ret = false;
			goto Exit;
		}
		byteData &= 0x0F;
		byteData |= (*block_current << 4);
		b_status = fl7112_write_byte(pdata,
					FL7112_FIRMWARE_INTERNAL_REGISTER_0X9003,
					byteData);
		if (b_status) {
			pr_err("fl7112 %s error line:%d\n", __func__, __LINE__);
			ret = false;
			goto Exit;
		}
	} else {
		b_status = true;
	}
Exit:
	return ret;
}

static bool fl7112_mtpwriteunlock(struct fl7112 *pdata)
{
	bool b_status = true;
	bool ret = true;
	bool isOne;

	b_status = i2cex_bitcheck(pdata,
			FL7112_FIRMWARE_INTERNAL_REGISTER_0X9010,
			3,
			&isOne);
	if (!b_status) {
		pr_err("fl7112 %s error line:%d bit check NG!\n", __func__, __LINE__);
		ret = false;
		goto Exit;
	}

	/* bit 3 not set */
	if (!isOne) {
		b_status = fl7112_write_byte(pdata,
				FL7112_FIRMWARE_INTERNAL_REGISTER_0X9010,
				0x55);
		if (b_status) {
			pr_err("fl7112 %s error line:%d 0x55\n", __func__, __LINE__);
			ret = false;
			goto Exit;
		}

		b_status = fl7112_write_byte(pdata,
				FL7112_FIRMWARE_INTERNAL_REGISTER_0X9010,
				0xAA);
		if (b_status) {
			pr_err("fl7112 %s error line:%d 0xAA\n", __func__, __LINE__);
			ret = false;
			goto Exit;
		}

		b_status = fl7112_write_byte(pdata,
				FL7112_FIRMWARE_INTERNAL_REGISTER_0X9010,
				0xFF);
		if (b_status) {
			pr_err("fl7112 %s error line:%d 0xFF\n", __func__, __LINE__);
			ret = false;
			goto Exit;
		}

		b_status = fl7112_write_byte(pdata,
				FL7112_FIRMWARE_INTERNAL_REGISTER_0X9010,
				0x18);
		if (b_status) {
			pr_err("fl7112 %s error line:%d 0x18\n", __func__, __LINE__);
			ret = false;
			goto Exit;
		}
	}
Exit:
	return ret;
}

static bool fl7112_mtpcheckstatus(
	struct fl7112 *pdata,
	bool *IsError,
	bool *IsBusy
)
{
	bool b_status;
	bool ret = true;
	u8 byteData;
	int retryMax;
	int retry;
	bool isError;
	bool isBusy;

	retryMax = FL7112_MTP_CHECK_STATUS_RETRY_MAX;
	b_status = true;
	isError = false;
	isBusy = false;

	for (retry = 0; retry < retryMax; retry++) {
		byteData = 0;

		/* read 0x9001  */
		b_status = fl7112_read(pdata,
					FL7112_FIRMWARE_INTERNAL_REGISTER_0X9001, &byteData, 1);
		if (b_status) {
			pr_err("%s error line:%d\n", __func__, __LINE__);
			ret = false;
			goto Exit;
		}

		/* check error */
		if (IS_BIT_SET(byteData, 6))
			isError = true;
		else
			isError = false;

		/* check busy */
		if (IS_BIT_SET(byteData, 7))
			isBusy = true;
		else
			isBusy = false;

		*IsError = isError;
		*IsBusy = isBusy;

		if (isError) {
			/* Error Bit */
			ret = false;
			goto Exit;
		}

		if (!isBusy) {
			/* Good */
			ret = true;
			break;
		}
		/* Busy */
		if (retry == (retryMax - 1)) {
			ret = false;
			break;
		}
	}
Exit:
	return ret;
}

static void fl7112_mtppowermodedisable(struct fl7112 *pdata)
{
	bool b_status;

	/* write [0X9000]  0x71  */
	b_status = i2cex_writebytecheck(pdata,
				FL7112_FIRMWARE_INTERNAL_REGISTER_0X9000,
				0x71);
	if (!b_status)
		pr_err("fl7112 %s error line:%d Power Saving Disable failed\n", __func__, __LINE__);
}

bool FL7112_MtpWrite_Firmware_Recovery(
	struct fl7112 *pdata,
	int Address,
	u32 DataBuffer,
	bool *IsError,
	bool *IsBusy
)
{
	bool b_status;
	bool ret = true;
	bool isError = false;
	bool isBusy = false;
	union dword_mapping dwordMappingReadBack;
	union dword_mapping *dwordMapping;
	int indexOfByte;

	/* MTP Read Back */
	fl7112_mtpwritelock(pdata);

	dwordMappingReadBack.Value = 0;
	b_status = fl7112_read(pdata,
			Address,
			(char *)&dwordMappingReadBack.Value,
			4);
	if (b_status) {
		pr_err("fl7112 %s error line:%d\n", __func__, __LINE__);
		ret = false;
		goto Exit;
	}

	b_status = fl7112_mtpwriteunlock(pdata);
	if (!b_status) {
		pr_err("fl7112 %s error line:%d\n", __func__, __LINE__);
		ret = false;
		goto Exit;
	}

	/* Write each bytes */
	dwordMapping = (union dword_mapping *)&DataBuffer;
	for (indexOfByte = 0; indexOfByte < 4; indexOfByte++) {
		/* Low 4 */
		dwordMappingReadBack.ByteData[indexOfByte] &= 0xF0;
		dwordMappingReadBack.ByteData[indexOfByte] |=
			(dwordMapping->ByteData[indexOfByte] & 0x0F);

		b_status = fl7112_write(pdata,
				Address,
				(u8 *)&dwordMappingReadBack.Value,
				4);
		if (b_status) {
			pr_err("fl7112 %s error line:%d  fl7112_write failed\n",
				__func__, __LINE__);
			ret = false;
			goto Exit;
		}

		isError = false;
		isBusy = false;
		b_status = fl7112_mtpcheckstatus(pdata,
						&isError,
						&isBusy);
		if (!b_status) {
			pr_err("fl7112 %s error line:%d\n", __func__, __LINE__);
			ret = false;
			goto Exit;
		}

		/* Full */
		dwordMappingReadBack.ByteData[indexOfByte] = dwordMapping->ByteData[indexOfByte];

		b_status = fl7112_write(pdata,
				Address,
				(u8 *)&dwordMappingReadBack.Value,
				4);
		if (b_status) {
			pr_err("fl7112 %s error line:%d  fl7112_write failed\n",
				__func__, __LINE__);
			ret = false;
			goto Exit;
		}

		isError = false;
		isBusy = false;
		b_status = fl7112_mtpcheckstatus(pdata,
						&isError,
						&isBusy);
		if (!b_status) {
			pr_err("fl7112 %s error line:%d\n", __func__, __LINE__);
			ret = false;
			goto Exit;
		}
	}

Exit:

	*IsError = isError;
	*IsBusy = isBusy;

	return ret;
}

static bool fl7112_mtpwrite_firmware(
	struct fl7112 *pdata,
	int Address,
	const u8 *DataBuffer,
	int DataBufferLength,
	bool *IsError,
	bool *IsBusy
)
{
	bool b_status;
	bool ret = true;
	int indexOfDword;
	const u8 *byteData = DataBuffer;
	u8 block_current;
	int addressOfMtp;
	int offset;
	int offset_in_block;
	bool isError;
	bool isBusy;

	isError = false;
	isBusy = false;

	fl7112_mtppowermodedisable(pdata);

	b_status = fl7112_mtpwriteunlock(pdata);
	if (!b_status) {
		pr_err("fl7112 %s error line:%d\n", __func__, __LINE__);
		ret = false;
		goto Exit;
	}

	offset = Address;
	block_current = 0xFF;
	for (indexOfDword = 0; indexOfDword < DataBufferLength; indexOfDword += 4) {
		b_status = fl7112_mtp_block_setup(pdata,
				&block_current,
				offset);
	if (!b_status) {
		pr_err("fl7112 %s error line:%d\n", __func__, __LINE__);
		ret = false;
		goto Exit;
	}

	offset_in_block = offset % FL7112_MTP_BLOCK_SIZE;
	addressOfMtp = FL7112_FIRMWARE_INTERNAL_REGISTER_0X5000 + offset_in_block;
	b_status = fl7112_write(pdata,
			addressOfMtp,
			byteData,
			4);
	if (b_status) {
		pr_err("fl7112 %s error line:%d  fl7112_write failed\n", __func__, __LINE__);
		ret = false;
		goto Exit;
	}
	isError = false;
	isBusy = false;
	b_status = fl7112_mtpcheckstatus(pdata,
					&isError,
					&isBusy);
#if FL7112_MTP_RECOVERY
	if (!b_status && isError) {
		union dword_mapping *dwordMapping;
		int indexOfRecovery;

		dwordMapping = (union dword_mapping *)byteData;

	for (indexOfRecovery = 0;
			indexOfRecovery < FL7112_MTP_WRITE_RECOVERY_MAX;
			indexOfRecovery++) {
		b_status = FL7112_MtpWrite_Firmware_Recovery(pdata,
				addressOfMtp,
				dwordMapping->Value,
				&isError,
				&isBusy);
		if (b_status)
			break;
	}
	if (!b_status) {
		pr_err("fl7112 %s error line:%d\n", __func__, __LINE__);
		ret = false;
		goto Exit;
	}
	}
#else
	if (!b_status) {
		pr_err("fl7112 %s error line:%d\n", __func__, __LINE__);
		ret = false;
		goto Exit;
	}
#endif
	byteData += 4;
	offset += 4;
	}

Exit:
	*IsError = isError;
	*IsBusy = isBusy;

	return ret;
}

static void fl7112_reset(struct fl7112 *pdata)
{
	/* set bit3 for register [0xA003] */
	(void)i2cex_bitset(pdata, FL7112_FIRMWARE_INTERNAL_REGISTER_0XA003, 3);
}

void fl7112_chip_reset(struct fl7112 *pdata)
{
	/*  LT7211_RST - H |  gpio_40 - L */
	gpio_direction_output(pdata->reset_pin, 0);
	msleep(20);
	/*  LT7211_RST - L |  gpio_40 - H */
	gpio_direction_output(pdata->reset_pin, 1);
	/* sleep */
	msleep(100);
	/*  LT7211_RST - H |  gpio_40 - L */
	gpio_direction_output(pdata->reset_pin, 0);
}

static int fl7112_parse_dt(struct device *dev, struct fl7112 *pdata)
{
	int ret = 0;
	struct device_node *np = dev->of_node;

	/* reset gpio */
	pdata->reset_pin = of_get_named_gpio(np, "fl7112,reset-gpio", 0);
	if (!gpio_is_valid(pdata->reset_pin)) {
		dev_err(dev, "%s: gpio %d is invalid!\n", __func__, pdata->reset_pin);
		return -EINVAL;
	}

	/* pwr 1v2 */
	pdata->pwr_1v2 = of_get_named_gpio(np, "fl7112,pwr-1v2", 0);
	if (!gpio_is_valid(pdata->pwr_1v2)) {
		dev_err(dev, "%s: gpio %d is invalid!\n", __func__, pdata->pwr_1v2);
		return -EINVAL;
	}

	/* pwr 3v3 */
	pdata->pwr_3v3 = of_get_named_gpio(np, "fl7112,pwr-3v3", 0);
	if (!gpio_is_valid(pdata->pwr_3v3)) {
		dev_err(dev, "%s: gpio %d is invalid!\n", __func__, pdata->pwr_3v3);
		return -EINVAL;
	}

	return ret;
}

static int fl7112_gpio_configure(struct fl7112 *pdata, bool on)
{
	int ret = 0;

	if (!gpio_is_valid(pdata->pwr_3v3)) {
		pr_err("Invalid GPIO for pwr_3v3\n");
		return -EINVAL;
	}

	if (!gpio_is_valid(pdata->pwr_1v2)) {
		pr_err("Invalid GPIO for pwr_1v2\n");
		return -EINVAL;
	}

	if (on) {
		ret = gpio_request(pdata->pwr_3v3,
			"fl7112-3p3-gpio");
	if (ret) {
		pr_err("fl7112 3p3 gpio request failed\n");
		goto error;
	}

	ret = gpio_direction_output(pdata->pwr_3v3, 0);
	if (ret) {
		pr_err("fl7112 3p3 en gpio direction failed\n");
		goto pwr_3p3_error;
	}

	ret = gpio_request(pdata->pwr_1v2,
		"fl7112-1p2-gpio");
	if (ret) {
		pr_err("fl7112 1p2 gpio request failed\n");
		goto pwr_3p3_error;
	}

	ret = gpio_direction_output(pdata->pwr_1v2, 0);
	if (ret) {
		pr_err("fl7112 1p2 gpio direction failed\n");
		goto pwr_1p2_error;
	}

		ret = gpio_request(pdata->reset_pin,
			"fl7112-reset-gpio");
		if (ret) {
			pr_err("fl7112 reset gpio request failed\n");
			goto pwr_1p2_error;
		}

		ret = gpio_direction_output(pdata->reset_pin, 1);
		if (ret) {
			pr_err("fl7112 reset gpio direction failed\n");
			goto reset_error;
		}
	} else {
		gpio_free(pdata->reset_pin);
		if (gpio_is_valid(pdata->pwr_1v2))
			gpio_free(pdata->pwr_1v2);
		if (gpio_is_valid(pdata->pwr_3v3))
			gpio_free(pdata->pwr_3v3);
	}

	return ret;

reset_error:
	gpio_free(pdata->reset_pin);
pwr_1p2_error:
	gpio_free(pdata->pwr_1v2);
pwr_3p3_error:
	gpio_free(pdata->pwr_3v3);
error:
	return ret;
}

static void fl7112_enable_vreg(struct fl7112 *pdata, int enable)
{
	if (enable) {
		if (gpio_is_valid(pdata->pwr_3v3))
			gpio_set_value(pdata->pwr_3v3, 1);

		if (gpio_is_valid(pdata->pwr_1v2))
			gpio_set_value(pdata->pwr_1v2, 1);
	} else {
		if (gpio_is_valid(pdata->pwr_3v3))
			gpio_set_value(pdata->pwr_3v3, 0);

		if (gpio_is_valid(pdata->pwr_1v2))
			gpio_set_value(pdata->pwr_1v2, 0);
	}
}

bool FL7112_MtpWrite_InformationBlock_Recovery(
	struct fl7112 *pdata,
	int Address,
	u32 Data,
	bool *IsError,
	bool *IsBusy
)
{
	bool b_status;
	bool ret = true;
	bool isError = false;
	bool isBusy = false;
	union dword_mapping dwordMappingReadBack;
	union dword_mapping *dwordMapping;
	int indexOfByte;

	/* MTP Read Back */
	fl7112_mtpwritelock(pdata);

	dwordMappingReadBack.Value = 0;
	b_status = fl7112_read(pdata,
			Address,
			(char *)&dwordMappingReadBack.Value,
			4);
	if (b_status) {
		pr_err("fl7112 %s error line:%d\n", __func__, __LINE__);
		ret = false;
		goto Exit;
	}

	b_status = fl7112_mtpwriteunlock(pdata);
	if (!b_status) {
		pr_err("fl7112 %s error line:%d\n", __func__, __LINE__);
		ret = false;
		goto Exit;
	}

	/* Write each bytes */
	dwordMapping = (union dword_mapping *)&Data;
	for (indexOfByte = 0; indexOfByte < 4; indexOfByte++) {
		/* Low 4 */
		dwordMappingReadBack.ByteData[indexOfByte] &= 0xF0;
		dwordMappingReadBack.ByteData[indexOfByte] |=
			dwordMapping->ByteData[indexOfByte] & 0x0F;

		b_status = fl7112_write(pdata,
				Address,
				(u8 *)&dwordMappingReadBack.Value,
				4);
		if (b_status) {
			pr_err("fl7112 %s error line:%d  fl7112_write failed\n",
				__func__, __LINE__);
			ret = false;
			goto Exit;
		}

		isError = false;
		isBusy = false;
		b_status = fl7112_mtpcheckstatus(pdata,
					&isError,
					&isBusy);
		if (!b_status) {
			pr_err("fl7112 %s error line:%d\n", __func__, __LINE__);
			ret = false;
			goto Exit;
		}

		/* Full */
		dwordMappingReadBack.ByteData[indexOfByte] = dwordMapping->ByteData[indexOfByte];

		b_status = fl7112_write(pdata,
				Address,
				(u8 *)&dwordMappingReadBack.Value,
				4);
		if (b_status) {
			pr_err("fl7112 %s error line:%d  fl7112_write failed\n",
				__func__, __LINE__);
			ret = false;
			goto Exit;
		}

		isError = false;
		isBusy = false;
		b_status = fl7112_mtpcheckstatus(pdata,
						&isError,
						&isBusy);
		if (!b_status) {
			pr_err("fl7112 %s error line:%d\n", __func__, __LINE__);
			ret = false;
			goto Exit;
		}
	}

Exit:

	*IsError = isError;
	*IsBusy = isBusy;

	return ret;
}

bool fl7112_mtpwrite_informationblock(
	struct fl7112 *pdata,
	int Address,
	u8 *DataBuffer,
	int DataBufferLength,
	bool *IsError,
	bool *IsBusy
)
{
	bool b_status;
	bool ret = true;
	int indexOfDword;
	u8 *byteData;
	bool isError;
	bool isBusy;
	int addressOfMtp;

	isError = false;
	isBusy = false;

	fl7112_mtppowermodedisable(pdata);

	b_status = fl7112_mtpwriteunlock(pdata);
	if (!b_status) {
		pr_err("fl7112 %s error line:%d Write Unlock NG!\n", __func__, __LINE__);
		ret = false;
		goto Exit;
	}

	byteData = DataBuffer;
	for (indexOfDword = 0; indexOfDword < DataBufferLength; indexOfDword += 4) {
		addressOfMtp = Address + indexOfDword;
		b_status = fl7112_write(pdata,
				addressOfMtp,
				byteData,
				4);
		if (b_status) {
			pr_err("fl7112 %s error line:%d\n", __func__, __LINE__);
			ret = false;
			goto Exit;
		}

		isError = false;
		isBusy = false;
		b_status = fl7112_mtpcheckstatus(pdata,
					&isError,
					&isBusy);
		#if FL7112_MTP_RECOVERY
		if (!b_status && isError) {
			union dword_mapping *dwordMapping;
			int indexOfRecovery;

			dwordMapping = (union dword_mapping *)byteData;

			for (indexOfRecovery = 0;
					indexOfRecovery < FL7112_MTP_WRITE_RECOVERY_MAX;
					indexOfRecovery++) {
				b_status = FL7112_MtpWrite_InformationBlock_Recovery(pdata,
								addressOfMtp,
								dwordMapping->Value,
								&isError,
								&isBusy);
				if (b_status)
					break;
			}
			if (!b_status) {
				pr_err("fl7112 %s error line:%d\n", __func__, __LINE__);
				ret = false;
				goto Exit;
			}
		}
		#else
		if (!b_status) {
			pr_err("fl7112 %s error line:%d state NG!\n", __func__, __LINE__);
			ret = false;
			goto Exit;
		}
		#endif
		byteData += 4;
	}

Exit:
	*IsError = isError;
	*IsBusy = isBusy;
	return ret;
}

bool fl7112_mtpread_informationblock(
	struct fl7112 *pdata,
	int Address,
	u8 *DataBuffer,
	int DataBufferLength
)
{
	bool b_status = true;
	bool ret = true;
	int indexOfDword;
	u8 *byteData = NULL;
	int addressOfMtp;

	/* Disable Power Saving */
	fl7112_mtppowermodedisable(pdata);

	/* Write Protect Enable */
	fl7112_mtpwritelock(pdata);

	byteData = DataBuffer;
	for (indexOfDword = 0; indexOfDword < DataBufferLength; indexOfDword += 4) {
		addressOfMtp = Address + indexOfDword;
		/* Read the information block data in the MTP memory */
		b_status = fl7112_read(pdata,
				addressOfMtp,
				byteData,
				4);
		if (b_status) {
			pr_err("fl7112 %s error line:%d\n", __func__, __LINE__);
			ret = false;
			goto Exit;
		}

		byteData += 4;
	}

Exit:
	return ret;
}

static bool fl7112_firmware_upgrade(struct fl7112 *pdata,
			const struct firmware *cfg)
{
	int data_len = (int)cfg->size;
	bool b_status = false;
	bool ret = true;
	u8 *dataBufferInformationBlock = NULL;
	u8 *dataBufferFirmwareReadBack = NULL;
	u8 *dataBufferInformationBlockReadBack = NULL;
	bool isError = false;
	bool isBusy = false;

	if (!cfg) {
		pr_err("fl7112 %s error line:%d param err : cfg\n", __func__, __LINE__);
		return false;
	}

	if (data_len != 0x6300) {
		pr_debug("fl7112 FW total size is incorrect\n");
		return false;
	}

	dataBufferInformationBlock =  (u8 *)cfg->data;
	dataBufferInformationBlock += FL7112_MTP_MAX_SIZE_OF_FIRMWARE_BLOCK;
	dataBufferFirmwareReadBack = kzalloc(FL7112_MTP_MAX_SIZE_OF_FIRMWARE_BLOCK, GFP_KERNEL);
	if (!dataBufferFirmwareReadBack)
		return false;

	dataBufferInformationBlockReadBack =
		kzalloc(FL7112_MTP_MAX_SIZE_OF_INFORMATION_BLOCK, GFP_KERNEL);
	if (!dataBufferInformationBlockReadBack) {
		kfree(dataBufferFirmwareReadBack);
		return false;
	}

	pdata->fw_status = UPDATE_RUNNING;

	pr_debug("fl7112 Firmware total size 0x%x\n", data_len);

	/* 1. Hold MCU Reset */
	b_status = i2cex_bitset(pdata,
			FL7112_FIRMWARE_INTERNAL_REGISTER_0XA002,
			5);
	if (!b_status) {
		pr_err("fl7112 %s error line:%d Hold MCU Reset NG\n", __func__, __LINE__);
		ret = false;
		goto Exit;
	} else {
		pr_debug("fl7112 Hold MCU Reset OK!\n");
	}

	/* 2. Firmware write */
	b_status = fl7112_mtpwrite_firmware(pdata,
					0x0,
					cfg->data,
					FL7112_MTP_MAX_SIZE_OF_FIRMWARE_BLOCK,
	&isError, &isBusy);
	if (!b_status) {
		pr_debug("fl7112 firmware write NG!\n");
		ret = false;
		goto Exit;
	} else {
		pr_debug("fl7112 firmware write OK!\n");
	}

	/* 3. read back and compare firmware  */
	b_status = fl7112_mtpread_firmware(pdata,
				0x0,
				dataBufferFirmwareReadBack,
				FL7112_MTP_MAX_SIZE_OF_FIRMWARE_BLOCK);
	if (!b_status)
		pr_err("fl7112 %s error line:%d - read back firmware NG!\n", __func__, __LINE__);

	b_status = memcmp(cfg->data, dataBufferFirmwareReadBack,
		FL7112_MTP_MAX_SIZE_OF_FIRMWARE_BLOCK);
	if (!b_status) {
		pr_debug("fl7112 firmware Compare OK!\n");
	} else {
		pr_err("fl7112 %s error line:%d - firmware Compare NG!\n", __func__, __LINE__);
		ret = false;
		goto Exit;
	}

	/* 4. InformationBlock write */
	b_status = fl7112_mtpwrite_informationblock(pdata,
						FL7112_FIRMWARE_INTERNAL_REGISTER_0X9800,
						dataBufferInformationBlock,
						FL7112_MTP_MAX_SIZE_OF_INFORMATION_BLOCK,
						&isError,
						&isBusy);
	if (!b_status) {
		pr_err("fl7112 %s error line:%d - write Information Block NG!\n",
			__func__, __LINE__);
		ret = false;
		goto Exit;
	} else {
		pr_debug("fl7112 Information block write OK!\n");
	}

	/* 5. read back and compare informationblock */
	b_status = fl7112_mtpread_informationblock(pdata,
					FL7112_FIRMWARE_INTERNAL_REGISTER_0X9800,
					dataBufferInformationBlockReadBack,
					FL7112_MTP_MAX_SIZE_OF_INFORMATION_BLOCK
	);

	if (!b_status)
		pr_err("fl7112 %s error line:%d - read back informationblock NG!\n",
			__func__, __LINE__);

	b_status = memcmp(dataBufferInformationBlock,
			dataBufferInformationBlockReadBack,
			FL7112_MTP_MAX_SIZE_OF_INFORMATION_BLOCK);
	if (!b_status) {
		pr_debug("fl7112 InformationBlock Compare OK!\n");
	} else {
		pr_err("fl7112 %s error line:%d - InformationBlock Compare NG!\n",
			__func__, __LINE__);
		ret = false;
	}

	pr_debug("fl7112 FW upgrade success!\n");

Exit:
	/* Release MCU Reset */
	b_status = i2cex_bitclear(pdata,
			FL7112_FIRMWARE_INTERNAL_REGISTER_0XA002,
			5);

	kfree(dataBufferFirmwareReadBack);
	kfree(dataBufferInformationBlockReadBack);
	return ret;
}

static void fl7112_firmware_cb(const struct firmware *cfg, void *data)
{
	struct fl7112 *pdata = (struct fl7112 *)data;

	if (!cfg) {
		pr_err("fl7112 get firmware failed\n");
		return;
	}

	/* FW upgrade */
	if (!fl7112_firmware_upgrade(pdata, cfg)) {
		pr_err("fl7112 upgrade firmware failed\n");
		fl7112_enable_vreg(pdata, false);
		msleep(20);
		fl7112_enable_vreg(pdata, true);
		fl7112_chip_reset(pdata);
	} else {
		msleep(3000);
		/* reset chip */
		fl7112_reset(pdata);
	}

	/* release */
	release_firmware(cfg);
}

u8 fl7112_read_addr_info(struct fl7112 *pdata, int addr)
{
	u8 rDat = 0;
	bool b_status = true;

	b_status = fl7112_read(pdata, addr, &rDat, 1);
	if (b_status)
		pr_err("fl7112 read addr 0x%x failed, ret: %d\n", addr, b_status);
	else
		pr_debug("fl7112 read addr 0x%x : %x\n", addr, rDat);

	return rDat;
}

bool fl7112_mtpread_firmware(struct fl7112 *pdata, int address, u8 *DataBuffer,
	int DataBufferLength)
{
	bool b_status = true;
	bool ret = true;
	int index;
	u8 *byteData;
	int offset;
	int offset_in_block;
	u8 block_current;

	fl7112_mtppowermodedisable(pdata);

	fl7112_mtpwritelock(pdata);

	byteData = DataBuffer;
	offset = address;
	block_current = 0xFF;
	for (index = 0; index < DataBufferLength; index += 4) {

		b_status = fl7112_mtp_block_setup(pdata,
					&block_current,
					offset);
		if (!b_status) {
			pr_err("fl7112 %s error line:%d\n", __func__, __LINE__);
			ret = false;
			goto Exit;
		}

		offset_in_block = offset % FL7112_MTP_BLOCK_SIZE;
		b_status = fl7112_read(pdata,
				FL7112_FIRMWARE_INTERNAL_REGISTER_0X5000 + offset_in_block,
				byteData,
				4);
		if (b_status) {
			pr_err("fl7112 %s error line:%d\n", __func__, __LINE__);
			ret = false;
			goto Exit;
		}
		byteData += 4;
		offset += 4;
	}
Exit:
	return ret;
}

static bool fl7112_firmware_get_version(struct fl7112 *pdata, u8 *VersionString)
{
	bool b_status = true;

	b_status = fl7112_mtpread_firmware(pdata,
				FL7112_FIRMWARE_OFFSET_0X1000,
				VersionString,
				4);
	if (!b_status) {
		pr_err("%s error line:%d get version Failed!\n", __func__, __LINE__);
		return false;
	}

	return true;
}

bool fl7112_read_fw_version(struct fl7112 *pdata, uint32_t *Version)
{
	bool b_status = true;
	u8 VersionString[4] = {0};
	uint32_t ver = 0;

	b_status = fl7112_firmware_get_version(pdata, VersionString);
	if (!b_status) {
		pr_err("fl7112 %s error line:%d read version failed!\n", __func__, __LINE__);
		return false;
	}

	ver = ((VersionString[3] << 24) |
		(VersionString[2] << 16) |
		(VersionString[1] << 8) |
		VersionString[0]);
	*Version = ver;

	return true;
}

static void fl7112_fw_upgrade_work(struct work_struct *work)
{
	int32_t ret = 0;
	struct fl7112 *pdata = container_of(work, struct fl7112, wk);

	pr_debug("[fl7112] %s enter\n", __func__);

	if (!pdata) {
		pr_err("fl7112 %s : pdata is NULL\n", __func__);
		return;
	}

	ret = request_firmware_nowait(THIS_MODULE, true,
				"fl7112_fw.bin", &pdata->i2c_client->dev, GFP_KERNEL,
				pdata, fl7112_firmware_cb);
	if (ret < 0)
		pr_err("[fl7112] request firmware upgrade failed\n");
}

static int fl7112_probe(struct i2c_client *client)
{
	struct fl7112 *pdata;
	int ret = 0;

	if (!client) {
		pr_err("invalid input\n");
		return -EINVAL;
	}

	if (!client->dev.of_node) {
		dev_err(&client->dev, "invalid input: of_node is NULL\n");
		return -EINVAL;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "fl7112 device doesn't support I2C\n");
		return -ENODEV;
	}

	pdata = devm_kzalloc(&client->dev, sizeof(struct fl7112), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	ret = fl7112_parse_dt(&client->dev, pdata);
	if (ret) {
		dev_err(&client->dev, "Fl7112 Parse Device Tree NG! %d\n", ret);
		goto err_dt_parse;
	}

	pdata->dev = &client->dev;
	pdata->i2c_client = client;

	ret = fl7112_gpio_configure(pdata, true);
	if (ret) {
		dev_err(&client->dev, "failed to configure GPIOs\n");
		goto err_dt_parse;
	}

	fl7112_enable_vreg(pdata, true);

	fl7112_chip_reset(pdata);

	i2c_set_clientdata(client, pdata);
	dev_set_drvdata(&client->dev, pdata);

	/* init upgrade queue */
	pdata->wq = create_singlethread_workqueue("fl7112_wq");
	if (!pdata->wq) {
		dev_err(&client->dev, "fl7112 create workqueue failed!\n");
		goto err_sysfs_init;
	}
	/* init upgrade work */
	INIT_WORK(&(pdata->wk), fl7112_fw_upgrade_work);

	/* read fw version */
	if (fl7112_read_fw_version(pdata, &pdata->version)) {
		/* compare version */
		if (pdata->version != FL7112_VERSION_NUM) {
			pr_debug("%s: check version NG [0x%08x] expect version [0x%08x]\n",
				__func__, pdata->version, FL7112_VERSION_NUM);
			/* need to upgrade */
			queue_work(pdata->wq, &(pdata->wk));
		} else {
			/* version correct and nothing todo */
			pr_debug("%s: check version OK [0x%08x]\n", __func__, pdata->version);
			fl7112_reset(pdata);
		}
	} else {
		dev_err(&client->dev, "fl7112 read FW version failed!\n");
		goto err_sysfs_init;
	}

	return 0;

err_sysfs_init:
	fl7112_gpio_configure(pdata, false);
err_dt_parse:
	return ret;

}

static void fl7112_remove(struct i2c_client *client)
{
	struct fl7112 *pdata = i2c_get_clientdata(client);

	if ((!pdata) || (!pdata->wq))
		return;

	destroy_workqueue(pdata->wq);

	fl7112_gpio_configure(pdata, false);
}

static const struct of_device_id fl7112_of_match[] = {
	{ .compatible = "qcom,fl7112-fs8822" },
	{ }
};
MODULE_DEVICE_TABLE(of, fl7112_of_match);

static struct i2c_driver fl7112_driver = {
	.probe = fl7112_probe,
	.remove = fl7112_remove,
	.driver = {
		.name = "fl7112",
		.of_match_table = fl7112_of_match,
	},
};


module_i2c_driver(fl7112_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Parade, FL7112");
