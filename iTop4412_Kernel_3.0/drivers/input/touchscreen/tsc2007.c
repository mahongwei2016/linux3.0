/*
 * drivers/input/touchscreen/tsc2007.c
 *
 * Copyright (c) 2008 MtekVision Co., Ltd.
 *	Kwangwoo Lee <kwlee@mtekvision.com>
 *
 * Using code from:
 *  - ads7846.c
 *	Copyright (c) 2005 David Brownell
 *	Copyright (c) 2006 Nokia Corporation
 *  - corgi_ts.c
 *	Copyright (C) 2004-2005 Richard Purdie
 *  - omap_ts.[hc], ads7846.h, ts_osk.c
 *	Copyright (C) 2002 MontaVista Software
 *	Copyright (C) 2004 Texas Instruments
 *	Copyright (C) 2005 Dirk Behme
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/i2c/tsc2007.h>

/* add by cym 20141202 */
#include <linux/irq.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
/* end add */

/* add by cym 20130417 */
#include <linux/proc_fs.h>
#include <asm/uaccess.h>

#define TSC2007_DEBUG_ON	0

#ifdef CONFIG_STAGING	//for Android
#define GTP_ICS_SLOT_REPORT   1
#endif

#define SCREEN_MAX_X    272	//480
#define SCREEN_MAX_Y    480	//800
#define PRESS_MAX       255
#define CFG_MAX_TOUCH_POINTS  1

#define TSC2007_DEBUG(fmt,arg...)          do{\
                                         if(TSC2007_DEBUG_ON)\
                                         printk("TSC2007-DEBUG[%d]"fmt,__LINE__, ##arg);\
                                       }while(0)

#if GTP_ICS_SLOT_REPORT
#include <linux/input/mt.h>
#endif
/* end add */

#define TSC2007_MEASURE_TEMP0		(0x0 << 4)
#define TSC2007_MEASURE_AUX		(0x2 << 4)
#define TSC2007_MEASURE_TEMP1		(0x4 << 4)
#define TSC2007_ACTIVATE_XN		(0x8 << 4)
#define TSC2007_ACTIVATE_YN		(0x9 << 4)
#define TSC2007_ACTIVATE_YP_XN		(0xa << 4)
#define TSC2007_SETUP			(0xb << 4)
#define TSC2007_MEASURE_X		(0xc << 4)
#define TSC2007_MEASURE_Y		(0xd << 4)
#define TSC2007_MEASURE_Z1		(0xe << 4)
#define TSC2007_MEASURE_Z2		(0xf << 4)

#define TSC2007_POWER_OFF_IRQ_EN	(0x0 << 2)
#define TSC2007_ADC_ON_IRQ_DIS0		(0x1 << 2)
#define TSC2007_ADC_OFF_IRQ_EN		(0x2 << 2)
#define TSC2007_ADC_ON_IRQ_DIS1		(0x3 << 2)

#define TSC2007_12BIT			(0x0 << 1)
#define TSC2007_8BIT			(0x1 << 1)

#define	MAX_12BIT			((1 << 12) - 1)

#define ADC_ON_12BIT	(TSC2007_12BIT | TSC2007_ADC_ON_IRQ_DIS0)

#define READ_Y		(ADC_ON_12BIT | TSC2007_MEASURE_Y)
#define READ_Z1		(ADC_ON_12BIT | TSC2007_MEASURE_Z1)
#define READ_Z2		(ADC_ON_12BIT | TSC2007_MEASURE_Z2)
#define READ_X		(ADC_ON_12BIT | TSC2007_MEASURE_X)
#define PWRDOWN		(TSC2007_12BIT | TSC2007_POWER_OFF_IRQ_EN)

struct ts_event {
	u16	x;
	u16	y;
	u16	z1, z2;
};

struct tsc2007 {
	struct input_dev	*input;
	char			phys[32];
	struct delayed_work	work;

	struct i2c_client	*client;

	u16			model;
	u16			x_plate_ohms;
	u16			max_rt;
	unsigned long		poll_delay;
	unsigned long		poll_period;

	bool			pendown;
	int			irq;

	int			(*get_pendown_state)(void);
	void			(*clear_penirq)(void);

	/* add by cym 20141202 */
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
	/* end add */
};

