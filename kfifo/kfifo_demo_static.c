/*
 * kernel kfifo demo
 *
 * (C) 2020.03.28 liweijie<ee.liweijie@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kfifo.h>

#define FIFO_SIZE 999999
static DECLARE_KFIFO(test, unsigned char, FIFO_SIZE);

int test_func(void)
{
	char i;
	int ret;
	unsigned char buf[6];

	printk(KERN_INFO "fifo test begin\n");	
	kfifo_in(&test, "hello", 5);
	printk(KERN_INFO "fifo len:%d\n", kfifo_len(&test));
	
	/* put values ito the fifo */
	for (i = 0; i < 10; i++) {
		kfifo_put(&test, i);
	}
	/* show the number of used elements */
	printk(KERN_INFO "fifo len:%d\n", kfifo_len(&test));

	/* get max of 5 byte from the fifo */	
	i = kfifo_out(&test, buf, 5);
	printk(KERN_INFO "buf:%.*s\n",i, buf);
	printk(KERN_INFO "fifo len:%d\n", kfifo_len(&test));

	ret = kfifo_out(&test, buf, 2);
	printk(KERN_INFO "ret:%d\n", ret);

	ret = kfifo_in(&test, buf, ret);
	printk(KERN_INFO "ret:%d\n", ret);

	printk(KERN_INFO "skip 1st element\n");
	kfifo_skip(&test);

	for (i = 20; kfifo_put(&test, i); i++);
	printk(KERN_INFO "queue len %d\n", kfifo_len(&test));
	
	if (kfifo_peek(&test, &i))
		printk(KERN_INFO "kfifo peek: %d\n", i);

	while (kfifo_get(&test, &i))
		printk(KERN_INFO "item = %d\n", i);
	
	printk(KERN_INFO "fifo test end\n");
	return 0;
}

static int __init mod_init(void)
{
	INIT_KFIFO(test);
	if (test_func() < 0)
		return -EIO;
	return 0;
}
static void __exit mod_exit(void)
{
	return;
}

module_init(mod_init);
module_exit(mod_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("liweijie<ee.liweijie@gmail.com>");

