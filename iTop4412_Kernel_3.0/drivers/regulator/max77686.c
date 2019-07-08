/*
 * max77686.c - Regulator driver for the Maxim 77686
 *
 * Copyright (C) 2011 Samsung Electronics
 * Chiwoong Byun <woong.byun@smasung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * This driver is based on max8997.c
 */

#include <linux/bug.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/max77686.h>
#include <linux/mfd/max77686-private.h>

#define PMIC_DEBUG KERN_INFO

struct max77686_data {
	struct device *dev;
	struct max77686_dev *iodev;
	int num_regulators;
	struct regulator_dev **rdev;
	int ramp_delay; /* in mV/us */

	bool buck1_gpiodvs;
	bool buck2_gpiodvs;
	bool buck5_gpiodvs;
	u8 buck1_vol[8];
	u8 buck2_vol[8];
	u8 buck5_vol[8];
	int buck125_gpios[3];
	int buck125_gpioindex;
	bool ignore_gpiodvs_side_effect;

	u8 saved_states[MAX77686_REG_MAX];
};

static inline void max77686_set_gpio(struct max77686_data *max77686)
{
	int set3 = (max77686->buck125_gpioindex) & 0x1;
	int set2 = ((max77686->buck125_gpioindex) >> 1) & 0x1;
	int set1 = ((max77686->buck125_gpioindex) >> 2) & 0x1;

	gpio_set_value(max77686->buck125_gpios[0], set1);
	gpio_set_value(max77686->buck125_gpios[1], set2);
	gpio_set_value(max77686->buck125_gpios[2], set3);
}

struct voltage_map_desc {
	int min;
	int max;
	int step;
	unsigned int n_bits;
};

/* Voltage maps in mV */
static const struct voltage_map_desc ldo_voltage_map_desc = {
	.min = 800,	.max = 3950,	.step = 50,	.n_bits = 6,
}; /* LDO3 ~ 5, 9 ~ 14, 16 ~ 26 */

static const struct voltage_map_desc ldo_low_voltage_map_desc = {
	.min = 800,	.max = 2375,	.step = 25,	.n_bits = 6,
}; /* LDO1 ~ 2, 6 ~ 8, 15 */

static const struct voltage_map_desc buck_dvs_voltage_map_desc = {
	.min = 600000,	.max = 3787500,	.step = 12500,	.n_bits = 8,
}; /* Buck2, 3, 4 (uV) */

static const struct voltage_map_desc buck_voltage_map_desc = {
	.min = 750,	.max = 3900,	.step = 50,	.n_bits = 6,
}; /* Buck1, 5 ~ 9 */

static const struct voltage_map_desc *reg_voltage_map[] = {
	[MAX77686_LDO1] = &ldo_low_voltage_map_desc,
	[MAX77686_LDO2] = &ldo_low_voltage_map_desc,
	[MAX77686_LDO3] = &ldo_voltage_map_desc,
	[MAX77686_LDO4] = &ldo_voltage_map_desc,
	[MAX77686_LDO5] = &ldo_voltage_map_desc,
	[MAX77686_LDO6] = &ldo_low_voltage_map_desc,
	[MAX77686_LDO7] = &ldo_low_voltage_map_desc,
	[MAX77686_LDO8] = &ldo_low_voltage_map_desc,
	[MAX77686_LDO9] = &ldo_voltage_map_desc,
	[MAX77686_LDO10] = &ldo_voltage_map_desc,
	[MAX77686_LDO11] = &ldo_voltage_map_desc,
	[MAX77686_LDO12] = &ldo_voltage_map_desc,
	[MAX77686_LDO13] = &ldo_voltage_map_desc,
	[MAX77686_LDO14] = &ldo_voltage_map_desc,
	[MAX77686_LDO15] = &ldo_low_voltage_map_desc,
	[MAX77686_LDO16] = &ldo_voltage_map_desc,
	[MAX77686_LDO17] = &ldo_voltage_map_desc,
	[MAX77686_LDO18] = &ldo_voltage_map_desc,
	[MAX77686_LDO19] = &ldo_voltage_map_desc,
	[MAX77686_LDO20] = &ldo_voltage_map_desc,
	[MAX77686_LDO21] = &ldo_voltage_map_desc,
	[MAX77686_LDO22] = &ldo_voltage_map_desc,
	[MAX77686_LDO23] = &ldo_voltage_map_desc,
	[MAX77686_LDO24] = &ldo_voltage_map_desc,
	[MAX77686_LDO25] = &ldo_voltage_map_desc,
	[MAX77686_LDO26] = &ldo_voltage_map_desc,
	[MAX77686_BUCK1] = &buck_voltage_map_desc,
	[MAX77686_BUCK2] = &buck_dvs_voltage_map_desc,
	[MAX77686_BUCK3] = &buck_dvs_voltage_map_desc,
	[MAX77686_BUCK4] = &buck_dvs_voltage_map_desc,
	[MAX77686_BUCK5] = &buck_voltage_map_desc,
	[MAX77686_BUCK6] = &buck_voltage_map_desc,
	[MAX77686_BUCK7] = &buck_voltage_map_desc,
	[MAX77686_BUCK8] = &buck_voltage_map_desc,
	[MAX77686_BUCK9] = &buck_voltage_map_desc,
	[MAX77686_EN32KHZ_AP] = NULL,
	[MAX77686_EN32KHZ_CP] = NULL,
};

