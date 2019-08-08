# linux 设备驱动开发详解 调试记录
## 参考书籍：
- 《linux 设备驱动开发详解》 宋宝华编著
### 第6章 字符设备 global_mem 驱动
- 通过以下几个步骤来调试
1. linux 模块的框架搭建，不涉及任何驱动程序，包括模块的加载与卸载
2. 字符设备的region的申请，alloc_chrdev_region / unregister_chrdev_region
3. 具体字符设备的初始化和把设备加入内核，定义chrdev,并向内核申请内存（kzlloc），对chrdev进行初始化（cdev_init），将chrdev加入内核(cdev_add),在移除模块是要将设备从内核移除(cdev_del),释放内存（kfree）
4. 实现chrdev的fops,open, release, write, read, 在open的时候，将字符设备的指针赋值给filp->private_data.
5. 在用户态验证驱动，mknod /dev/g_m_0 c 243, 0生成设备节点，通过echo "hell0" > /dev/g_m_0对设备进行写入操作，通过cat /dev/g_m_0 对设备进行读取操作，由于驱动程序是在内核空间运行，在内核空间和用户空间进行数据的交互需要用到内核提供的函数 copy_from(to)_user
6. 修改驱动以支持N个设备

