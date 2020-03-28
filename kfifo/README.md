# 分析kfifo

kfifo也是内核常用的数据结构，今天分析kfifo。fifo:first in first out先进先出。这至少会涉及到数据的入队，数据的出队。先想想如果自己来实现一个fifo该怎么设计：如果设计的话，只需要满足语义，我会用链表或者数组来实现，数组效率更高,设计思路：<br>
先申请一片数组作为fifo的大小，然后设计两个指针。入队指针指向数组的尾部，数组增加，指针加1，出队指针指向数组的头部，数据出队之后，出队指针数组加1.<br>
考虑特殊处理：入队指针和出队指针到达数组的尾部时指针都要重新跳转到数组头部，这是一个循环fifo。<br>
来看看内核的设计思路。

## 数据结构和初始化

kfifo相关的操作位于头文件#include <linux/kfifo.h><br>
使用一个kfifo，主要定义3个参数，访问这个fifo的入口，fifo中存储的数据类型，以及fifo的长度。<br>
```c
#define FIFO_SIZE 32
static (test, unsigned char, FIFO_SIZE);
表示存储长度为32，每个fifo中存储的是一个字节，fifo的头部使用test来访问
```

把 DECLARE_KFIFO展看看看
```c
/**
 * DECLARE_KFIFO - macro to declare a fifo object
 * @fifo: name of the declared fifo
 * @type: type of the fifo elements
 * @size: the number of elements in the fifo, this must be a power of 2
 */
#define DECLARE_KFIFO(fifo, type, size)	STRUCT_KFIFO(type, size) fifo

宏定义STRUCT_KFIFO是用到了type和size
#define STRUCT_KFIFO(type, size) \
	struct __STRUCT_KFIFO(type, size, 0, type)

继续看__STRUCT_KFIFO
#define __STRUCT_KFIFO(type, size, recsize, ptrtype) \
{ \
	__STRUCT_KFIFO_COMMON(type, recsize, ptrtype); \
	type		buf[((size < 2) || (size & (size - 1))) ? -1 : size]; \
}
这个宏第1步定义了__STRUCT_KFIFO_COMMON，这个一看就是公共的东西。
#define __STRUCT_KFIFO_COMMON(datatype, recsize, ptrtype) \
	union { \
		struct __kfifo	kfifo; \
		datatype	*type; \
		const datatype	*const_type; \
		char		(*rectype)[recsize]; \
		ptrtype		*ptr; \
		ptrtype const	*ptr_const; \
	}
主要是第2步骤： type buf[],是个数组，数组大小要么返回-1，要么返回size。当size小于2(也就是0或者1)。
```

定义完数组后，需要对该数组初始化

```c
/**
 * INIT_KFIFO - Initialize a fifo declared by DECLARE_KFIFO
 * @fifo: name of the declared fifo datatype
 */
#define INIT_KFIFO(fifo) \
(void)({ \
	typeof(&(fifo)) __tmp = &(fifo); \
	struct __kfifo *__kfifo = &__tmp->kfifo; \
	__kfifo->in = 0; \
	__kfifo->out = 0; \
	__kfifo->mask = __is_kfifo_ptr(__tmp) ? 0 : ARRAY_SIZE(__tmp->buf) - 1;\
	__kfifo->esize = sizeof(*__tmp->buf); \
	__kfifo->data = __is_kfifo_ptr(__tmp) ?  NULL : __tmp->buf; \
})

涉及到通用结构体
struct __kfifo {
	unsigned int	in;
	unsigned int	out;
	unsigned int	mask;
	unsigned int	esize;
	void		*data;
};
```
初始化完后，更新了各个成员变量，data的指针，指向了DECLARE_KFIFO定义的buf数组的首地址。这之后就可以通过该首地址操作这个fifo。<br>

