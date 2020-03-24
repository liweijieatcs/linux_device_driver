# 编写snull程序
## **内容简介**
这是一篇手把手实现LDD3第17章：网络驱动程序的记录过程(其实我是把程序调试完了再来写的总结文章)。
```
确认内核版本
$uname -r
4.15.0-88-generic
```

## **模块程序的框架**
 这是内核的模块驱动(module driver)，那先就把模块的框架和对应的Makefile写好。<br>
 构成模块的文件为snull.c snull.h Makefile（为什么要多加一个snull.h文件呢？可以不要的）<br>
 Makefile文件如下
 ```makefile
 obj-m += snull.o

 all:
    make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules

clean:
    make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean
 ```

模块驱动框架程序如下：
```c
#include <linux/module.h>

int snull_module_init(void)
{
    int ret = 0;
    return ret;
}
void snull_module_exit(void)
{
    return;
}

module_init(snull_module_init);
module_exit(snull_module_exit);

```
## **网络驱动编写的指导思想**
最简单的网络驱动程序做以下三件事情：<br>
-  从网卡上收取数据包发给协议栈
-  从协议栈解析数据包，通过网卡发出去
-  统计状态：发了多少个包，收了多少个包，丢了多少包等状态的统计。<br>
  
这里涉及到网卡，数据包，协议栈，收取，收发。都要用软件来表示，如果让你自己来写，你怎么用软件来表示：<br>
内核中用**net_device**表示一个网络设备。<br>
使用**skb**表示一个数据包。<br>
<br>**数据包的接收**<br>
网卡驱动程序不涉及协议栈，但是数据包从网卡收了要发到协议栈,要从协议栈取数据包通过网卡发出去，那协议栈至少要给个接口吧。<br>
数据包先要到达网卡的接收寄存器，触发中断，这时候接收中断就产生了，这时候数据是放在网卡上的，我首先要把数据弄到内存吧，那我就要申请内存，内核提供的skb的内存申请接口**dev_alloc_skb**，有内存了，我就可以用**memcpy**把数据包从网卡拷贝到内存，完成pkt到skb的组装。<br>
调用协议栈的收取接口**netif_rx**，数据包传给协议栈<br>
<br>**数据包的发送**<br>
发送的数据包是来自协议栈的，应用程序的内容，什么时候发送，由协议栈决定，就是把数据包skb给驱动程序，驱动程序解析出其中的数据，这时候也要通过**memcpy**把处于内存的skb拷贝到网卡的寄存器。在把pkt写好之后，就通知硬件可以发送了。硬件就执行发送。这个发送函数集成在**net_dev**中，是个函数指针，**.ndo_start_xmit**，只需要挂载发送函数即可。<br>
<br>**协议栈控制函数**<br>
为了协调协议栈和网卡之间的数据交流，应该能想到，应用程序到到协议栈，肯定比协议栈到网卡的速度快。处于速度慢的一方，肯定要有机制来通知快的一方，以我的"速度"为准.当我准备好了，我就通知你给我发送，但是当我处理不过来了，我就得通知你停止发送。<br>
使用**netif_start_queue**通知协议栈说可以发送。使用**netif_stop_queue**通知协议栈停止发送。还有一种情况，就是发到一半，停止了，我要使用**netif_wake_queue**再次通知协议栈发送。<br>
<br>**网卡的状态统计**<br>
为了明确知道网卡发送和接收的状态，需要在发送和接收的过程中统计。应该在什么地方统计呢？<br>
- 一个skb送给协议栈了，这个包才算完全的接收，这时候接收统计就增加(RX packets)。
- 一个skb通过网卡发送出去了，发送完成，这时候发送统计就增加(TX packets)。
- 当一个包从协议栈下来了，但是没有发出去，都应该统计到丢包(dropped)。
- 当我传输超时后，就应该把这个包统计在错误包(errors)。
- 等等
## **为网络设备申请内存**
```c
#include <linux/netdevice.h>
申请函数：
#define alloc_netdev(sizeof_priv, name, name_assign_type, setup) \
	alloc_netdev_mqs(sizeof_priv, name, name_assign_type, setup, 1, 1)

继续看这个宏定义的调用函数
struct net_device *alloc_netdev_mqs(int sizeof_priv, const char *name,
				    unsigned char name_assign_type,
				    void (*setup)(struct net_device *),
				    unsigned int txqs, unsigned int rxqs);
释放函数：
void free_netdev(struct net_device *dev);
```
按照内核提供的函数填写如下<br>
首先函数返回的是一个net_devices的指针，那就先定义2个指针数组用于存储返回值
```c
struct net_device *snull_devs[2];
```
第一个参数是这个网络驱动的私有变量，这个私有变量，不知道有什么内容，但至少应该有一个net_device的结构，因为这是个网络设备，后面用到什么的时候再逐次添加
```c
struct snull_priv = {
    struct net_device *dev;
};
```
第二个参数是网络设备的名字，如果只是一个的话，直接写出，比如sn0，但是有多个的话，就需要写成sn%d。后续申请的可以安好编号递增。<br>
第三个参数NET_NAME_UNKNOWN,直接翻译成中文就是，网络名字不可知，查看内核中的定义写到,不要暴露给用户空间，那这里有个疑问了，怎么才能暴露给用户空间，暴露了有什么用，作为往后提高的部分再深入研究。
```c
/* interface name assignment types (sysfs name_assign_type attribute) */
#define NET_NAME_UNKNOWN	0	/* unknown origin (not exposed to userspace) */
```
最后一个是网络设备初始化函数，一个网络设备要初始化的内容是什么呢？不论怎样，先写上。
```c
void snull_init(struct net_device *dev)
{
    return;
}
```
填好这些基本的数据结构之后就调用**alloc_netdev**函数，在这里申请两个网络设备
```c
snull_devs[0] = alloc_netdev(sizeof (struct snull_priv), "sn%d", NET_NAME_UNKNOWN, snull_init);
snull_devs[1] = alloc_netdev(sizeof (struct snull_priv), "sn%d", NET_NAME_UNKNOWN, snull_init);
```
对应的在模块退出的时候，要对该内存块进行清除
```c
for (i = 0; i < 2; i++) {
    free_netdev(snull_devs[i])
}
```
这里也可以看出，为何要把snull_devs设置成全局，**因为在init和exit函数中都要用到该指针。**
## **注册网络设备到内核**
内核提供的注册函数和注销函数
```c
#include <linux/netdevice.h>
int register_netdev(struct net_device *dev);
void unregister_netdev(struct net_device *dev);

/net/core/dev.c
/**
 *	register_netdev	- register a network device
 *	@dev: device to register
 *
 *	Take a completed network device structure and add it to the kernel
 *	interfaces. A %NETDEV_REGISTER message is sent to the netdev notifier
 *	chain. 0 is returned on success. A negative errno code is returned
 *	on a failure to set up the device, or if the name is a duplicate.
 *
 *	This is a wrapper around register_netdevice that takes the rtnl semaphore
 *	and expands the device name if you passed a format string to
 *	alloc_netdev.
 */
int register_netdev(struct net_device *dev)
{
	int err;

	rtnl_lock();
	err = register_netdevice(dev);
	rtnl_unlock();
	return err;
}
EXPORT_SYMBOL(register_netdev);
```
调用了register_netdevice(dev),注册函数是有返回值的，所以在写代码的时候要检测该返回值,在这里
```c
for (i = 0; i < 2; i++) {
    if ((result = register_netdev(snull_devs[i]))) { //这里要写两个括号，避免编译器警告
        printk(KERN_INFO "snull: erro:%d register.\n", result);
    } else {
        printk(KERN_INFO "snull register success.\n")
    }
}
```
只写这两步的时候，运行make和sudo insmod snull.ko，内核显示空指针处理错误，为什么？<br>
对于一个设备，必须要在初始化函数中写上对他的操作函数，即使操作函数为空。需要修改snull_init函数<br>
```c
struct net_device_ops snull_netdev_ops = {};
void snull_init(struct net_device *dev)
{
    dev->netdev_ops = &snull_netdev_ops;
    return;
}
```
到此，我们就写好了网络驱动的框架代码，全部代码如下
```c
#include <linux/module.h>
#include <linux/netdevice.h>

void snull_module_exit(void);

struct snull_priv {
	struct net_device dev;
};

struct net_device *snull_devs[2];

struct net_device_ops snull_netdev_ops = {

};

void snull_init(struct net_device *dev)
{
	dev->netdev_ops = &snull_netdev_ops;
}

int snull_module_init(void)
{
	int ret = -ENOMEM;
	int i = 0;
	int result = 0;

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

out:
	if (ret)
		snull_module_exit();

	return 0;
}

void snull_module_exit(void)
{
	int i;

	for (i = 0; i < 2; i++) {
		if (snull_devs[i]) {
			unregister_netdev(snull_devs[i]);
			free_netdev(snull_devs[i]);
		}
	}

	return;
}

module_init(snull_module_init);
module_exit(snull_module_exit);

MODULE_LICENSE("Dual BSD/GPL");
```
对代码进行编译(make),将模块加载(sudo insmod snull.ko)到内核，可以从内核的sysfs文件夹下看到我们注册的网络模块
```c
$ ls /sys/class/net
sn0 sn1
```
用ifconfig -a也可以看到我们的网卡已经注册上去。
```c
sn0       Link encap:AMPR NET/ROM  HWaddr   
          [NO FLAGS]  MTU:0  Metric:1
          RX packets:0 errors:0 dropped:0 overruns:0 frame:0
          TX packets:0 errors:0 dropped:0 overruns:0 carrier:0
          collisions:0 txqueuelen:1000 
          RX bytes:0 (0.0 B)  TX bytes:0 (0.0 B)

sn1       Link encap:AMPR NET/ROM  HWaddr   
          [NO FLAGS]  MTU:0  Metric:1
          RX packets:0 errors:0 dropped:0 overruns:0 frame:0
          TX packets:0 errors:0 dropped:0 overruns:0 carrier:0
          collisions:0 txqueuelen:1000 
          RX bytes:0 (0.0 B)  TX bytes:0 (0.0 B)
```
## **为网络设备增加操作函数**
我们刚才添加的网络设备，什么都不能做，只是注册到内核，接下来我们就要为网络设备添加操作。网络设备是**net_device**，那net_device的本质是decive.<br>
但是网络设备有他的特殊性，他是用来收发数据包的。查看内核中对该操作都定义了哪些函数<br>
都是一些函数指针，这体现了面向对象设计的思想，相当于虚函数，每个具体的网卡可以具体的实现对应的函数<br>
没错，这个结构体非常大，做内核的人好耐心。因为net_device表示了一切的网络设备，比如网卡，网桥等，所以要兼容所有设备的操作。针对以太网，我们需要，打开设备，关闭设备，发送数据包，超时处理，更改MAC地址，更改MTU长度等，用到哪个就添加哪个。
### **设备的打开**
首先就是open函数。在该函数中，会调用netif_start_queue()
```c
int snull_open(struct net_device *dev)
{
	netif_start_queue(dev);
	return 0;
}
```
这是什么意思？看内核代码:允许上层调用设备的发送例程
```c
#include <linux/netdevice.h>
static __always_inline void netif_tx_start_queue(struct netdev_queue *dev_queue)
{
	clear_bit(__QUEUE_STATE_DRV_XOFF, &dev_queue->state);
}

/**
 *	netif_start_queue - allow transmit
 *	@dev: network device
 *
 *	Allow upper layers to call the device hard_start_xmit routine.
 */
static inline void netif_start_queue(struct net_device *dev)
{
	netif_tx_start_queue(netdev_get_tx_queue(dev, 0));
}
```
### **设备的关闭**
与打开设备对应的是关闭设备
```c
int snull_release(struct net_device *dev)
{
	netif_stop_queue(dev);
	return 0;
}
```
内核代码:
```c
#include <linux/netdevice.h>
static __always_inline void netif_tx_stop_queue(struct netdev_queue *dev_queue)
{
	set_bit(__QUEUE_STATE_DRV_XOFF, &dev_queue->state);
}

/**
 *	netif_stop_queue - stop transmitted packets
 *	@dev: network device
 *
 *	Stop upper layers calling the device hard_start_xmit routine.
 *	Used for flow control when transmit resources are unavailable.
 */
static inline void netif_stop_queue(struct net_device *dev)
{
	netif_tx_stop_queue(netdev_get_tx_queue(dev, 0));
}

```
### **设备的收包流程（rx）**
网络设备的主要功能是收发数据包，当数据包到达网卡时，硬件是最先知道的，硬件知道了，当然要通过中断的形式告知内核，数据包到来，收数据包。<br>
想想数据到达硬件，他是放在哪儿的，只能放在硬件，硬件哪里可以放数据？只有寄存器。当我们得知有数据包到来时，我们首先做的事情就是把数据从硬件的寄存器拷贝到内存，要拷贝内存，首先你要有内存吧，没有的话，那就申请吧。<br>
内核申请skb的函数**dev_alloc_skb**
```c
#include <linux/skbbuf.h>
/* legacy helper around netdev_alloc_skb() */
static inline struct sk_buff *dev_alloc_skb(unsigned int length)
{
	return netdev_alloc_skb(NULL, length);
}

/**
 *	netdev_alloc_skb - allocate an skbuff for rx on a specific device
 *	@dev: network device to receive on
 *	@length: length to allocate
 *
 *	Allocate a new &sk_buff and assign it a usage count of one. The
 *	buffer has unspecified headroom built in. Users should allocate
 *	the headroom they think they need without accounting for the
 *	built in space. The built in space is used for optimisations.
 *
 *	%NULL is returned if there is no free memory. Although this function
 *	allocates memory it can be called from an interrupt.
 */
static inline struct sk_buff *netdev_alloc_skb(struct net_device *dev,
					       unsigned int length)
{
	return __netdev_alloc_skb(dev, length, GFP_ATOMIC);
}
```
申请完的下一步就是将物理层的pkt拷贝到skb中，用memcpy函数，需要知道源地址，目的地址，长度
```c
memcpy(skb_put(skb, pkt->datalen), pkt->data, pkt->datalen);
```
然后填上协议类型，统计一些变量，再调用netif_rx送到协议栈，到此收据的收取流程就结束了。
```c
int netif_rx(struct sk_buff *skb);
```