static inline int max77686_get_rid(struct regulator_dev *rdev)
{
	return rdev_get_id(rdev);
}

static int max77686_get_voltage_unit(int rid)
{
	int unit = 0;

	switch (rid) {
	case MAX77686_BUCK2 ... MAX77686_BUCK4:
		unit = 1; /* BUCK2,3,4 is uV */
		break;
	default:
		unit = 1000;
		break;
	}

	return unit;
}

static int max77686_list_voltage(struct regulator_dev *rdev,
		unsigned int selector)
{
	const struct voltage_map_desc *desc;
	int rid = max77686_get_rid(rdev);
	int val;

	if (rid >= ARRAY_SIZE(reg_voltage_map) ||
			rid < 0)
		return -EINVAL;

	desc = reg_voltage_map[rid];
	if (desc == NULL)
		return -EINVAL;

	val = desc->min + desc->step * selector;
	if (val > desc->max)
		return -EINVAL;

	return val * max77686_get_voltage_unit(rid);
}

static int max77686_get_enable_register(struct regulator_dev *rdev,
		int *reg, int *mask, int *pattern)
{
	int rid = max77686_get_rid(rdev);

	switch (rid) {
	case MAX77686_LDO1 ... MAX77686_LDO26:
		*reg = MAX77686_REG_LDO1CTRL1 + (rid - MAX77686_LDO1);
		*mask = 0xC0;
		*pattern = 0xC0;
		break;
	case MAX77686_BUCK1:
		*reg = MAX77686_REG_BUCK1CTRL;
		*mask = 0x03;
		*pattern = 0x03;
		break;
	case MAX77686_BUCK2:
		*reg = MAX77686_REG_BUCK2CTRL1;
		*mask = 0x30;
		*pattern = 0x10;
		break;
	case MAX77686_BUCK3:
		*reg = MAX77686_REG_BUCK3CTRL1;
		*mask = 0x30;
		*pattern = 0x10;
		break;
	case MAX77686_BUCK4:
		*reg = MAX77686_REG_BUCK4CTRL1;
		*mask = 0x30;
		*pattern = 0x30;
		break;
	case MAX77686_BUCK5 ... MAX77686_BUCK9:
		*reg = MAX77686_REG_BUCK5CTRL + (rid - MAX77686_BUCK5)*2;
		*mask = 0x03;
		*pattern = 0x03;
		break;
	case MAX77686_EN32KHZ_AP ... MAX77686_EN32KHZ_CP:
		*reg = MAX77686_REG_32KHZ;
		*mask = 0x01 << (rid - MAX77686_EN32KHZ_AP);
		*pattern = 0x01 << (rid - MAX77686_EN32KHZ_AP);
		break;
	default:
		/* Not controllable or not exists */
		return -EINVAL;
	}

	return 0;
}

static int max77686_reg_is_enabled(struct regulator_dev *rdev)
{
	struct max77686_data *max77686 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max77686->iodev->i2c;
	int ret, reg, mask, pattern;
	u8 val;

	ret = max77686_get_enable_register(rdev, &reg, &mask, &pattern);
	if (ret == -EINVAL)
		return 1; /* "not controllable" */
	else if (ret)
		return ret;

	ret = max77686_read_reg(i2c, reg, &val);
	if (ret)
		return ret;

	printk(PMIC_DEBUG "%s: id=%d, ret=%d, val=%x, mask=%x, pattern=%x\n",
		__func__, max77686_get_rid(rdev), (val & mask) == pattern,
		val, mask, pattern);

	return (val & mask) == pattern;
}

