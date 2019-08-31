## ucore Lab2

lab 2 直接执行`make qemu-nox`会显示 assert 失败:

```
kernel panic at kern/mm/default_pmm.c:277:
    assertion failed: (p0 = alloc_page()) == p2 - 1
```

## 1 连续物理内存管理

## 1.1 page 概览

对物理内存的管理,为了节省空间,也是为了配合接下来的虚拟内存管理,通常以某个比 byte 大一些的单位进行管理,我们称这一单位内存为一"**页(page)**",通常是 4KB.待 `pages` 初始化完毕后,物理内存示意图如下:

![](/images/物理页示意图.png)

其中绿色代表可以分配的内存,红色代表不可被分配的内存.注意,`ucore`规定物理内存可用范围最大不超过`KERNSIZE`.函数`page_init`的主要作用就是初始化`pages`也就是所有`page`的所有信息.

注意,`pages`以全局指针的形式存在,因为最开始无法知道 `page` 的数量,所以无法写成数量确定的数组.此数量必须尽快确认,否则后期无法管理.

如何确定 `page` 的数量`npage`呢?

## 1.1 探测物理内存布局,获取 pages 大小

 `npages`可由最大物理内存边界/PGSIZE 得出.

而最大物理内存边界可以借助 BIOS 可以探测并计算出来,参考[探测系统物理内存布局](https://chyyuu.gitbooks.io/ucore_os_docs/content/lab2/lab2_3_3_2_search_phymem_layout.html)和[实现物理内存探测](https://chyyuu.gitbooks.io/ucore_os_docs/content/lab2/lab2_3_6_implement_probe_phymem.html).可以获取到最大可用物理内存边界`maxpa`, 但`maxpa` 最终必须<=`KMEMSIZE`.

于是确认了`npage`和`pages`:

```C
npage = maxpa / PGSIZE;
pages = (struct Page *)ROUNDUP((void *)end, PGSIZE);// 从end向上取整页
```

## 1.2 确定每个 page 的状态

每个`page` 的状态即其四个字段:

```C
struct Page {
    int ref;                // 虚存引用计数,暂不关心,都设为 0
    uint32_t flags;         // 当前页状态位.解释见下
    unsigned int property;  // (对于 first fit)用于可用区域内存的第一个 page,记录其之后有多少个 page 是 free 的
    list_entry_t page_link; // 上/下一个空闲 page
};
```

**对于状态的解释** : 在当前阶段,此字段理解为是否可用.通过`SetPageReserved`来标记其为保留(即不可用)的.

注意使用`free_list`作为双良链表的表头,应使用相关函数维护好关系.

参考[以页为单位管理物理内存
](https://chyyuu.gitbooks.io/ucore_os_docs/content/lab2/lab2_3_3_3_phymem_pagelevel.html)

## 练习 1 重写内存管理函数

参考 严蔚敏老师的 *数据结构(C 语言版)* 8.2 节 "首次拟合法". 

**`first fit` 算法要求空闲 block 按起始地址有序排列.**

default 系列的内存管理函数,注释已经描述的非常清楚了,这里再描述下算法原理.

`first-fit` 的分配算法, 核心步骤如下: 
1. 找到 property > n 的节点 base_page
2. 从 base_page 步进 n 个 page 找到 p
3. 设置 p 的 property,值为 property - n 
4. 把 p 作为新节点直接加入链表
5. 删除 base_page

释放的情况比较多,释放page后有四种情况:
1. 空闲块与其之前的空闲块相邻,与之后的空闲块不相邻
2. 空闲块与其之前的空闲块相邻,与之后的空闲块相邻
3. 空闲块与其之前的空闲块不相邻,与之后的空闲块不相邻
4. 空闲块与其之前的空闲块相邻,与之后的空闲块也相邻
