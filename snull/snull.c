/*
 * snull.c --  the Simple Network Utility
 *
 * Copyright (C) 2020.3.24 liweijie<ee.liweijie@gmail.com>
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 *
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/etherdevice.h>
#include <linux/tcp.h>

#define SNULL_RX_INTR 0x0001
#define SNULL_TX_INTR 0x0002

int pool_size = 8;
module_param(pool_size, int, 0);

void snull_module_exit(void);
static void (*snull_interrupt)(int, void *, struct pt_regs *);

struct net_device *snull_devs[2];
struct snull_packet {
	struct snull_packet *next;
	struct net_device *dev;
	int	datalen;
	u8 data[ETH_DATA_LEN];
};

struct snull_priv {
	struct net_device *dev;
	struct sk_buff *skb;
	struct snull_packet *ppool;
	struct snull_packet *rx_queue;
	spinlock_t lock;
	int status;
	int rx_int_enabled;
	int tx_packetlen;
	u8 *tx_packetdata;
	struct net_device_stats stats;
	struct napi_struct napi;
};

struct snull_packet *snull_get_tx_buffer(struct net_device *dev)
{
	struct snull_priv *priv = netdev_priv(dev);
	unsigned long flags;
	struct snull_packet *pkt;

	spin_lock_irqsave(&priv->lock, flags);
	/* 让pkt指向下一个pkt,如果数据包被取完了，通知内核，要求停止发送 */
	pkt = priv->ppool;
	priv->ppool = pkt->next;

	if (priv->ppool == NULL) {
		printk (KERN_INFO "Pool empty\n");
		netif_stop_queue(dev);
	}

	spin_unlock_irqrestore(&priv->lock, flags);
	return pkt;
}

void snull_release_buffer(struct snull_packet *pkt)
{
	unsigned long flags;
	struct snull_priv *priv = netdev_priv(pkt->dev);
	
	spin_lock_irqsave(&priv->lock, flags);
	pkt->next = priv->ppool;
	priv->ppool = pkt;
	spin_unlock_irqrestore(&priv->lock, flags);

	if (netif_queue_stopped(pkt->dev) && pkt->next == NULL)
		netif_wake_queue(pkt->dev);
}

void snull_setup_pool(struct net_device *dev)
{
	struct snull_priv *priv = netdev_priv(dev);
	int i;
	struct snull_packet *pkt;

	priv->ppool = NULL;
	for (i = 0; i < pool_size; i++) {
		pkt = kmalloc (sizeof (struct snull_packet), GFP_KERNEL);
		if (pkt == NULL) {
			printk (KERN_NOTICE "Ran out of memory allocating packet pool\n");
			return;
		}
		pkt->dev = dev;
		pkt->next = priv->ppool;
		priv->ppool = pkt;
		printk(KERN_INFO "%d name:%s, pkt:0x%lx, priv:0x%lx,priv->ppool:0x%lx\n",
				i, dev->name, (unsigned long)pkt, (unsigned long)priv, (unsigned long)priv->ppool);
	}
	printk(KERN_INFO "create snull pool\n");
}

void snull_teardown_pool(struct net_device *dev)
{
	struct snull_priv *priv = netdev_priv(dev);
	struct snull_packet *pkt;

	while ((pkt = priv->ppool)) {
	priv->ppool = pkt->next;
	printk(KERN_INFO "name:%s, pkt:0x%lx, priv:0x%lx,priv->ppool:0x%lx\n",
				dev->name, (unsigned long)pkt, (unsigned long)priv, (unsigned long)priv->ppool);
	kfree (pkt);
	}
}

void snull_enqueue_buf(struct net_device *dev, struct snull_packet *pkt)
{
	unsigned long flags;
	struct snull_priv *priv = netdev_priv(dev);

	spin_lock_irqsave(&priv->lock, flags);
	pkt->next = priv->rx_queue;
	priv->rx_queue = pkt;
	spin_unlock_irqrestore(&priv->lock, flags);
}

struct snull_packet *snull_dequeue_buf(struct net_device *dev)
{
	struct snull_priv *priv = netdev_priv(dev);
	struct snull_packet *pkt;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	pkt = priv->rx_queue;
	if (pkt != NULL)
		priv->rx_queue = pkt->next;
	spin_unlock_irqrestore(&priv->lock, flags);
	return pkt;
}

static void snull_rx_ints(struct net_device *dev, int enable)
{
	struct snull_priv *priv = netdev_priv(dev);
	priv->rx_int_enabled = enable;
}

