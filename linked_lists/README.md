# 分析linked_lists

在内核中使用链表的地方非常多，链表是将所有的节点通过指针串联起来，内核使用双链表。

## 普通的链表

我们通常认识的链表都这么定义

```c
struct node {
    int data;
    struct node *next;
    struct node *prev;
};
```

先定义一个数据，再加上一个前向指针，再加上以后后向指针，如果只是使用这个数据结构的话，似乎没有什么问题。<br>
如果许多结构体都要使用链表，这样定义，显得冗余：每个结构体都要加2个指针，还有针对每个结构体写增删查改。很麻烦，既然每个结构体都要使用这2个指针，那我就把这两个指针抽出来，单独处理，这样，数据和操作数据的方法进行了分离，有面向对象编程的思想:

```c
struct node {
    int data;
    struct list list_head;
};
```

## 内核链表的组成

内核把数据的链接部分剥离出来，就形成了内核linked_lists API。位于<linux/list.h><br>
链表API分为初始化，增删查改，遍历。好像对数据结构的操作也就这些，剩下的用到再具体分析。

## 初始化链表

还是举例说明吧,先定义了结构体，包括int data成员

```c
struct mystruct {
     int data ;
} ;
```

这个数据结构要使用链表来管理，就要在数据结构中再加入一个链表的变量

```c
struct mystruct {
    int data;
    struct list_head mylist;
};
```

struct list_head位于linux/type.h，定义如下

```c
struct list_head {
        struct list_head *next, *prev;
};
```

这样就可以通过mylist.next，mylist.prev来访问相邻的项。<br>
以上相当于双联表的数据结构定义完了，接下来就是多个节点怎么链接起来，只有先链接起来才可以操作他，假设定义了两个结构体,分别初始化他们，内核提供了两种方法

```c
第1种方法：
struct mystruct node1 = {
    .data = 99,
    .mylist = LIST_HEAD_INIT(node1.mylist),
};
按照宏展开：
#define LIST_HEAD_INIT(name) { &(name), &(name) }
.mylist = {&node1.mylist, &node1.mylist}

等同于结构体的指针分别指向自己：
.mylist = {
    .*next = &node1.mylist,
    .*prev = &node1.mylist,
},
------------------------------------------------------------------
第2种方法
struct mystruct node2;
node2.data = 99;
INIT_LIST_HEAD(&node2.mylist);
这是个inline函数：
static inline void INIT_LIST_HEAD(struct list_head *list)
{
        WRITE_ONCE(list->next, list);
        list->prev = list;
}
还是等同于结构体的指针分别指向自己
```

完成节点的定义，要把节点链接起来，还需要定义一个链表的表头

```c
LIST_HEAD(mylisthead);
展开这个宏：
#define LIST_HEAD(name) \
        struct list_head name = LIST_HEAD_INIT(name)
等于:
struct list_head mylisthead = LIST_HEAD_INIT(mylisthead);
继续展开：
struct list_head mylisthead = {
    .*next = &mylisthead,
    .*prev = &mylisthead,
};
相当于只是定义了头部，这个头部没有数据，只有前向后向指针，分别指向了自己
```

## 增加链表节点

接下来把定义的节点链接起来吧，内核提供了list_add

```c
list_add(&node1.mylist, &mylisthead);
lsit_add(&node2.mylist, &mylisthead);

展开list_add
/**
 * list_add - add a new entry
 * @new: new entry to be added
 * @head: list head to add it after
 *
 * Insert a new entry after the specified head.
 * This is good for implementing stacks.
 */
static inline void list_add(struct list_head *new, struct list_head *head)
{
        __list_add(new, head, head->next);
}

展开__list_add
/*
 * Insert a new entry between two known consecutive entries.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void __list_add(struct list_head *new,
                              struct list_head *prev,
                              struct list_head *next)
{
        if (!__list_add_valid(new, prev, next))
                return;

        next->prev = new;
        new->next = next;
        new->prev = prev;
        WRITE_ONCE(prev->next, new);
}
每次双链表增加要移动4次指针
```

## 删除链表节点

内核提供了**list_del**方法，想想如果自己来从双链表删除节点涉及哪些操作，因为是双链表，只需要传一个节点进来就行,自己实现一下

