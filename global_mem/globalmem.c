#include <linux/module.h>
#include <linux/cdev.h>         /* for alloc_chrdev_region */
#include <linux/slab.h>         /* for kzlloc */

#define GLOBAL_MEM_SIZE 4096

dev_t dev_no;

/* define the global mem device and the pointer to the device */
struct global_mem_dev {
    struct cdev cdev;
    unsigned char mem[GLOBAL_MEM_SIZE];
};
struct global_mem_dev *global_mem_devp;

static int __init global_mem_init(void)
{
    int ret;
    int major;
    int minor;

    /* alloc chrdev region */
    ret = alloc_chrdev_region(&dev_no, 0, 1, "globalmem");
    if (ret < 0) {
        printk(KERN_INFO "fail to alloc chrdev region\n");
    }
    major = MAJOR(dev_no);
    minor = MINOR(dev_no);
    printk(KERN_INFO "chrdev major:%d, minor:%d\n", major, minor);

    /* alloc memory for the  device */
    global_mem_devp = (struct global_mem_dev *)kzalloc(sizeof(struct global_mem_dev), GFP_KERNEL);
    if (!global_mem_devp) {
        ret = -ENOMEM;
        goto fail_malloc;
    }
    printk(KERN_INFO "global_mem init success.\n");
    return 0;

fail_malloc:
    unregister_chrdev_region(dev_no, 1);
    return ret;
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