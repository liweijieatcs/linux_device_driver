#include <linux/module.h>
#include <linux/cdev.h>         /* for alloc_chrdev_region */

dev_t dev_no;
static int __init global_mem_init(void)
{
    int ret;
    int major;
    int minor;
    ret = alloc_chrdev_region(&dev_no, 0, 1, "globalmem");
    if (ret < 0) {
        printk(KERN_INFO "fail to alloc chrdev region\n");
    }
    major = MAJOR(dev_no);
    minor = MINOR(dev_no);
    printk(KERN_INFO "chrdev major:%d, minor:%d\n", major, minor);
    printk(KERN_INFO "global_mem init success.\n");
    return 0;
}

static void __exit global_mem_exit(void)
{
    unregister_chrdev_region(dev_no, 1);
    printk(KERN_INFO "global_mem exit success.\n");
    return;
}

module_init(global_mem_init);
module_exit(global_mem_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("liweijie");
MODULE_VERSION("0.01");