## 分析kfifo_in
知道数据结构和初始化之后，接着分析两个最重要的操作kfifo_in(入队操作),kfifo_out(出队操作)。
```c
/**
 * kfifo_in - put data into the fifo
 * @fifo: address of the fifo to be used
 * @buf: the data to be added
 * @n: number of elements to be added
 *
 * This macro copies the given buffer into the fifo and returns the
 * number of copied elements.
 *
 * Note that with only one concurrent reader and one concurrent
 * writer, you don't need extra locking to use these macro.
 */
#define	kfifo_in(fifo, buf, n) \
({ \
	typeof((fifo) + 1) __tmp = (fifo); \
	typeof(__tmp->ptr_const) __buf = (buf); \
	unsigned long __n = (n); \
	const size_t __recsize = sizeof(*__tmp->rectype); \
	struct __kfifo *__kfifo = &__tmp->kfifo; \
	(__recsize) ?\
	__kfifo_in_r(__kfifo, __buf, __n, __recsize) : \
	__kfifo_in(__kfifo, __buf, __n); \
})
依据__recsize决定是调用__kfifo_in_r还是__kfifo_in，在初始化的时候，__recsize=0，继续调用__kfifo_in

unsigned int __kfifo_in(struct __kfifo *fifo,
		const void *buf, unsigned int len)
{
	unsigned int l;

	l = kfifo_unused(fifo);
	if (len > l)
		len = l;

    /* 调用memcpy将数据拷贝到fifo->data */
	kfifo_copy_in(fifo, buf, len, fifo->in);
	fifo->in += len;
	return len;
}

static void kfifo_copy_in(struct __kfifo *fifo, const void *src,
		unsigned int len, unsigned int off)
{
	unsigned int size = fifo->mask + 1;
	unsigned int esize = fifo->esize;
	unsigned int l;

	off &= fifo->mask;
	if (esize != 1) {
		off *= esize;
		size *= esize;
		len *= esize;
	}
	l = min(len, size - off);
    /* 这里的2个memcpy值得分析
     * 当即将入队的数据大于空闲部分，就先用完空闲部分，然后把剩下的拷贝到数据的头部
     * /
	memcpy(fifo->data + off, src, l);
	memcpy(fifo->data, src + l, len - l);
	/*
	 * make sure that the data in the fifo is up to date before
	 * incrementing the fifo->in index counter
	 */
    /* 使用内存屏障保证了这次写完才能变换fifo的长度 */
	smp_wmb();
}
```

## 分析kfifo_out
直接看代码
```c
/**
 * kfifo_out - get data from the fifo
 * @fifo: address of the fifo to be used
 * @buf: pointer to the storage buffer
 * @n: max. number of elements to get
 *
 * This macro get some data from the fifo and return the numbers of elements
 * copied.
 *
 * Note that with only one concurrent reader and one concurrent
 * writer, you don't need extra locking to use these macro.
 */
#define	kfifo_out(fifo, buf, n) \
__kfifo_uint_must_check_helper( \
({ \
	typeof((fifo) + 1) __tmp = (fifo); \
	typeof(__tmp->ptr) __buf = (buf); \
	unsigned long __n = (n); \
	const size_t __recsize = sizeof(*__tmp->rectype); \
	struct __kfifo *__kfifo = &__tmp->kfifo; \
	(__recsize) ?\
	__kfifo_out_r(__kfifo, __buf, __n, __recsize) : \
	__kfifo_out(__kfifo, __buf, __n); \
}) \
)
继续调用__kfifo_out
unsigned int __kfifo_out(struct __kfifo *fifo,
		void *buf, unsigned int len)
{
	len = __kfifo_out_peek(fifo, buf, len);
	fifo->out += len;
	return len;
}

继续深入__kfifo_out_peek
unsigned int __kfifo_out_peek(struct __kfifo *fifo,
		void *buf, unsigned int len)
{
	unsigned int l;

	l = fifo->in - fifo->out;
	if (len > l)
		len = l;

	kfifo_copy_out(fifo, buf, len, fifo->out);
	return len;
}
EXPORT_SYMBOL(__kfifo_out_peek);
```
核心函数kfifo_copy_out()
```c
static void kfifo_copy_out(struct __kfifo *fifo, void *dst,
		unsigned int len, unsigned int off)
{
	unsigned int size = fifo->mask + 1;
	unsigned int esize = fifo->esize;
	unsigned int l;

	off &= fifo->mask;
	if (esize != 1) {
		off *= esize;
		size *= esize;
		len *= esize;
	}
	l = min(len, size - off);

	memcpy(dst, fifo->data + off, l);
	memcpy(dst + l, fifo->data, len - l);
	/*
	 * make sure that the data is copied before
	 * incrementing the fifo->out index counter
	 */
	smp_wmb();
}
```
这个和入队差不多的情况，实现环形队列主要就是两次memcpy来实现。<br>

看了内核的基本思路和自己想的差不多，关键是告诉了你思路，你能实现吗？有内核实现得好吗？<br>
