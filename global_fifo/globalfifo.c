#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/slab.h>			/* for kzalloc() */
#include <linux/uaccess.h>		/* for copy_from(to)_user */
#include <linux/wait.h>			/* for wait_queue_head_t */
#include <linux/sched/signal.h>	/* for signal_pending */
#include <linux/wait.h>			/* for wait_up_interrruptible */
#include <linux/poll.h>			/* for poll_table */

#define GLOBALMEM_SIZE 4096
#define DEVICE_NUM 4
#define GLOBAL_MEM_MAGIC 'g'
#define MEM_CLEAR _IO(GLOBAL_MEM_MAGIC, 0)

static struct class *globalmem_class;
static char *chr_dev_name[20] = {"global_mem_0", "global_mem_1", "global_mem_2", "global_mem_3"};

struct global_mem_dev {
	struct cdev cdev;
	unsigned char mem[GLOBALMEM_SIZE];
	int current_len;			/* 表示当前在fifo中剩余的长度 */
	struct mutex mutex;
	wait_queue_head_t r_wait;	/* 读等待队列 */
	wait_queue_head_t w_wait;	/* 写等待队列 */
};

struct global_mem_dev *global_mem_devp;

dev_t devno; /* 为了在init和exit函数中使用，要用到全局变量 */

int global_mem_open(struct inode *inode, struct file *filp)
{
	/* 在设备驱动中默认将设备指针挂接在文件的私有数据中，在后续只需要对文件的私有数据进行操作即可 */
	//filp->private_data = global_mem_devp;
	struct global_mem_dev *dev = container_of(inode->i_cdev, struct global_mem_dev, cdev);
	filp->private_data = dev;

	return 0;
}

int global_mem_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static ssize_t global_mem_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
	int ret = 0;
	struct global_mem_dev *dev = filp->private_data;

	/* 申请等待队列wait */
	DECLARE_WAITQUEUE(wait, current);

	mutex_lock(&dev->mutex);
	add_wait_queue(&dev->r_wait, &wait);

	/* 判断FIFO 是否为0，如果为空则不能读取了，需要等待写进程增加current_len   */
	while (dev->current_len == 0) {
		/* 非阻塞方式:
		 * 直接返回EAGIAN，释放mutex 
		 */
		if (filp->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			goto out;
		}

		/* 阻塞方式:
		 * 需要讲进程状态设置为TASK_INTERRRUPTIBLE，可以被信号唤醒，即所谓的浅度睡眠
		 * 释放mutex，调用schedule()，调取其他进程。
		 */
		printk("globelmem read before schedule.\n");
		__set_current_state(TASK_INTERRUPTIBLE);
		/* 在把自己切换出去 时，要释放锁，以便读进程可以拿到 */
		mutex_unlock(&dev->mutex); 
		schedule();

		/* 如果是因为被信号唤醒了，则返回ERESTARTSYS 将r_wait移除队列，
		 * 并且把进程状态设置为TASK_RUNNING 
		 */
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			goto out2;
		}

		/* 
		 * 再次进入的时候，需要再次获得锁，这里对应的是unlock可能是schedule之前的
		 * 也可能是最后返回时的锁	
		 */
		mutex_lock(&dev->mutex);
	}

	if (count > dev->current_len) {
		count = dev->current_len;
	}
	
	if (copy_to_user(buf, dev->mem, count)) {
		ret = -EFAULT;
		goto out;
	} else {
		/* 将剩下的数据移动到fifo的头部 */
		memcpy(dev->mem, dev->mem + count, dev->current_len - count);
		dev->current_len -= count;
		printk(KERN_INFO "read %lu byte(s), current_len:%d\n", count, dev->current_len);

		/* 读进程完成，唤醒可能阻塞的写进程 */
		wake_up_interruptible(&dev->w_wait);
		
		ret = count;
	}

out:
	mutex_unlock(&dev->mutex);

out2:
	remove_wait_queue(&dev->r_wait, &wait);
	set_current_state(TASK_RUNNING);
	
	return ret;

}

static ssize_t global_mem_write(struct file *filp, const char __user *buf, size_t count, loff_t *ppos)
{
	int ret = 0;
	struct global_mem_dev *dev = filp->private_data;
	DECLARE_WAITQUEUE(wait, current);	/* 定义等待队列wait */

	mutex_lock(&dev->mutex);
	add_wait_queue(&dev->w_wait, &wait);	/* 添加w_wait到等待队列wait */

	while (dev->current_len == GLOBALMEM_SIZE) {
		if (filp->f_flags &O_NONBLOCK) {
			ret = -EAGAIN;
			goto out;
		}

		printk("globelmem write before schedule.\n");
		__set_current_state(TASK_INTERRUPTIBLE);
		mutex_unlock(&dev->mutex);
		/* 把自己切换出去 */
		schedule();

		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			goto out2;
		}

		mutex_lock(&dev->mutex);
	}

	if (count > GLOBALMEM_SIZE - dev->current_len)
		count = GLOBALMEM_SIZE - dev->current_len;

	if (copy_from_user(dev->mem + dev->current_len, buf, count)) {
		ret = -EFAULT;
		goto out;
	} else {
		dev->current_len += count;
		printk(KERN_INFO "write %lu byte(s) current_len:%d\n", count, dev->current_len);

		/* 写进程完成了，唤醒可能阻塞的读进程 */
		wake_up_interruptible(&dev->r_wait);
		ret = count;
	}
	
out:
	mutex_unlock(&dev->mutex);
