/*
 * kernel linked_list demo
 *
 * (C) 2020.03.26 liweijie<ee.liweijie@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/list.h>

static int __init mod_init(void)
{
	struct mystruct {
		int data;
		struct list_head mylist;
		};

	struct mystruct node_1;
	struct mystruct node_2;
	struct mystruct node_3 = {
		.data = 97,
		.mylist = LIST_HEAD_INIT(node_3.mylist),
	};

	struct list_head *position = NULL;
	struct mystruct *datastructptr = NULL;
	LIST_HEAD(mylinkedlist);
	
	node_1.data = 99;
	INIT_LIST_HEAD(&(node_1.mylist));

	node_2.data = 98;
	INIT_LIST_HEAD(&(node_2.mylist));

	list_add(&node_1.mylist, &mylinkedlist);
	list_add(&node_2.mylist, &mylinkedlist);
	list_add(&node_3.mylist, &mylinkedlist);

	list_for_each(position, &mylinkedlist) {
		datastructptr = list_entry(position, struct mystruct, mylist);
		printk(KERN_INFO "data:%d\n", datastructptr->data);
	}

	list_del(&node_1.mylist);
	printk(KERN_INFO "after delete one node\n");
	list_for_each_entry(datastructptr, &mylinkedlist, mylist) {
		printk(KERN_INFO "data:%d\n", datastructptr->data);
	}

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