int snull_open(struct net_device *dev)
{
	if (dev == snull_devs[0])
		memcpy(dev->dev_addr, "\0SNUL0", ETH_ALEN);
	else
		memcpy(dev->dev_addr, "\0SNUL1", ETH_ALEN);

	netif_start_queue(dev);
	printk(KERN_INFO "snull open\n");

	return 0;
}

int snull_release(struct net_device *dev)
{
	netif_stop_queue(dev);
	printk(KERN_INFO "snull release\n");

	return 0;
}

/*
 * 接收数据包：检索，封装并传递到更高层
 */
void snull_rx(struct net_device *dev, struct snull_packet *pkt)
{
	struct sk_buff *skb;
	struct snull_priv *priv;
	priv = netdev_priv(dev);

	/* 为接收包分配一个skb,+2是为了下面的skb_reserve使用 */
	skb = dev_alloc_skb(pkt->datalen + 2);
	if (!skb) {
		if (printk_ratelimit())
			printk(KERN_NOTICE "snull rx: low on mem - packet dropped\n");
		priv->stats.rx_dropped++;
		goto out;
	}

	/* 16字节对齐，即IP首部前是网卡硬件地址首部，
	 * 占用14个字节（原地址：6，目的地址：6，类型：2）
	 * 需要将其增加2 */
	skb_reserve(skb, 2);
	/* 开辟一个缓冲区用于存放接收数据 */
	memcpy(skb_put(skb, pkt->datalen), pkt->data, pkt->datalen);

	/* Write metadata, and then pass to the receive level */
	skb->dev = dev;
	if (skb->dev == snull_devs[0])
		printk(KERN_INFO "skb->dev is snull_devs[0]\n");
	else
		printk(KERN_INFO "skb->dev is snull_devs[1]\n");
	/* 确定包的协议 */
	skb->protocol = eth_type_trans(skb, dev);
	printk(KERN_INFO "skb->protocol:%d\n", skb->protocol);
	skb->ip_summed = CHECKSUM_UNNECESSARY; /* don't check it */
	/* 统计接收包数和字节数 */
	priv->stats.rx_packets++;
	priv->stats.rx_bytes += pkt->datalen;
	/* 上报应用层 */
	netif_rx(skb);
out:
	return;
}

/*
 * 数据的收发都要依靠中断,在中断中要处理rx tx中断
 */
static void snull_regular_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	int statusword;
	struct snull_priv *priv;
	struct snull_packet *pkt = NULL;

	struct net_device *dev = (struct net_device *)dev_id;

	/* paranoid */
	if (!dev)
		return;

	/* Lock the device */
	priv = netdev_priv(dev);

	spin_lock(&priv->lock);
	statusword = priv->status;
	priv->status = 0;
	/*
	 * 数据包到来，产生接收中断 调用接收函数
	 * 在接收函数中申请skb，将收到的pkt,拷贝到skb中
	 * 调用netif_rx，传递给协议栈
	 */
	if (statusword & SNULL_RX_INTR) {
		printk(KERN_INFO "---start %s rx process---\n", dev->name);
		printk(KERN_INFO "name:%s enter the rx interrupt\n", dev->name);
		pkt = priv->rx_queue;
		if (pkt) {
			priv->rx_queue = pkt->next;
			/* 网卡接收到数据，上报给应用层 */
			snull_rx(dev, pkt);
		}
		printk(KERN_INFO "--- stop %s rx process---\n", dev->name);
	}

	/* 数据包传输完成，产生传输中断
	 * 统计发送的包数和字节数，并释放这个包的内存 */
	if (statusword & SNULL_TX_INTR) {
		printk(KERN_INFO "name:%s enter the tx interrupt\n", dev->name);
		priv->stats.tx_packets++;
		priv->stats.tx_bytes += priv->tx_packetlen;
		dev_kfree_skb(priv->skb);
	}
	spin_unlock(&priv->lock);

	if (pkt)
		snull_release_buffer(pkt); /* Do this outside the lock! */
	printk(KERN_INFO "snull regular interrupt\n");

	return;
}