static int max77686_reg_enable(struct regulator_dev *rdev)
{
	struct max77686_data *max77686 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max77686->iodev->i2c;
	int ret, reg, mask, pattern;

	ret = max77686_get_enable_register(rdev, &reg, &mask, &pattern);
	if (ret)
		return ret;

/*	printk(PMIC_DEBUG "%s: id=%d, reg=%x, mask=%x, pattern=%x\n",
		__func__, max77686_get_rid(rdev), reg, mask, pattern);
*/
	return max77686_update_reg(i2c, reg, pattern, mask);
}

static int max77686_reg_disable(struct regulator_dev *rdev)
{
	struct max77686_data *max77686 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max77686->iodev->i2c;
	int ret, reg, mask, pattern;

	ret = max77686_get_enable_register(rdev, &reg, &mask, &pattern);
	if (ret)
		return ret;

	printk(PMIC_DEBUG "%s: id=%d, reg=%x, mask=%x, pattern=%x\n",
		__func__, max77686_get_rid(rdev), reg, mask, pattern);

	return max77686_update_reg(i2c, reg, ~mask, mask);
}

static int max77686_get_voltage_register(struct regulator_dev *rdev,
		int *_reg, int *_shift, int *_mask)
{
	int rid = max77686_get_rid(rdev);
	int reg, shift = 0, mask = 0x3f;

	switch (rid) {
	case MAX77686_LDO1 ... MAX77686_LDO26:
		reg = MAX77686_REG_LDO1CTRL1 + (rid - MAX77686_LDO1);
		break;
	case MAX77686_BUCK1:
		reg = MAX77686_REG_BUCK1OUT;
		break;
	case MAX77686_BUCK2:
		reg = MAX77686_REG_BUCK2DVS1;
		mask = 0xff;
		break;
	case MAX77686_BUCK3:
		reg = MAX77686_REG_BUCK3DVS1;
		mask = 0xff;
		break;
	case MAX77686_BUCK4:
		reg = MAX77686_REG_BUCK4DVS1;
		mask = 0xff;
		break;
	case MAX77686_BUCK5 ... MAX77686_BUCK9:
		reg = MAX77686_REG_BUCK5OUT + (rid - MAX77686_BUCK5);
		break;
	default:
		return -EINVAL;
	}

	*_reg = reg;
	*_shift = shift;
	*_mask = mask;

	return 0;
}

static int max77686_get_voltage(struct regulator_dev *rdev)
{
	struct max77686_data *max77686 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max77686->iodev->i2c;
	int reg, shift, mask, ret;
	int rid = max77686_get_rid(rdev);
	u8 val;

	ret = max77686_get_voltage_register(rdev, &reg, &shift, &mask);
	if (ret)
		return ret;

	if ((rid == MAX77686_BUCK1 && max77686->buck1_gpiodvs) ||
			(rid == MAX77686_BUCK2 && max77686->buck2_gpiodvs) ||
			(rid == MAX77686_BUCK5 && max77686->buck5_gpiodvs))
		reg += max77686->buck125_gpioindex;

	ret = max77686_read_reg(i2c, reg, &val);
	if (ret)
		return ret;

	val >>= shift;
	val &= mask;

	printk(PMIC_DEBUG "%s: id=%d, reg=%x, mask=%x, val=%x\n",
		__func__, max77686_get_rid(rdev), reg, mask, val);

	return max77686_list_voltage(rdev, val);
}

static inline int max77686_get_voltage_proper_val(
		const struct voltage_map_desc *desc,
		int min_vol, int max_vol)
{
	int i = 0;

	if (desc == NULL)
		return -EINVAL;

	if (max_vol < desc->min || min_vol > desc->max)
		return -EINVAL;

	while (desc->min + desc->step * i < min_vol &&
			desc->min + desc->step * i < desc->max)
		i++;

	if (desc->min + desc->step * i > max_vol)
		return -EINVAL;

	if (i >= (1 << desc->n_bits))
		return -EINVAL;

	return i;
}

