/*
 * kernel container_of demo
 *
 * (C) 2020.03.26 liweijie<ee.liweijie@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>

struct person {
    int age;
    int salary;
    char *name;
};

int __init container_of_init(void)
{
    struct person leo;
    struct person *leo_ptr;

    int *age_ptr = &(leo.age);
    int *salary_ptr = &(leo.salary);
    char **name_ptr = &(leo.name);

    leo_ptr = container_of(age_ptr, struct person, age);
    printk(KERN_INFO "addr of person:0x%p\n", leo_ptr);

    leo_ptr = container_of(salary_ptr, struct person, salary);
    printk(KERN_INFO "addr of person:0x%p\n", leo_ptr);

    leo_ptr = container_of(name_ptr, struct person, name);
    printk(KERN_INFO "addr of person:0x%p\n", leo_ptr);


    printk(KERN_INFO "addr of age:0x%p, 0x%p\n", &(leo_ptr->age), &(leo.age));
    printk(KERN_INFO "addr of salary:0x%p, 0x%p\n", &(leo_ptr->salary), &(leo.salary));
    printk(KERN_INFO "addr of name:0x%p, 0x%p\n", leo_ptr->name, leo.name);

    return 0;
}

void __exit container_of_exit(void)
{
    return;
}
module_init(container_of_init);
module_exit(container_of_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("liweijie<ee.liweijie@gmail.com>");