out2:
	remove_wait_queue(&dev->w_wait, &wait);
	set_current_state(TASK_RUNNING);
	
	return ret;
}

static unsigned int global_mem_poll(struct file *filp, poll_table *wait)
{
	unsigned int mask =0;
	struct global_mem_dev *dev = filp->private_data;

	mutex_lock(&dev->mutex);

	poll_wait(filp, &dev->r_wait, wait);
	poll_wait(filp, &dev->w_wait, wait);

	if (dev->current_len != 0) {
		mask |= POLLIN | POLLRDNORM;
	}

	if (dev->current_len != GLOBALMEM_SIZE) {
		mask |= POLLOUT | POLLWRNORM;
	}

	mutex_unlock(&dev->mutex);

	return mask;
}


static loff_t global_mem_llseek(struct file * filp, loff_t offset, int orig)
{
	loff_t ret = 0;
	switch (orig) {
	case 0:
		if (offset < 0) {
			ret = -EINVAL;
			break;
		}
		
		if ((unsigned int)offset > GLOBALMEM_SIZE) {
			ret = -EINVAL;
			break;
		}

		/* 表示文件从头开始seek */
		filp->f_pos = (unsigned int)offset; 
		ret = filp->f_pos;
		break;
		
	case 1:
		if ((filp->f_pos + offset) > GLOBALMEM_SIZE) {
			ret = -EINVAL;
			break;
		}

		if ((filp->f_pos + offset) < 0) {
			ret = -EINVAL;
			break;
		}

		/* 表示文件从当前位置开始seek */
		filp->f_pos += offset;
		ret = filp->f_pos;
		break;

	default:
		ret = -EINVAL;
		break;
	}	

	return ret;
}

long global_mem_ioctl(struct file *filp, unsigned int cmd, unsigned long args)
{
	long ret = 0;
	struct global_mem_dev *dev = filp->private_data;
	
	switch (cmd){
	case MEM_CLEAR:
		mutex_lock(&dev->mutex);
		memset(dev->mem, 0, GLOBALMEM_SIZE);
		mutex_unlock(&dev->mutex);
		printk(KERN_INFO "global mem is set to zero\n");
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

struct file_operations global_mem_fops = {
	.owner = THIS_MODULE,
	.open = global_mem_open,
	.release = global_mem_release,
	.read = global_mem_read,
	.write = global_mem_write,
	.unlocked_ioctl = global_mem_ioctl,
	.llseek = global_mem_llseek,
	.poll = global_mem_poll,
};


static int __init global_mem_init(void)
{
	int ret = 0;
	int i = 0;
	struct global_mem_dev *global_mem_devp_tmp;

	/* 向内核申请设备号,申请多个设备(DEVICE_NUM)，共用主设备号 */
	ret = alloc_chrdev_region(&devno, 0 , DEVICE_NUM, "global_mem");
	if (ret < 0) {
		printk(KERN_INFO "Fail to alloc chrdev region, ret:%d\n", ret);
	}
	printk(KERN_INFO "chrdev alloc success, major:%d, minor:%d\n", MAJOR(devno), MINOR(devno));

	/* 把设备添加到内核 */
	global_mem_devp = (struct global_mem_dev *)kzalloc(sizeof(struct global_mem_dev)* DEVICE_NUM, GFP_KERNEL);
	if (!global_mem_devp) {
		ret = -ENOMEM;
		goto fail_malloc;
	}

	/* 将设备注册到内核 在c语言中->优先级高于&*/
	global_mem_devp_tmp = global_mem_devp;
	for (i = 0; i < DEVICE_NUM; i++) {
		mutex_init(&(global_mem_devp_tmp + i)->mutex);
		init_waitqueue_head(&(global_mem_devp_tmp + i)->r_wait);
		init_waitqueue_head(&(global_mem_devp_tmp + i)->w_wait);
		cdev_init(&(global_mem_devp_tmp + i)->cdev, &global_mem_fops);
		(global_mem_devp_tmp + i)->cdev.owner = THIS_MODULE;
		ret = cdev_add(&(global_mem_devp_tmp + i)->cdev, MKDEV(MAJOR(devno), i), 1);
		if (ret)
			printk(KERN_INFO "ERR %d add globalmem device\n", ret);
	}

	/* 创建class */
	globalmem_class =class_create(THIS_MODULE, "global_mem_class");
		if (IS_ERR(globalmem_class)) {
			printk(KERN_INFO "creat globalmem_class failed!\n");
			return -1;
	}

	/* 将设备挂在global_mem class 下 */
	for (i = 0; i < DEVICE_NUM; i++) {
		device_create(globalmem_class, NULL, MKDEV(MAJOR(devno), i), NULL, *(chr_dev_name + i));
	}

	return 0;

fail_malloc:
	unregister_chrdev_region(devno, DEVICE_NUM);
	return ret;
}

static void __exit global_mem_exit(void)
{
	int i = 0;
	
	for (i = 0; i < DEVICE_NUM; i++)
		cdev_del(&(global_mem_devp + i)->cdev);

	kfree(global_mem_devp);
	unregister_chrdev_region(devno, DEVICE_NUM);

	for (i = 0; i < DEVICE_NUM; i++)
		device_destroy(globalmem_class, MKDEV(MAJOR(devno), i));

	class_destroy(globalmem_class);

	printk(KERN_INFO "global_mem exit success.\n");
}

module_init(global_mem_init);
module_exit(global_mem_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("liweijie");