static void snull_hw_tx(char *buf, int len, struct net_device *dev)
{
	struct iphdr *ih;
	struct net_device *dest;
	struct snull_priv *priv;
	u32 *saddr, *daddr;
	struct snull_packet *tx_buffer;


	/* 以太网头部14字节，IP头部20个字节，*/
	if (len < sizeof(struct ethhdr) + sizeof(struct iphdr)) {
		printk("snull: Hmm... packet too short (%i octets)\n", len);
		return;
	}
	/*
	 * 打印上层应用层要发的包的内容
	 * 14字节以太网首都 + 20字节IP地址首都 + 20字节TCP地址首部 + n字节数据
	 */
	if (0) {
		int i = 0;
		printk(KERN_INFO "ethernet header:\n");
		for (i = 0; i < 14; i++)
			printk("%3d:%02x", i, buf[i] & 0xff);
		printk(KERN_INFO "IP header:\n");
		for (i = 14; i < 34; i++)
			printk("%3d:%02x", i, buf[i] & 0xff);
		printk(KERN_INFO "TCP header:\n");
		for (i = 34; i < 54; i++)
			printk("%3d:%02x", i, buf[i] & 0xff);
		printk(KERN_INFO "data:\n");
		for (i = 54; i < len; i++)
			printk("%3d, %02x", i, buf[i] & 0xff);
	}
	/*
	 * Ethhdr is 14 bytes, but the kernel arranges for iphdr
	 * to be aligned (i.e., ethhdr is unaligned)
	 */
	/* 提取本地和目标的IP地址 */
	ih = (struct iphdr *)(buf + sizeof(struct ethhdr));
	saddr = &ih->saddr;
	daddr = &ih->daddr;

	printk(KERN_INFO "ih->protocol = %d is buf[23]\n", ih->protocol);

	printk(KERN_INFO "txbe %s:saddr:%d.%d.%d.%d -->daddr:%d.%d.%d.%d\n", dev->name,
				((u8 *)saddr)[0], ((u8 *)saddr)[1], ((u8 *)saddr)[2],((u8 *)saddr)[3],
				((u8 *)daddr)[0], ((u8 *)daddr)[1], ((u8 *)daddr)[2],((u8 *)daddr)[3]);

	/* 修改原地址，目的地址 */
	((u8 *)saddr)[2] ^= 1;
	((u8 *)daddr)[2] ^= 1;
	printk(KERN_INFO "txaf %s:saddr:%d.%d.%d.%d -->daddr:%d.%d.%d.%d\n",dev->name,
				((u8 *)saddr)[0], ((u8 *)saddr)[1], ((u8 *)saddr)[2],((u8 *)saddr)[3],
				((u8 *)daddr)[0], ((u8 *)daddr)[1], ((u8 *)daddr)[2],((u8 *)daddr)[3]);

	/* IP改变，重新构建校验和 */
	ih->check = 0;
	ih->check = ip_fast_csum((unsigned char *)ih, ih->ihl);

	/* 打印变更后的地址和TCP地址 */
	if (dev == snull_devs[0])
		printk(KERN_INFO "name:%s, %08x:%05i --> %08x:%05i\n",
				dev->name,
				ntohl(ih->saddr),ntohs(((struct tcphdr *)(ih+1))->source),
				ntohl(ih->daddr),ntohs(((struct tcphdr *)(ih+1))->dest));
	else
		printk(KERN_INFO "name:%s,%08x:%05i <-- %08x:%05i\n",
				dev->name,
				ntohl(ih->daddr),ntohs(((struct tcphdr *)(ih+1))->dest),
				ntohl(ih->saddr),ntohs(((struct tcphdr *)(ih+1))->source));

	/*
	 * 数据包准备好了
	 * 要模拟两个中断：一个是在接收端模拟接收中断，另一个实在发送端模拟发送完成中断
	 * 通过设置私有变量的状态来模拟priv->status
	 */
	//dest = snull_devs[dev == snull_devs[0] ? 1 : 0]; /* 如果源是snull_devs[0],目的则是snull_devs[1] */
	/* 获取目的网卡地址 */
	if (dev == snull_devs[0]) {
		dev = snull_devs[0];
		dest = snull_devs[1];
		printk(KERN_INFO "snull_devs[0]\n");
	} else {
		dev = snull_devs[1];
		dest = snull_devs[0];
		printk(KERN_INFO "snull_dev[1]\n");
	}
	
	/* 处理目的端：接收 */
	priv = netdev_priv(dest);
	/* 取出一块内存，分配给本地网卡 */
	tx_buffer = snull_get_tx_buffer(dev);
	/* 设置数据包大小 */
	tx_buffer->datalen = len;
	printk(KERN_INFO "tx_buffer->datalen = %d\n", tx_buffer->datalen);
	/* 填充发送网卡的数据 */
	memcpy(tx_buffer->data, buf, len);
	/* 把发送的数据直接加入到接收队列
	 * 这里相当于本地网卡要发送的数据已经给目标网卡直接接收到了 
	 */
	snull_enqueue_buf(dest, tx_buffer);
	if (priv->rx_int_enabled) {
		priv->status |= SNULL_RX_INTR;	/* 目的端收到数据包之后，模拟触发接收中断 */
		snull_interrupt(0, dest, NULL);
		printk(KERN_INFO "priv->status = %d\n", priv->status);
	}

	/* 处理源端：发送 */
	priv = netdev_priv(dev);
	/* 把本地网卡要发送的数据存到私有数据缓冲区 */
	priv->tx_packetlen = len;
	priv->tx_packetdata = buf;
	/* 模拟产生一个发送中断 */
	priv->status |= SNULL_TX_INTR;	/* 源端发送完了，触发发送中断 */
	snull_interrupt(0, dev, NULL);
	printk(KERN_INFO "snull_interrupt(0, dev, NULL)\n");
}

