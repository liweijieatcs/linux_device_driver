# 分析 container_of 宏
## 功能和使用
这个宏的作用是通过得知结构体中的某个成员变量的地址，然后找到结构体的首地址。<br>
举例说明：<br>
有个结构体：
```c
struct person {
    int age;
    int salary;
    char *name;
};

struct person leo;
int *salary_ptr = &(leon.salary);
```
得知这个结构体salary的指针:**salary_ptr**,如何能找到leo这个结构体变量的首地址。找到首地址之后，你可以通过这个首地址再去找该结构体中其他的变量<br>
先想一下，你来设计，你应该如何找到:直观感觉就是往回数，结构体在存储的时候，是顺序存储的，找到salary是第几个变量，然后减去之前的变量占用的空间，就是结构体的首地址了。还要考虑对齐的问题。暂时先把这个思路留在这里。继续往下看<br>

简单写个程序测试一下<br>
Makefile文件
```
obj-m += container_of.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
```
测试代码：container_of.c
```c
#include <linux/module.h>
#include <linux/kernel.h>

struct person {
    int age;
    int salary;
    char *name;
};

int container_of_init(void)
{
    struct person leo;
    struct person *leo_ptr;

    int *age_ptr = &(leo.age);
    int *salary_ptr = &(leo.salary);
    char **name_ptr = &(leo.name);

    leo_ptr = container_of(age_ptr, struct person, age);
    printk(KERN_INFO "addr of person:0x%p\n", leo_ptr);

    leo_ptr = container_of(salary_ptr, struct person, salary);
    printk(KERN_INFO "addr of person:0x%p\n", leo_ptr);

    leo_ptr = container_of(name_ptr, struct person, *name);
    printk(KERN_INFO "addr of person:0x%p\n", leo_ptr);

    printk(KERN_INFO "addr of age:0x%p, 0x%p\n", &(leo_ptr->age), &(leo.age));
    printk(KERN_INFO "addr of salary:0x%p, 0x%p\n", &(leo_ptr->salary), &(leo.salary));
    printk(KERN_INFO "addr of name:0x%p, 0x%p\n", leo_ptr->name, leo.name);

    return 0;
}

void container_of_exit(void)
{
    return;
}
module_init(container_of_init);
module_exit(container_of_exit);
```
输出了结果：通过成员变量，都找到了结构体的首地址，并且再通过结构体的首地址，找到了变量的地址。
```c
[158680.247539] addr of person:0x00000000e491801b
[158680.247541] addr of person:0x00000000e491801b
[158680.247542] addr of person:0x00000000e491801b
[158680.247542] addr of age:0x00000000e491801b, 0x00000000e491801b
[158680.247549] addr of salary:0x000000001e76c839, 0x000000001e76c839
[158680.247550] addr of name:0x0000000063561914, 0x0000000063561914
```
## 代码分析
container_of宏位于<linux/kernel.h>中，所以用的时候，要加上这个头文件。
```c
/**
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:	the pointer to the member.
 * @type:	the type of the container struct this is embedded in.
 * @member:	the name of the member within the struct.
 *
 */
#define container_of(ptr, type, member) ({				\
	void *__mptr = (void *)(ptr);					\
	BUILD_BUG_ON_MSG(!__same_type(*(ptr), ((type *)0)->member) &&	\
			 !__same_type(*(ptr), void),			\
			 "pointer type mismatch in container_of()");	\
	((type *)(__mptr - offsetof(type, member))); })
```
慢慢来理解这个宏，这个宏由两部分组成，一个是**BUILD_BUG_ON_MSG**，还有就是((type *)(__mptr - offsetof(type, member)));<br>
先看**BUILD_BUG_ON_MSG**，编译告警输出消息。
```c
/**
 * BUILD_BUG_ON_MSG - break compile if a condition is true & emit supplied
 *		      error message.
 * @condition: the condition which the compiler should know is false.
 *
 * See BUILD_BUG_ON for description.
 */
#define BUILD_BUG_ON_MSG(cond, msg) compiletime_assert(!(cond), msg)
```
是在编译阶段输出告警，举个例子：
```c
正确的指针类型定义：
char **name_ptr = &(leo.name);
错误的指针类型
int **name_ptr = &(leo.name);
```
由于用了错误的指针，会在编译阶段输出：
```c
./include/linux/compiler.h:324:38: error: call to ‘__compiletime_assert_25’ 
declared with attribute error: pointer type mismatch in container_of()
```
接着继续展开((type *)(__mptr - offsetof(type, member))); 
```c
void *__mptr = (void *)(ptr);
((type *)(__mptr - offsetof(type, member))); 

#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
```
将例子中的参数带入
```c
struct person {
    int age;
    int salary;
    char *name;
};
struct person leo;
int *salary_ptr = &(leo.salary);
leo_ptr = container_of(salary_ptr, struct person, salary);
展开后：
(struct person*)((void *)(salary_ptr) - ((size_t))&((struct person*)0->salary));
```
### 第1部分
(void *)(salary_ptr):这部分是结构体中salary的位置，用salary_ptr表示，将salary_ptr指针转换为void指针，这个很好理解。<br><br>
### 第2部分
((size_t))&((struct person*)0->salary))：这部分就是结构体首地址到salary的偏移量。<br>
由于只需要一个偏移量，那将结构体的首地址设置成从0开始，(struct person*)0,就构造了这个结构体，但是地址是0，但是这个结构体构造还是不变的，(struct person*)0->salary表示了结构体首部到salary的偏移量。<br>
这样取出来是结构体的成员，还要对这个成员进行取地址运算&，得到成员的地址。将这个地址转换为(size_t)类型，这样就表示了结构体首部到salary的偏移。<br>
### 计算偏移量
用第1部分减去第2部分，就得到了结构体的首地址，最后将该地址，转换成(struct person*)返回。<br>
