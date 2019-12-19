#include <linux/module.h>	/* for moudule_init &module_eixt */

#include <linux/blkdev.h>	/* for register_blkdev() */


#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/hdreg.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/version.h>


#define BIO_ENDIO(bio, status)	bio_endio(bio)

static int vmem_disk_major;
module_param(vmem_disk_major, int, 0);

#define HARDSECT_SIZE 512
#define NSECTORS 1024
#define NDEVICES 4

#define VMEM_DISK_MINORS	16
#define KERNEL_SECTOR_SHIFT	9
#define KERNEL_SECTOR_SIZE	(1 << KERNEL_SECTOR_SHIFT)

enum {
	VMEMD_QUEUE,
	VMEMD_NOQUEUE,
};

static int request_mode = VMEMD_QUEUE;
module_param(request_mode, int, 0);


struct vmem_disk_dev {
	int size;
	u8 *data;
	int users;
	int media_changes;
	spinlock_t lock;
	struct request_queue *queue;
	struct gedisk *gd;
};

static struct vmem_disk_dev *devices = NULL;

/* handle an io request */
static void vmem_disk_transfer(struct vmem_disk_dev *dev, unsigned long sector,
								unsigned long nsect, char *buffer, int write)
{
	unsigned long offset = sector * KERNEL_SECTOR_SIZE;
	unsigned long nbytes = nsect * KERNEL_SECTOR_SIZE;

	if ((offset + nbytes) > dev->size) {
		pr_info("Beyond-end write (%ld %ld)\n", offset, nbytes);
		return;
	}
	
	if (write)
		memcpy(dev->data + offset, buffer, nbytes);
	else
		memcpy(buffer, dev->data + offset, nbytes);
}

/* transfor a sigle bio */
static int vmem_disk_xfer_bio(struct vmem_disk_dev *dev, struct bio *bio)
{
	struct bio_vec bvec;
	struct bvec_iter iter;
	sector_t sector = bio->bi_iter.bi_sector;

	bio_for_each_segment(bvec, bio, iter) {
		char *buffer = kmap_atomic(bvec.bv_page) + bvec.bv_offset;

		vmem_disk_transfer(dev, sector,
							bio_cur_bytes(bio) >> KERNEL_SECTOR_SHIFT,
							buffer, bio_data_dir(bio) == WRITE);

		sector += bio_cur_bytes(bio) >> KERNEL_SECTOR_SHIFT;
		kunmap_atomic(buffer);
	}
	
	return 0;
}

/* direct make request version */
static blk_qc_t vmem_disk_make_request(struct request_queue *q, struct bio *bio)
{
	struct vmem_disk_dev *dev = q->queuedata;
	int status;

	status = vmem_disk_xfer_bio(dev, bio);
	BIO_ENDIO(bio, status);

	return BLK_QC_T_NONE;
}

/* The request_queue version. */
static void vmem_disk_request(struct request_queue *q)
{
	struct request *req;
	struct bio *bio;

	while ((req = blk_peek_request(q)) != NULL) {
		struct vmem_disk_dev *dev = req->rq_disk->private_data;

		if (req->cmd_type != REQ_TYPE_FS) {
			pr_info("Skip non-fs request\n");
			blk_start_request(req);
			__blk_end_request_all(req, -EIO);
			continue;
		}

		blk_start_request(req);
		__rq_for_each_bio(bio, req)
			vmem_disk_xfer_bio(dev, bio);
		__blk_end_request_all(req, 0);
	}
}


static void setup_device(struct vmem_disk_dev *dev, int which)
{
	memset(dev, 0, sizeof(struct vmem_disk_dev));
	dev->size = NSECTORS * HARDSECT_SIZE;
	dev->data = vmalloc(dev->size);

	if (dev->data == NULL) {
		printk(KERN_INFO "vmalloc failure\n");
		return;
	}

	spin_lock_init(&dev->lock);

	switch (request_mode) {
	case VMEMD_NOQUEUE:
		dev->queue = blk_alloc_queue(GFP_KERNEL);

		if (dev->queue == NULL) {
			goto out_vfree;
		}

		blk_queue_make_request(dev->queue, vmem_disk_make_request);
		break;
	default:
		printk(KERN_INFO "Bad request mode %d, using simple\n", request_mode);
	case VMEMD_QUEUE:
		dev->queue = blk_init_queue(vmem_disk_request, &dev->lock);
		if (dev->queue == NULL)
			goto out_vfree;

		break;

	}

out_vfree:
		if (dev->data)
			vfree(dev->data);

}

static int __init vmem_disk_init(void)
{
	int i = 0;
	int vmem_disk_major = 0;

	vmem_disk_major = register_blkdev(vmem_disk_major, "vmem_disk");

	if (vmem_disk_major <= 0) {
		printk(KERN_INFO "vmem_disk: unable to get major number\n");
		return -EBUSY;
	}
	printk(KERN_INFO "major:%d", vmem_disk_major);

	devices = kmalloc(NDEVICES * sizeof(struct vmem_disk_dev), GFP_KERNEL);
	if (devices)
		goto out_unregister;

	for (i = 0; i < NDEVICES; i++) {
		setup_device(devices + i, i);
	}

	return 0;

out_unregister:
	unregister_blkdev(vmem_disk_major, "sbd");
	return -ENOMEM;
}

static void __exit vmem_disk_exit(void)
{

}

module_init(vmem_disk_init);
module_exit(vmem_disk_exit);

MODULE_LICENSE("GPL");

