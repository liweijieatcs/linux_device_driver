#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/timer.h>

dev_t devno;
static struct class *seconds_class;

struct second_dev {
	struct cdev cdev;
	atomic_t counter;
	struct timer_list s_timer;
};

static struct second_dev *second_devp;

static void second_timer_handler(struct timer_list *s_timer)
{
	mod_timer(&second_devp->s_timer, jiffies + HZ);
	atomic_inc(&second_devp->counter);

	printk(KERN_INFO "current jiffies is %ld\n", jiffies);
}

static int second_open(struct inode *inode, struct file *filp)
{
	timer_setup(&second_devp->s_timer, second_timer_handler, 0);

	/* 设置定时器的周期为1秒 */
	second_devp->s_timer.expires = jiffies + HZ;

	/* 将定时器添加到内核的定时器链表 */
	add_timer(&second_devp->s_timer);

	atomic_set(&second_devp->counter, 0); /* 初始化秒计数为 0 */

	return 0;
}

static int second_release(struct inode *inode, struct file *filp)
{
	del_timer(&second_devp->s_timer);

	return 0;
}

static ssize_t second_read(struct file *filp, char __user * buf, size_t count, loff_t * ppos)
{
	int counter;

	counter = atomic_read(&second_devp->counter);
	if (put_user(counter, (int *)buf))/* 复制 counter 到 userspace */
		return -EFAULT;
	else
		return sizeof(unsigned int);
}

static const struct file_operations second_fops = {
	.owner = THIS_MODULE,
	.open = second_open,
	.release = second_release,
	.read = second_read,
};

static int __init second_init(void)
{
	int ret;
	
	ret = alloc_chrdev_region(&devno, 0, 1, "second");
	
	if (ret < 0)
		return ret;

	second_devp = kzalloc(sizeof(*second_devp), GFP_KERNEL);
	if (!second_devp) {
		ret = -ENOMEM;
		goto fail_malloc;
	}
	
	cdev_init(&second_devp->cdev, &second_fops);
	second_devp->cdev.owner = THIS_MODULE;
	ret = cdev_add(&second_devp->cdev, devno, 1);
	if (ret)
		printk(KERN_ERR "Failed to add second device\n");
	
	/* 创建class */
	seconds_class =class_create(THIS_MODULE, "seconds_class");
		if (IS_ERR(seconds_class)) {
			printk(KERN_INFO "creat secongs_class failed!\n");
			return -1;
	}

	/* 将设备挂在seconds_class class 下 */
	device_create(seconds_class, NULL, devno, NULL, "second");

	return ret;

fail_malloc:
	unregister_chrdev_region(devno, 1);
	return ret;
}
module_init(second_init);

static void __exit second_exit(void)
{
	cdev_del(&second_devp->cdev);
	kfree(second_devp);
	unregister_chrdev_region(devno, 1);
	device_destroy(seconds_class, devno);
	class_destroy(seconds_class);
}

module_exit(second_exit);

MODULE_LICENSE("GPL");