/* 
 * tx函数是协议栈决定何时调用
 * 在初始化网络设备时，挂接.ndo_start_xmit	    = snull_tx
 */
int snull_tx(struct sk_buff *skb, struct net_device *dev)
{
	int len;
	char *data, shortpkt[ETH_ZLEN];
	struct snull_priv *priv = netdev_priv(dev);

	/* 获取上层要发送的数据和长度 */
	data = skb->data;
	len = skb->len;
	printk(KERN_INFO "skb->len = %d\n", skb->len);
	printk(KERN_INFO "***start %s tx process***\n", dev->name);
	printk(KERN_INFO "name:%s data_len:%d\n", dev->name, len);
	/* 如果小于60字节，用0填充，最终修改了data,len*/
	if (len < ETH_ZLEN) {
		memset(shortpkt, 0, ETH_ZLEN);
		memcpy(shortpkt, skb->data, skb->len);
		len = ETH_ZLEN;
		data = shortpkt;
	}

	/* 
	 * 用私有变量记录skb，以便在发送完成
	 * 调用中断的时候，释放skb 
	 */
	priv->skb = skb;
	/* 模拟把数据写入硬件，通过硬件发送出去，实际不是 */
	snull_hw_tx(data, len, dev);
	printk(KERN_INFO "****stop %s tx process***\n", dev->name);

	return 0; /* Our simple device can not fail */
}

struct net_device_stats *snull_stats(struct net_device *dev)
{
	struct snull_priv *priv = netdev_priv(dev);
	return &priv->stats;
}

int snull_header(struct sk_buff *skb, struct net_device *dev,
			unsigned short type, const void *daddr, const void *saddr,
			unsigned len)
{
	struct ethhdr *eth = (struct ethhdr *)skb_push(skb,ETH_HLEN);

	printk(KERN_INFO "---begin create the %s header---\n", dev->name);
	printk(KERN_INFO "len = %d\n", len);

	printk(KERN_INFO "type = 0x%04x\n", type); //ETH_P_IP    0x0800        /* Internet Protocol packet    */

	/* 
	 * 将整形变量从主机字节序转变为网络字节序
	 * 就是整数在地址空间的存储方式变为：高位字节存放在内存的低地址处
	 */
	eth->h_proto = htons(type);
	printk(KERN_INFO "h_proto = 0x%04x\n", eth->h_proto);
	printk(KERN_INFO "addr_len = %d\n", dev->addr_len);
	printk(KERN_INFO "dev_addr = %02x.%02x.%02x.%02x.%02x.%02x\n",
		dev->dev_addr[0], dev->dev_addr[1], dev->dev_addr[2],
		dev->dev_addr[3], dev->dev_addr[4], dev->dev_addr[5]);
	if (saddr) {
		printk("saddr = %02x.%02x.%02x.%02x.%02x.%02x\n",
		*((unsigned char *)saddr + 0),
		*((unsigned char *)saddr + 1),
		*((unsigned char *)saddr + 2),
		*((unsigned char *)saddr + 3),
		*((unsigned char *)saddr + 4),
		*((unsigned char *)saddr + 5));
	}

	if (daddr) {
		printk("daddr = %02x.%02x.%02x.%02x.%02x.%02x\n",
		*((unsigned char *)daddr + 0),
		*((unsigned char *)daddr + 1),
		*((unsigned char *)daddr + 2),
		*((unsigned char *)daddr + 3),
		*((unsigned char *)daddr + 4),
		*((unsigned char *)daddr + 5));
	}
	/* 上层应用数据，通过下层添加硬件地址，才能决定发送到目标网卡 */
	memcpy(eth->h_source, saddr ? saddr : dev->dev_addr, dev->addr_len);
	memcpy(eth->h_dest,   daddr ? daddr : dev->dev_addr, dev->addr_len);

	printk(KERN_INFO "h_source = %02x.%02x.%02x.%02x.%02x.%02x\n",
		eth->h_source[0], eth->h_source[1], eth->h_source[2],
		eth->h_source[3], eth->h_source[4], eth->h_source[5]);
	printk(KERN_INFO "  h_dest = %02x.%02x.%02x.%02x.%02x.%02x\n",
		eth->h_dest[0], eth->h_dest[1], eth->h_dest[2],
		eth->h_dest[3], eth->h_dest[4], eth->h_dest[5]);

	/*
	 * 设置目标网卡硬件地址，即本地网卡和目标网卡硬件地址最后一个字节的低有效位
	 * 是相反关系，即本地是\0snull0的话，目标就是\0snull1
	 * 或者本地是\0snull1,目标就是\0snull0
	 */
	eth->h_dest[ETH_ALEN-1]   ^= 0x01;   /* dest is us xor 1 */
	printk(KERN_INFO "h_dest[ETH_ALEN-1] ^ 0x01 = %02x\n", eth->h_dest[ETH_ALEN-1]);
	printk(KERN_INFO "hard_header_len = %d\n", dev->hard_header_len);
	
	printk(KERN_INFO "---end of create the %s header---\n", dev->name);

	return (dev->hard_header_len);
}

