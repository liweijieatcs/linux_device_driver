#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

static int __init dma_demo_init(void)
{
        void *va = NULL;
        unsigned long pa;

        va = kmalloc(12, GFP_KERNEL); 
        pa = __pa(va);
        printk(KERN_INFO "kmalloc virtual addr:0x%p, pa:0x%lx\n", va, pa);
        va = __va(pa);
        printk(KERN_INFO "kmalloc virtual addr:0x%p, pa:0x%lx\n", va, pa);
        kfree(va);

        va = vmalloc(123);
        pa = __pa(va);
        printk(KERN_INFO "vmalloc virtual addr:0x%p, pa:0x%lx\n", va, pa);
        va = __va(pa);
        printk(KERN_INFO "vmalloc virtual addr:0x%p, pa:0x%lx\n", va, pa);
        vfree(va);
        
        return 0;
}

static void __exit dma_demo_exit(void)
{
        return;
}

module_init(dma_demo_init);
module_exit(dma_demo_exit);