/* add by cym 20141202 */
int flags = 0;
/* end add */

/* add by cym 20130417 */
static struct proc_dir_entry *ts_proc_entry;

//13695 63 -1806708 72 -9270 32799468 65536
//signed long pointercal[7] = {13662, -2, -1835668, 104, -9256, 32765962, 65536};
signed long pointercal[7] = {-8317, 14, 33618640, -2, -4904, 18777894, 65536};

static int ts_proc_write(struct file *file, const char *buffer,
			     unsigned long count, void *data)
{
	char buf[256];
	unsigned long len;
	char *p = (char *)buf;
	int i,j;
	int flag = 1;

	memset(buf,0,256);
	len = min_t(unsigned long, sizeof(buf) - 1, count);

	if (copy_from_user(buf, buffer, len))
		return count;

	j = 0;
	for(i=0;i<len;i++)
	{
		if(flag)
		{
			pointercal[j] = simple_strtol(p, &p, 10);

			j++; 
			if(j>=7)
				break;

			flag = 0;
		}
		

		if(p[0] == ' ')
		{
			flag = 1;
		}
		if(p[0] == '\t')
		{
			flag = 1;
		}
		p++;
	}
	return count;

}
/* end add */

static inline int tsc2007_xfer(struct tsc2007 *tsc, u8 cmd)
{
	s32 data;
	u16 val;

	data = i2c_smbus_read_word_data(tsc->client, cmd);
	if (data < 0) {
		dev_err(&tsc->client->dev, "i2c io error: %d\n", data);
		return data;
	}

	/* The protocol and raw data format from i2c interface:
	 * S Addr Wr [A] Comm [A] S Addr Rd [A] [DataLow] A [DataHigh] NA P
	 * Where DataLow has [D11-D4], DataHigh has [D3-D0 << 4 | Dummy 4bit].
	 */
	val = swab16(data) >> 4;

	dev_dbg(&tsc->client->dev, "data: 0x%x, val: 0x%x\n", data, val);

	return val;
}

static void tsc2007_read_values(struct tsc2007 *tsc, struct ts_event *tc)
{
	/* add by cym 20130417 */
	long a,b,c,d,e,f,div;
	/* end add */

	/* y- still on; turn on only y+ (and ADC) */
	tc->y = tsc2007_xfer(tsc, READ_Y);

	/* turn y- off, x+ on, then leave in lowpower */
	tc->x = tsc2007_xfer(tsc, READ_X);

	/* turn y+ off, x- on; we'll use formula #1 */
	tc->z1 = tsc2007_xfer(tsc, READ_Z1);
	tc->z2 = tsc2007_xfer(tsc, READ_Z2);

	/* add by cym 20130417 */
//#if GTP_ICS_SLOT_REPORT
	a = pointercal[0];
	b = pointercal[1];
	c = pointercal[2];
	d = pointercal[3];
	e = pointercal[4];
	f = pointercal[5];
	div = pointercal[6];

	tc->x = (a*tc->x + b*tc->y + c)/div;
	tc->y = (d*tc->x + e*tc->y + f)/div;
//#endif
	/* end add */
	//printk("x:%d, y:%d\n", tc->x, tc->y);
	/* Prepare for next touch reading - power down ADC, enable PENIRQ */
	tsc2007_xfer(tsc, PWRDOWN);
}

static u32 tsc2007_calculate_pressure(struct tsc2007 *tsc, struct ts_event *tc)
{
	u32 rt = 0;

	/* range filtering */
	if (tc->x == MAX_12BIT)
		tc->x = 0;

	if (likely(tc->x && tc->z1)) {
		/* compute touch pressure resistance using equation #1 */
		rt = tc->z2 - tc->z1;
		rt *= tc->x;
		rt *= tsc->x_plate_ohms;
		rt /= tc->z1;
		rt = (rt + 2047) >> 12;
	}

	return rt;
}

static void tsc2007_send_up_event(struct tsc2007 *tsc)
{
	struct input_dev *input = tsc->input;

	dev_dbg(&tsc->client->dev, "UP\n");
	TSC2007_DEBUG("UP\n");

#if GTP_ICS_SLOT_REPORT
	input_mt_slot(input, 0);
    	input_report_abs(input, ABS_MT_TRACKING_ID, -1);
#else
	input_report_key(input, BTN_TOUCH, 0);
	input_report_abs(input, ABS_PRESSURE, 0);
#endif
	input_sync(input);
}