static int max77686_set_voltage(struct regulator_dev *rdev,
		int min_uV, int max_uV, unsigned *selector)
{
	struct max77686_data *max77686 = rdev_get_drvdata(rdev);
	struct i2c_client *i2c = max77686->iodev->i2c;
	int min_vol = min_uV, max_vol = max_uV, unit = 0;
	const struct voltage_map_desc *desc;
	int rid = max77686_get_rid(rdev);
	int reg, shift = 0, mask, ret;
	int i;
	u8 org;

	unit = max77686_get_voltage_unit(rid);
	min_vol /= unit;
	max_vol /= unit;

	desc = reg_voltage_map[rid];

	i = max77686_get_voltage_proper_val(desc, min_vol, max_vol);
	if (i < 0)
		return i;

	ret = max77686_get_voltage_register(rdev, &reg, &shift, &mask);
	if (ret)
		return ret;

	max77686_read_reg(i2c, reg, &org);
	org = (org & mask) >> shift;

	ret = max77686_update_reg(i2c, reg, i << shift, mask << shift);
	*selector = i;

	if (rid == MAX77686_BUCK2 || rid == MAX77686_BUCK3 ||
			rid == MAX77686_BUCK4) {
		/* If the voltage is increasing */
		if (org < i)
			udelay(DIV_ROUND_UP(desc->step * (i - org),
						max77686->ramp_delay));
	}

/*	printk(PMIC_DEBUG "%s: id=%d, reg=%x, mask=%x, org=%x, val=%x\n",
		__func__, max77686_get_rid(rdev), reg, mask, org, i);
*/
	return ret;
}

static struct regulator_ops max77686_ldo_ops = {
	.list_voltage		= max77686_list_voltage,
	.is_enabled		= max77686_reg_is_enabled,
	.enable			= max77686_reg_enable,
	.disable		= max77686_reg_disable,
	.get_voltage		= max77686_get_voltage,
	.set_voltage		= max77686_set_voltage,
	.set_suspend_enable	= max77686_reg_enable,
	.set_suspend_disable	= max77686_reg_disable,
};

static struct regulator_ops max77686_buck_ops = {
	.list_voltage		= max77686_list_voltage,
	.is_enabled		= max77686_reg_is_enabled,
	.enable			= max77686_reg_enable,
	.disable		= max77686_reg_disable,
	.get_voltage		= max77686_get_voltage,
	.set_voltage		= max77686_set_voltage,
	.set_suspend_enable	= max77686_reg_enable,
	.set_suspend_disable	= max77686_reg_disable,
};

static struct regulator_ops max77686_fixedvolt_ops = {
	.list_voltage		= max77686_list_voltage,
	.is_enabled		= max77686_reg_is_enabled,
	.enable			= max77686_reg_enable,
	.disable		= max77686_reg_disable,
	.set_suspend_enable	= max77686_reg_enable,
	.set_suspend_disable	= max77686_reg_disable,
};

#define regulator_desc_ldo(num)		{	\
	.name		= "LDO"#num,		\
	.id		= MAX77686_LDO##num,	\
	.ops		= &max77686_ldo_ops,	\
	.type		= REGULATOR_VOLTAGE,	\
	.owner		= THIS_MODULE,		\
}
#define regulator_desc_buck(num)		{	\
	.name		= "BUCK"#num,		\
	.id		= MAX77686_BUCK##num,	\
	.ops		= &max77686_buck_ops,	\
	.type		= REGULATOR_VOLTAGE,	\
	.owner		= THIS_MODULE,		\
}

static struct regulator_desc regulators[] = {
	regulator_desc_ldo(1),
	regulator_desc_ldo(2),
	regulator_desc_ldo(3),
	regulator_desc_ldo(4),
	regulator_desc_ldo(5),
	regulator_desc_ldo(6),
	regulator_desc_ldo(7),
	regulator_desc_ldo(8),
	regulator_desc_ldo(9),
	regulator_desc_ldo(10),
	regulator_desc_ldo(11),
	regulator_desc_ldo(12),
	regulator_desc_ldo(13),
	regulator_desc_ldo(14),
	regulator_desc_ldo(15),
	regulator_desc_ldo(16),
	regulator_desc_ldo(17),
	regulator_desc_ldo(18),
	regulator_desc_ldo(19),
	regulator_desc_ldo(20),
	regulator_desc_ldo(21),
	regulator_desc_ldo(22),
	regulator_desc_ldo(23),
	regulator_desc_ldo(24),
	regulator_desc_ldo(25),
	regulator_desc_ldo(26),
	regulator_desc_buck(1),
	regulator_desc_buck(2),
	regulator_desc_buck(3),
	regulator_desc_buck(4),
	regulator_desc_buck(5),
	regulator_desc_buck(6),
	regulator_desc_buck(7),
	regulator_desc_buck(8),
	regulator_desc_buck(9),
	{
		.name	= "EN32KHz AP",
		.id	= MAX77686_EN32KHZ_AP,
		.ops	= &max77686_fixedvolt_ops,
		.type	= REGULATOR_VOLTAGE,
		.owner	= THIS_MODULE,
	}, {
		.name	= "EN32KHz CP",
		.id	= MAX77686_EN32KHZ_CP,
		.ops	= &max77686_fixedvolt_ops,
		.type	= REGULATOR_VOLTAGE,
		.owner	= THIS_MODULE,
	},
};

