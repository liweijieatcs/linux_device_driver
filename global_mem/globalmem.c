#include <linux/module.h>
#include <linux/cdev.h>         /* for alloc_chrdev_region */
#include <linux/slab.h>         /* for kzlloc */
#include <linux/uaccess.h>      /* for copy_from(to)_user */

#define GLOBAL_MEM_SIZE 4096

dev_t dev_no;

/* define the global mem device and the pointer to the device */
struct global_mem_dev {
    struct cdev cdev;
    unsigned char mem[GLOBAL_MEM_SIZE];
};
struct global_mem_dev *global_mem_devp;


int global_mem_open(struct inode *inode, struct file *filp)
{
    /* set the device pointer to the file private data
     * then we can manipulate the device via private data
     */
    filp->private_data = global_mem_devp;
    return 0;
}

int global_mem_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static ssize_t global_mem_read(struct file *filp, char __user *buf, size_t size, loff_t *ppos)
{
    unsigned long p =  *ppos;
    unsigned int count = size;
    int ret = 0;
    struct global_mem_dev *dev = filp->private_data;

    if (p >= GLOBAL_MEM_SIZE)
        return 0;

    if (count > GLOBAL_MEM_SIZE - p)
        count = GLOBAL_MEM_SIZE - p;

    if (copy_to_user(buf, (void *)(dev->mem + p), count)) {
        ret = -EFAULT;
    } else {
        *ppos += count;
        ret = count;
        printk(KERN_INFO "read %u bytes(s) from %lu\n", count, p);
    }

    return ret;
}

static ssize_t global_mem_write(struct file *filp, const char __user *buf, size_t size, loff_t *ppos)
{
    unsigned long p =  *ppos;
    unsigned int count = size;
    int ret = 0;
    struct global_mem_dev *dev = filp->private_data;

    if (p >= GLOBAL_MEM_SIZE)
        return 0;

    if (count > GLOBAL_MEM_SIZE - p)
        count = GLOBAL_MEM_SIZE - p;

    if (copy_from_user(dev->mem + p, buf, count)) {
        ret = -EFAULT;
    } else {
        *ppos += count;
        ret = count;
        printk(KERN_INFO "written %u bytes(s) from %lu\n", count, p);
    }

    return ret;
}

static const struct file_operations global_mem_fops = {
    .owner = THIS_MODULE,
    .open = global_mem_open,
    .read = global_mem_read,
    .write = global_mem_write,
    .release = global_mem_release,
};

static int __init global_mem_init(void)
{
    int ret;
    int err;
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

    /* init chrdev and add the chrdev to the kernel */
    cdev_init(&(global_mem_devp->cdev), &global_mem_fops);
    err = cdev_add(&(global_mem_devp->cdev), dev_no, 1);
    if (err)
        printk(KERN_NOTICE "Error %d adding globalmem device\n", err);

    printk(KERN_INFO "global_mem init success.\n");
    return 0;

fail_malloc:
    unregister_chrdev_region(dev_no, 1);
    return ret;
}

static void __exit global_mem_exit(void)
{
    cdev_del(&global_mem_devp->cdev);
    kfree(global_mem_devp);
    unregister_chrdev_region(dev_no, 1);
    printk(KERN_INFO "global_mem exit success.\n");
    return;
}

module_init(global_mem_init);
module_exit(global_mem_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("liweijie");
MODULE_VERSION("0.01");