static void tsc2007_work(struct work_struct *work)
{
	static u16 x_10 = 0, y_10 = 0, i = 0, x_10_pre = 0, y_10_pre = 0, cmp_x = 0, cmp_y = 0;
	struct tsc2007 *ts =
		container_of(to_delayed_work(work), struct tsc2007, work);
	bool debounced = false;
	struct ts_event tc;
	u32 rt;
	u16 x, y;

	/*
	 * NOTE: We can't rely on the pressure to determine the pen down
	 * state, even though this controller has a pressure sensor.
	 * The pressure value can fluctuate for quite a while after
	 * lifting the pen and in some cases may not even settle at the
	 * expected value.
	 *
	 * The only safe way to check for the pen up condition is in the
	 * work function by reading the pen signal state (it's a GPIO
	 * and IRQ). Unfortunately such callback is not always available,
	 * in that case we have rely on the pressure anyway.
	 */
	if (ts->get_pendown_state) {
		if (unlikely(!ts->get_pendown_state())) {
		//if (!ts->get_pendown_state())) {
			tsc2007_send_up_event(ts);
			ts->pendown = false;
			goto out;
		}
		TSC2007_DEBUG("pen is still down\n");
		dev_dbg(&ts->client->dev, "pen is still down\n");
	}

	tsc2007_read_values(ts, &tc);

	rt = tsc2007_calculate_pressure(ts, &tc);
	if (rt > ts->max_rt) {
		/*
		 * Sample found inconsistent by debouncing or pressure is
		 * beyond the maximum. Don't report it to user space,
		 * repeat at least once more the measurement.
		 */
		dev_dbg(&ts->client->dev, "ignored pressure %d\n", rt);
		debounced = true;
		goto out;

	}

	if(rt)
	{
		if(tc.x > x_10_pre)
			cmp_x = tc.x - x_10_pre;
		else
			cmp_x = x_10_pre - tc.x;

		if(tc.y > y_10_pre)
                        cmp_y = tc.y - y_10_pre;
                else
                        cmp_y = y_10_pre - tc.y;

		if((cmp_x<4) && (cmp_y<4))
		{
			x_10 += x_10_pre;//tc.x;
			y_10 += y_10_pre;//tc.y;
			i++;

			//x_10_pre = tc.x;
			//y_10_pre = tc.y;
		}
		else
		{
			x_10 = 0;
			y_10 = 0;
			i = 0;
			
			x_10_pre = tc.x;
			y_10_pre = tc.y;

			x_10 += tc.x;
                        y_10 += tc.y;
                        i++;

			//x_10_pre = tc.x;
                        //y_10_pre = tc.y;
		}
	}

	if (rt && (1 == i)) {
		tc.x = x_10/i;
		tc.y = y_10/i;

		x_10 = 0;
		y_10 = 0;
		i = 0;

		struct input_dev *input = ts->input;

		if (!ts->pendown) {
			dev_dbg(&ts->client->dev, "DOWN\n");
			TSC2007_DEBUG("DOWN\n");
			input_report_key(input, BTN_TOUCH, 1);
			ts->pendown = true;
		}
#if 1
		x = SCREEN_MAX_X - tc.y;
		y = tc.x;
#else
		//x = (tc.x * 48)/80;
		//y = (tc.y * 80)/48;
#endif	
		TSC2007_DEBUG("%s: x:%d, y:%d, pre:%d\n", __FUNCTION__, x, y, rt);
		//printk("%s: x:%d, y:%d, pre:%d\n", __FUNCTION__, x, y, rt);;
#if GTP_ICS_SLOT_REPORT
		//input_mt_slot(input, 0);
		//input_report_abs(input, ABS_MT_TRACKING_ID, 0);
    		input_report_abs(input, ABS_MT_POSITION_X, x);
		input_report_abs(input, ABS_MT_POSITION_Y, y);
		//input_report_abs(input, ABS_MT_WIDTH_MAJOR, 1);
		input_report_abs(input, ABS_MT_PRESSURE, x+y);
		input_report_abs(input, ABS_MT_TOUCH_MAJOR, x+y);
		input_report_abs(input, ABS_MT_TRACKING_ID, 0);
#else
		input_report_abs(input, ABS_X, tc.x);
		input_report_abs(input, ABS_Y, tc.y);
		//input_report_key(input, BTN_TOUCH, 1);
		input_report_abs(input, ABS_PRESSURE, rt);
#endif
		input_sync(input);

		dev_dbg(&ts->client->dev, "point(%4d,%4d), pressure (%4u)\n",
			tc.x, tc.y, rt);

	}
	else if(!ts->get_pendown_state && ts->pendown) {
		/*
		 * We don't have callback to check pendown state, so we
		 * have to assume that since pressure reported is 0 the
		 * pen was lifted up.
		 */
		 TSC2007_DEBUG("tsc2007_send_up_event\n");
		tsc2007_send_up_event(ts);
		ts->pendown = false;
	}

 out:
	/* modify by cym 20141202 */
#if 0
	if (ts->pendown || debounced)
#else
	if ((ts->pendown || debounced) && (0 == flags))
#endif
	/* end modify */
		schedule_delayed_work(&ts->work,
				      msecs_to_jiffies(ts->poll_period));
	else
		enable_irq(ts->irq);

}

