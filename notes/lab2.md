# ucore Lab2

## 实验要点

- 以页为单位实现物理内存管理

## 1 非连续物理内存管理

## 1.1 page 概览

对物理内存的管理,为了节省空间,也是为了配合接下来的虚拟内存管理,通常以某个比 byte 大一些的单位进行管理,我们称这一单位内存为一"**页(page)**",通常是 4KB.待 `pages` 初始化完毕后,物理内存示意图如下:

![](https://github.com/libinyl/ucore-study/blob/master/images/%E7%89%A9%E7%90%86%E9%A1%B5%E7%A4%BA%E6%84%8F%E5%9B%BE.png?raw=true)

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
    int ref;                // 虚存引用计数, 实际上是被页表项引用的数量.每有一个页表项指向此 page,ref 就+1
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

## 练习 2 实现寻找虚拟地址对应的页表项

完成函数:

```
pte_t *
get_pte(pde_t *pgdir, uintptr_t la, bool create) {
```

此函数的作用是向上提供一个几乎透明的操作,返回指定 linear address 对应的 page table entry.注意写这个函数时有坑,**此函数并非要初始化对应的页表项,而是单纯的获取其地址!** 初始化页表项的在`boot_map_segment`中进行.

1. 得到 page dir 的 index: PDX(la),于是对应的 pde 是 pgdir[PDX(la)].

2. 考察 pde 具体的结构:

![](https://github.com/libinyl/CS-notes/blob/master/images/intel/v3/Figure%204-4.%20Formats%20of%20CR3%20and%20Paging-Structure%20Entries%20with%2032-Bit%20Paging.png?raw=true)

高 20 位是 page table 地址.

pgdir[PSX(la)]& ~0xFFF即可得到 `page table` 的物理地址.

在取值之前,首先要判断存在位PTE_P.
1. 如果存在,说明之前已经初始化过了 pd,只需计算la对应的页表项即可.
1. 如果不存在,说明当前 page directory entry 除了 PTE_P 外都是空的!更别说页表了,肯定也是不存在.此时需要新申请一块物理内存,作为新的页表.但如果 create=0 的话就直接返回 NULL 就行了.

当前的页目录是`__boot_pgdir`,而我们之前只初始化了虚拟地址的`[0,4M)`和`KERNBASE + [0 ~ 4M)`的页表,而未初始化其他部分.其他部分是 0.新申请`page`作为页表之后,只是把页表的(物理)基址写到 pd 项中而已.**此函数可能更新pd,但不会更新pt**.

```C
pte_t *
get_pte(pde_t *pgdir, uintptr_t la, bool create) {
    // 1. 由线性地址取page directory 中对应的条目
    pde_t *pdep = &pgdir[PDX(la)];
    // 2.1 若存在位为 0,则需要判断 create 选项.
    if (!(*pdep & PTE_P)) {
        struct Page *page;
        // 2.1.1 若 create=0 则返回 NULL
        if (!create)
            return NULL;
        // 2.1.2 若 create=1 则分配一块物理内存,作为新的页表
        if (page = alloc_page() == NULL) {
            return NULL;
        }
        // 2.1.3 设置此 page 的引用计数
        set_page_ref(page, 1);
        // 2.1.4 修改 page directory 项的标志位,把新页表地址写入此项.
        uintptr_t pa = page2pa(page);
        memset(KADDR(pa), 0, PGSIZE);
        *pdep = pa | PTE_U | PTE_W | PTE_P;
    }
    // 2.2 若存在位不为 0,则返回页表项地址.
    //      1. 对 *pdep 取高 20 位得到页表(物理)基址
    //      2. 用KADDR将页表物理基址换算为内核虚拟地址
    //      2. 从页表虚拟基址取 PTX(la) 个偏移量得到页表项,返回它的地址.
    return &((pte_t *)KADDR(PDE_ADDR(*pdep)))[PTX(la)];
}
```

## 练习3 释放某虚地址所在的页并取消对应二级页表项的映射

编写函数:

```C
// 释放给定页表ptep关联的page
// 去使能地址 la 对应的 tlb.
static inline void
page_remove_pte(pde_t *pgdir, uintptr_t la, pte_t *ptep)
```

用到函数:`page_ref_dec`, 即减少引用.

```C
    // 排除页表不存在的情况
    if (*ptep & PTE_P) {
        struct Page *page = pte2page(*ptep);
        if (page_ref_dec(page) == 0) {
            free_page(page);
        }
        *ptep = 0;
        tlb_invalidate(pgdir, la);
    }
```