#include <linux/module.h>

static int __init global_mem_init(void)
{
    printk(KERN_INFO "global_mem init success.\n");
    return 0;
}

static void __exit global_mem_exit(void)
{
    printk(KERN_INFO "global_mem exit success.\n");
    return;
}

module_init(global_mem_init);
module_exit(global_mem_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("liweijie");
MODULE_VERSION("0.01");