static irqreturn_t tsc2007_irq(int irq, void *handle)
{
	struct tsc2007 *ts = handle;

	//printk("%s, line = %d\n", __FUNCTION__, __LINE__);
#if 1
	if (!ts->get_pendown_state || likely(ts->get_pendown_state())) 
	{
		disable_irq_nosync(ts->irq);
		schedule_delayed_work(&ts->work,
				      msecs_to_jiffies(ts->poll_delay));
	}

	if (ts->clear_penirq)
		ts->clear_penirq();
#endif
	return IRQ_HANDLED;
}

static void tsc2007_free_irq(struct tsc2007 *ts)
{
	free_irq(ts->irq, ts);
	if (cancel_delayed_work_sync(&ts->work)) {
		/*
		 * Work was pending, therefore we need to enable
		 * IRQ here to balance the disable_irq() done in the
		 * interrupt handler.
		 */
		enable_irq(ts->irq);
	}
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void tsc2007_ts_suspend(struct early_suspend *handler)
{
#ifdef CONFIG_STAGING	//for Android
	struct tsc2007 *ts;
	
	ts = container_of(handler, struct tsc2007, early_suspend);

	//cancel_delayed_work_sync(&ts->work);
	
	disable_irq(ts->client->irq);

	flags = 1;
#endif

	printk("tsc2007_ts: suspended\n");
}

static void tsc2007_ts_resume(struct early_suspend *handler)
{
#ifdef CONFIG_STAGING   //for Android
        struct tsc2007 *ts;

        ts = container_of(handler, struct tsc2007, early_suspend);

        enable_irq(ts->client->irq);

	flags = 0;
#endif

	printk("tsc2007_ts: resumed\n");
}
#endif

static int __devinit tsc2007_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	struct tsc2007 *ts;
	struct tsc2007_platform_data *pdata = client->dev.platform_data;
	struct input_dev *input_dev;
	int err;

	if (!pdata) {
		dev_err(&client->dev, "platform data is required!\n");
		return -EINVAL;
	}

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_READ_WORD_DATA))
		return -EIO;

	ts = kzalloc(sizeof(struct tsc2007), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!ts || !input_dev) {
		err = -ENOMEM;
		goto err_free_mem;
	}

	ts->client = client;
	ts->irq = client->irq;
	ts->input = input_dev;
	INIT_DELAYED_WORK(&ts->work, tsc2007_work);

	ts->model             = pdata->model;
	ts->x_plate_ohms      = pdata->x_plate_ohms;
	ts->max_rt            = pdata->max_rt ? : MAX_12BIT;
	ts->poll_delay        = pdata->poll_delay ? : 1;
	ts->poll_period       = pdata->poll_period ? : 1;
	ts->get_pendown_state = pdata->get_pendown_state;
	ts->clear_penirq      = pdata->clear_penirq;

	snprintf(ts->phys, sizeof(ts->phys),
		 "%s/ts2007", dev_name(&client->dev));

	input_dev->name = "TSC2007 Touchscreen";
	input_dev->phys = ts->phys;
	input_dev->id.bustype = BUS_I2C;

