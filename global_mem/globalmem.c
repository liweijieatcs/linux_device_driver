#include <linux/module.h>
#include <linux/cdev.h>         /* for alloc_chrdev_region */
#include <linux/slab.h>         /* for kzlloc */
#include <linux/uaccess.h>      /* for copy_from(to)_user */

#define GLOBAL_MEM_SIZE 4096
#define MEM_CLEAR 0x01
#define DEVICE_NUM 2

dev_t dev_no;

static struct class *globalmem_class[DEVICE_NUM];
static char *chr_dev_name[20] = {"g_m_0", "g_m_1"}; /* This must match the DEVICE_NUM */

/* define the global mem device and the pointer to the device */
struct global_mem_dev {
    struct cdev cdev;
    unsigned char mem[GLOBAL_MEM_SIZE];
    struct mutex mutex;
};
struct global_mem_dev *global_mem_devp;


int global_mem_open(struct inode *inode, struct file *filp)
{
    /* set the device pointer to the file private data
     * then we can manipulate the device via private data
     */
    struct global_mem_dev *dev = container_of(inode->i_cdev, struct global_mem_dev, cdev);
    filp->private_data = dev;
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

    mutex_lock(&dev->mutex);
    if (copy_to_user(buf, (void *)(dev->mem + p), count)) {
        ret = -EFAULT;
    } else {
        *ppos += count;
        ret = count;
        printk(KERN_INFO "read %u bytes(s) from %lu\n", count, p);
    }
    mutex_unlock(&dev->mutex);

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

    mutex_lock(&dev->mutex);
    if (copy_from_user(dev->mem + p, buf, count)) {
        ret = -EFAULT;
    } else {
        *ppos += count;
        ret = count;
        printk(KERN_INFO "written %u bytes(s) from %lu\n", count, p);
    }
    mutex_unlock(&dev->mutex);

    return ret;
}

static long global_mem_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct global_mem_dev *dev = filp->private_data;
    switch (cmd) {
    case MEM_CLEAR:
        mutex_lock(&dev->mutex);
        memset(dev->mem, 0, GLOBAL_MEM_SIZE);
        mutex_unlock(&dev->mutex);
        printk(KERN_INFO "globalmem is set to zero\n");
        break;

    default:
        return -EINVAL;
    }

    return 0;
}

static const struct file_operations global_mem_fops = {
    .owner = THIS_MODULE,
    .open = global_mem_open,
    .read = global_mem_read,
    .write = global_mem_write,
    .release = global_mem_release,
    .unlocked_ioctl = global_mem_ioctl,
};

static int __init global_mem_init(void)
{
    int ret;
    int err;
    int major;
    int minor;
    int i;
    struct global_mem_dev *global_mem_devp_tmp;

    /* alloc chrdev region */
    ret = alloc_chrdev_region(&dev_no, 0, DEVICE_NUM, "globalmem");
    if (ret < 0) {
        printk(KERN_INFO "fail to alloc chrdev region\n");
    }
    major = MAJOR(dev_no);
    minor = MINOR(dev_no);
    printk(KERN_INFO "chrdev major:%d, minor:%d\n", major, minor);

    /* alloc memory for the  device */
    global_mem_devp = (struct global_mem_dev *)kzalloc(sizeof(struct global_mem_dev) * DEVICE_NUM, GFP_KERNEL);
    if (!global_mem_devp) {
        ret = -ENOMEM;
        goto fail_malloc;
    }

    /* init chrdev and add the chrdev to the kernel */
    global_mem_devp_tmp = global_mem_devp;
    for (i = 0; i < DEVICE_NUM; i++) {
        cdev_init(&(global_mem_devp_tmp + i)->cdev, &global_mem_fops);
        err = cdev_add(&(global_mem_devp_tmp + i)->cdev, MKDEV(MAJOR(dev_no), i), 1);
        if (err)
            printk(KERN_NOTICE "Error %d adding globalmem device\n", err);
    }

    /* create class and create device attach it*/
    globalmem_class[0] =class_create(THIS_MODULE, "global_mem");
    if (IS_ERR(globalmem_class[0])) {
        printk(KERN_INFO "creat globalmem_class failed!\n");
        return -1;
    }
    for (i = 0; i < DEVICE_NUM; i++) {
        device_create(globalmem_class[0], NULL, MKDEV(MAJOR(dev_no), i), NULL, *(chr_dev_name + i));
        printk(KERN_INFO "chr dev %s created!\n", *(chr_dev_name + i));
    }

    /* init mutex */
    mutex_init(&global_mem_devp->mutex);

    printk(KERN_INFO "global_mem init success.\n");
    return 0;

fail_malloc:
    unregister_chrdev_region(dev_no, DEVICE_NUM);
    return ret;
}

static void __exit global_mem_exit(void)
{
    int i;

    for (i = 0; i < DEVICE_NUM; i++)
        cdev_del(&(global_mem_devp + i)->cdev);
    kfree(global_mem_devp);
    unregister_chrdev_region(dev_no, DEVICE_NUM);

    for (i = 0; i < DEVICE_NUM; i++)
        device_destroy(globalmem_class[0], MKDEV(MAJOR(dev_no), i));

    class_destroy(globalmem_class[0]);
    printk(KERN_INFO "global_mem exit success.\n");
    return;
}

module_init(global_mem_init);
module_exit(global_mem_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("liweijie");
MODULE_VERSION("0.01");