static __devinit int max77686_pmic_probe(struct platform_device *pdev)
{
	struct max77686_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct max77686_platform_data *pdata = dev_get_platdata(iodev->dev);
	struct regulator_dev **rdev;
	struct max77686_data *max77686;
	struct i2c_client *i2c;
	int i, ret, size;
	u8 data = 0;

	printk(PMIC_DEBUG "%s\n", __func__);

	if (!pdata) {
		dev_err(pdev->dev.parent, "No platform init data supplied.\n");
		return -ENODEV;
	}

	max77686 = kzalloc(sizeof(struct max77686_data), GFP_KERNEL);
	if (!max77686)
		return -ENOMEM;

	size = sizeof(struct regulator_dev *) * pdata->num_regulators;
	max77686->rdev = kzalloc(size, GFP_KERNEL);
	if (!max77686->rdev) {
		kfree(max77686);
		return -ENOMEM;
	}

	rdev = max77686->rdev;
	max77686->dev = &pdev->dev;
	max77686->iodev = iodev;
	max77686->num_regulators = pdata->num_regulators;
	max77686->ramp_delay = 10;
	platform_set_drvdata(pdev, max77686);
	i2c = max77686->iodev->i2c;

	max77686_read_reg(i2c, MAX77686_REG_DEVICE_ID, &data);
	printk(PMIC_DEBUG "%s: DEVICE ID=0x%x\n", __func__, data);

	for (i = 0; i < pdata->num_regulators; i++) {
		const struct voltage_map_desc *desc;
		int id = pdata->regulators[i].id;

		desc = reg_voltage_map[id];
		if (desc) {
			regulators[id].n_voltages =
				(desc->max - desc->min) / desc->step + 1;

			printk(PMIC_DEBUG "%s: desc=%p, id=%d, n_vol=%d, max=%d, min=%d, step=%d\n",
					__func__, desc, id, regulators[id].n_voltages,
					desc->max, desc->min, desc->step);
		}

		rdev[i] = regulator_register(&regulators[id], max77686->dev,
				pdata->regulators[i].initdata, max77686);
		if (IS_ERR(rdev[i])) {
			ret = PTR_ERR(rdev[i]);
			dev_err(max77686->dev, "regulator init failed for %d\n",
					id);
			rdev[i] = NULL;
			goto err;
		}
	}

	return 0;
err:
	for (i = 0; i < max77686->num_regulators; i++)
		if (rdev[i])
			regulator_unregister(rdev[i]);

	kfree(max77686->rdev);
	kfree(max77686);

	return ret;
}

static int __devexit max77686_pmic_remove(struct platform_device *pdev)
{
	struct max77686_data *max77686 = platform_get_drvdata(pdev);
	struct regulator_dev **rdev = max77686->rdev;
	int i;

	for (i = 0; i < max77686->num_regulators; i++)
		if (rdev[i])
			regulator_unregister(rdev[i]);

	kfree(max77686->rdev);
	kfree(max77686);

	return 0;
}

static const struct platform_device_id max77686_pmic_id[] = {
	{ "max77686-pmic", 0},
	{ },
};
MODULE_DEVICE_TABLE(platform, max77686_pmic_id);

static struct platform_driver max77686_pmic_driver = {
	.driver = {
		.name = "max77686-pmic",
		.owner = THIS_MODULE,
	},
	.probe = max77686_pmic_probe,
	.remove = __devexit_p(max77686_pmic_remove),
	.id_table = max77686_pmic_id,
};

static int __init max77686_pmic_init(void)
{
	printk(PMIC_DEBUG "%s\n", __func__);

	return platform_driver_register(&max77686_pmic_driver);
}
subsys_initcall(max77686_pmic_init);

static void __exit max77686_pmic_cleanup(void)
{
	platform_driver_unregister(&max77686_pmic_driver);
}
module_exit(max77686_pmic_cleanup);

MODULE_DESCRIPTION("MAXIM 77686 Regulator Driver");
MODULE_AUTHOR("Chiwoong Byun <woong.byun@samsung.com>");
MODULE_LICENSE("GPL");