#if GTP_ICS_SLOT_REPORT
	input_dev->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS) ;
	input_dev->absbit[0] = BIT(ABS_X) | BIT(ABS_Y) | BIT(ABS_PRESSURE);

	set_bit(INPUT_PROP_DIRECT, input_dev->propbit);
	input_mt_init_slots(input_dev, 255);

	input_set_abs_params(input_dev, ABS_X, 0, SCREEN_MAX_X, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, SCREEN_MAX_Y, 0, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, 255, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0, SCREEN_MAX_X, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0, SCREEN_MAX_Y, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_PRESSURE, 0, 255, 0, 0);
    	input_set_abs_params(input_dev, ABS_MT_TRACKING_ID, 0, CFG_MAX_TOUCH_POINTS, 0, 0);
#else
	set_bit(EV_SYN, input_dev->evbit);
	set_bit(EV_ABS, input_dev->evbit);
	set_bit(EV_KEY, input_dev->evbit);

	//input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	//input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	set_bit(ABS_X, input_dev->absbit);
	set_bit(ABS_Y, input_dev->absbit);
	set_bit(ABS_PRESSURE, input_dev->absbit);
	set_bit(BTN_TOUCH, input_dev->keybit);

	input_set_abs_params(input_dev, ABS_X, 0, MAX_12BIT, pdata->fuzzx, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, MAX_12BIT, pdata->fuzzy, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, MAX_12BIT,
			pdata->fuzzz, 0);
#endif

	if (pdata->init_platform_hw)
		pdata->init_platform_hw();

	err = request_irq(ts->irq, tsc2007_irq, IRQ_TYPE_EDGE_FALLING,
			client->dev.driver->name, ts);
	if (err < 0) {
		dev_err(&client->dev, "irq %d busy?\n", ts->irq);
		goto err_free_mem;
	}

	/* Prepare for touch readings - power down ADC and enable PENIRQ */
	err = tsc2007_xfer(ts, PWRDOWN);
	if (err < 0)
		goto err_free_irq;

	err = input_register_device(input_dev);
	if (err)
		goto err_free_irq;

	i2c_set_clientdata(client, ts);

	/* add by cym 20130417 */
	ts_proc_entry = create_proc_entry("driver/micc_ts", 0, NULL);   
	if (ts_proc_entry) {   
		ts_proc_entry->write_proc = ts_proc_write;   
	}
	/* end add */

	/* add by cym 20141202 */
#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN;//EARLY_SUSPEND_LEVEL_DISABLE_FB + 1;
	ts->early_suspend.suspend = tsc2007_ts_suspend;
	ts->early_suspend.resume = tsc2007_ts_resume;
	register_early_suspend(&ts->early_suspend);
#endif
	/* end add */

	return 0;

 err_free_irq:
	tsc2007_free_irq(ts);
	if (pdata->exit_platform_hw)
		pdata->exit_platform_hw();
 err_free_mem:
	input_free_device(input_dev);
	kfree(ts);
	return err;
}

static int __devexit tsc2007_remove(struct i2c_client *client)
{
	struct tsc2007	*ts = i2c_get_clientdata(client);
	struct tsc2007_platform_data *pdata = client->dev.platform_data;

	tsc2007_free_irq(ts);

	if (pdata->exit_platform_hw)
		pdata->exit_platform_hw();

	input_unregister_device(ts->input);
	kfree(ts);

	return 0;
}

static const struct i2c_device_id tsc2007_idtable[] = {
	{ "tsc2007", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, tsc2007_idtable);

static struct i2c_driver tsc2007_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "tsc2007"
	},
	.id_table	= tsc2007_idtable,
	.probe		= tsc2007_probe,
	.remove		= __devexit_p(tsc2007_remove),
};

static int __init tsc2007_init(void)
{
	return i2c_add_driver(&tsc2007_driver);
}

static void __exit tsc2007_exit(void)
{
	i2c_del_driver(&tsc2007_driver);
}

module_init(tsc2007_init);
module_exit(tsc2007_exit);

MODULE_AUTHOR("Kwangwoo Lee <kwlee@mtekvision.com>");
MODULE_DESCRIPTION("TSC2007 TouchScreen Driver");
MODULE_LICENSE("GPL");