### **设备的发包流程（tx）**
首先确定发的包是从哪儿来的？数据包是从协议栈来的。要通过网卡发出去，网卡这个硬件只认识硬件寄存器，所以要包协议栈到来的skb想把法写到硬件的寄存器。<br>
我在写的这个过程中，我是不能再次接收的，为什么？因为我只有有限的寄存器，还没有写完，等我发完了你再写吧，那怎么知道我发完了呢？<br>
当我写完了，我就改变我的状态，我可以写了，在每次中断例程中都去查询这个状态，如果查到我是发完的状态，就执行下一次数据协议，然后发送。<br>
协议栈下来的是skb，首先，要把skb中的数据部分解析出来**data = skb->data**，这时候还要把这个skb记录在设备的私有变量中**priv->skb = skb**，以便我们的发送完后，把这个skb对应的内存释放掉**dev_kfree_skb(priv->skb)**。<br>
当把数据解析出来后，就通过**memcpy(tx_buffer->data, buf, len);**把数据拷贝到网卡的寄存器中。通常情况下是写网卡的是能发送寄存器。网卡就把数据包发送出去了。

### **snull的设计**
```
                                              |-----------|
			                      |           |
		        	    |-------->|  remote0  |
			            |         |192.168.0.2|
			            |         |           |
				    |         |-----------|
				    |
		|-----------|       |
		|        sn0|<------|
		|192.168.0.1|
		|           |
		|        sn1|<------|
		|192.168.1.2|	    |
		|-----------|	    |
				    |
				    |         |-----------|
				    |	      |           |
	                            |-------->|  remote1  |
					      |192.168.1.1|
					      |           |
					      |-----------|								
```
sull网络拓扑如上所示：
当执行ping remote0 -I sn0时，数据包的发送流程是**192.168.0.1--->192.168.0.2**，此时会调用snull的发送函数。在发送函数中直接修改IP地址的第3个oct。并重新构建检验和。
```c
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
```
改变之后，数据流量相当于**192.168.1.1--->192.168.1.2**sn1就接收到了sn0发过来的数据包。<br>
sn0收到数据包之后，要回复该数据包，发送的数据流是**192.168.1.2--->192.168.1.1**，此时也调用了snull_tx函数，修改了IP地址，变为**192.168.0.2--->192.168.0.1**