```c
void list_del(struct list_head *node)
{
    /* 定义两个临时变量存储当前节点的prev和next */
    prev = node->prev;
    next = node->next;

    /* 指针变换，相当于删除中间的节点 */
    prev->next = next;
    next->prev = prev;

    /* 将删除的节点指针置为NULL */
    node->prev = NULL;
    node->next = NULL;
}
```

```c
/**
 * list_del - deletes entry from list.
 * @entry: the element to delete from the list.
 * Note: list_empty() on entry does not return true after this, the entry is
 * in an undefined state.
 */
static inline void list_del(struct list_head *entry)
{
        __list_del_entry(entry);
        entry->next = LIST_POISON1;
        entry->prev = LIST_POISON2;
}

#define LIST_POISON1  ((void *) 0x00100100)
#define LIST_POISON2  ((void *) 0x00200200)

static inline void __list_del_entry(struct list_head *entry)
{
        if (!__list_del_entry_valid(entry))
                return;

        __list_del(entry->prev, entry->next);
}

/*
 * Delete a list entry by making the prev/next entries
 * point to each other.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void __list_del(struct list_head * prev, struct list_head * next)
{
        next->prev = prev;
        WRITE_ONCE(prev->next, next);
}
```

自己写代码的时候，把链表删除了，就把节点的prev和next置为空，内核却制定到了具体为止难道LIST_POISON，why? 有毒。<br>

## 遍历链表

如何遍历一个链表：先找到链表头，依次找到next指针，如果这个指针又指回了链表头，说明遍历完了整个链表。<br>
看内核实现：<br>
```c
struct list_head *position = NULL;
struct mystruct *datastructptr = NULL;
LIST_HEAD(mylisthead);

第1种遍历方法：先遍历链表，然后通过链表找到结构体的头部，再通过头部找到结构体的其他元素。
list_for_each(position, &mylisthead) {
    datastructptr = list_entry(position, struct mystruct, mylist);
    printk(KERN_INFO "data:%d\n", datastructptr->data);
}

遍历链表
list_for_each(position, &mylisthead)

/**
 * list_for_each        -       iterate over a list
 * @pos:        the &struct list_head to use as a loop cursor.
 * @head:       the head for your list.
 */
#define list_for_each(pos, head) \
        for (pos = (head)->next; pos != (head); pos = pos->next)

遍历链表后，找到每个节点的成员，内核用list_entry
/**
 * list_entry - get the struct for this entry
 * @ptr:        the &struct list_head pointer.
 * @type:       the type of the struct this is embedded in.
 * @member:     the name of the list_head within the struct.
 */
#define list_entry(ptr, type, member) \
        container_of(ptr, type, member)


第2中方法：将上面两步骤合二为一
list_for_each_entry(datastructptr, &mylisthead, mylist) {
    printk(KERN_INFO "data:%d\n", datastructptr->data);
}

/**
 * list_for_each_entry  -       iterate over list of given type
 * @pos:        the type * to use as a loop cursor.
 * @head:       the head for your list.
 * @member:     the name of the list_head within the struct.
 */
#define list_for_each_entry(pos, head, member)                          \
        for (pos = list_first_entry(head, typeof(*pos), member);        \
             &pos->member != (head);                                    \
             pos = list_next_entry(pos, member))

先找到第1个成员list_first_entry
/**
 * list_first_entry - get the first element from a list
 * @ptr:        the list head to take the element from.
 * @type:       the type of the struct this is embedded in.
 * @member:     the name of the list_head within the struct.
 *
 * Note, that list is expected to be not empty.
 */
#define list_first_entry(ptr, type, member) \
        list_entry((ptr)->next, type, member)

/**
 * list_entry - get the struct for this entry
 * @ptr:        the &struct list_head pointer.
 * @type:       the type of the struct this is embedded in.
 * @member:     the name of the list_head within the struct.
 */
#define list_entry(ptr, type, member) \
        container_of(ptr, type, member)

步长就是找到下一个list_next_entry
/**
 * list_next_entry - get the next element in list
 * @pos:        the type * to cursor
 * @member:     the name of the list_head within the struct.
 */
#define list_next_entry(pos, member) \
        list_entry((pos)->member.next, typeof(*(pos)), member)
```