/*
* The "change_mtu" method is usually not needed.
* If you need it, it must be like this.
*/
int snull_change_mtu(struct net_device *dev, int new_mtu)
{
	unsigned long flags;
	struct snull_priv *priv = netdev_priv(dev);
	spinlock_t *lock = &priv->lock;

	/* check ranges */
	if ((new_mtu < 68) || (new_mtu > 1500))
		return -EINVAL;
	/*
	* Do anything you need, and the accept the value
	*/
	spin_lock_irqsave(lock, flags);
	dev->mtu = new_mtu;
	spin_unlock_irqrestore(lock, flags);
	return 0; /* success */
}

static const struct header_ops snull_header_ops = {
	.create = snull_header,
};

struct net_device_ops snull_netdev_ops = {
	.ndo_open	     = snull_open,
	.ndo_stop	     = snull_release,
	.ndo_start_xmit      = snull_tx,
	.ndo_get_stats	     = snull_stats,
};

void snull_init(struct net_device *dev)
{
	struct snull_priv *priv;

	ether_setup(dev); /* assign some of the fields */

	dev->header_ops = &snull_header_ops;
	dev->netdev_ops = &snull_netdev_ops;

	dev->flags |= IFF_NOARP;
	dev->features |= NETIF_F_HW_CSUM;

	priv = netdev_priv(dev);
	memset(priv, 0, sizeof(struct snull_priv));
	
	spin_lock_init(&priv->lock);
	snull_rx_ints(dev, 1);
	snull_setup_pool(dev);
	printk(KERN_INFO "snull init\n");
}

void snull_module_exit(void)
{
	int i;

	for (i = 0; i < 2; i++) {
		if (snull_devs[i]) {
			unregister_netdev(snull_devs[i]);
			snull_teardown_pool(snull_devs[i]);
			free_netdev(snull_devs[i]);
		}
	}

	return;
}

int snull_module_init(void)
{
	int ret = -ENOMEM;
	int i = 0;
	int result = 0;

	snull_interrupt = snull_regular_interrupt;

	snull_devs[0] = alloc_netdev(sizeof(struct snull_priv), "sn%d", NET_NAME_UNKNOWN, snull_init);
	snull_devs[1] = alloc_netdev(sizeof(struct snull_priv), "sn%d", NET_NAME_UNKNOWN, snull_init);

	if (snull_devs[0] == NULL || snull_devs[1] == NULL)
		goto out;

	ret = -ENODEV;
	for (i = 0; i < 2; i++) {
		if ((result = register_netdev(snull_devs[i]))) {
			printk(KERN_INFO "snull: error :%d register device:%s\n", result, snull_devs[i]->name);
		} else {
			ret = 0;
		}
	}
	printk(KERN_INFO "snull init module\n");

out:
	if (ret)
		snull_module_exit();

	return 0;
}

module_init(snull_module_init);
module_exit(snull_module_exit);

MODULE_LICENSE("Dual BSD/GPL");
