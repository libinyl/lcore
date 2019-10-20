# ucore Lab3

内存管理模块依赖关系概览:

![](/images/存储管理模块依赖图.png)

lab3 的主题是 `Page Fault`及对应的页替换算法。

我们事先虚拟内存的目的之一就是为每个用户态程序虚拟出来一个更大的内存空间。但事实上物理内存就那么点，到那里去用更多的部分呢？答案就是磁盘。而替换内存的代价又比较大，所以有必要设计合理的算法来保证性能。

虚拟内存的地址与物理地址不同,但访问每一个有效的虚拟地址最终一定会映射至某个物理地址,这就是内存地址的虚拟化.有了虚拟化的基址,我们就可以通过设置页表项来限制访问空间,完成内存访问保护的功能.

内存虚拟化的一大特性就是"按需分配",具体来说就是按需分页.

## 练习 1 给未被映射的地址映射上物理页

完成位于 `mm/vmm.c`的 `do_pgfault` 函数.

### 1.1 回顾中断机制,`page fault`是如何被触发的?

`page fault`是一种中断,参考手册 6.2 节:

>The allowable range for vector numbers is 0 to 255. Vector numbers in the range 0 through 31 are reserved by the Intel 64 and IA-32 architectures for architecture-defined exceptions and interrupts. Not all of the vector numbers in this range have a currently defined function. The unassigned vector numbers in this range are reserved. Do not use the reserved vector numbers.

0~31 号异常由处理器确定.分别是:

![](https://github.com/libinyl/CS-notes/blob/master/images/intel/v3/Table%206-1.%20Protected-Mode%20Exceptions%20and%20Interrupts.png?raw=true)

第 14 号就是我们看到的 page fault. 同时我们也可以看到一些眼熟的异常,如 `Stack-Segment Fault` 等.

也就是说,处理器帮我们做好了`page fault`的探测工作,一旦页不存在,就会触发 14 号中断.我们只需关心中断处理函数.

call graph:

```
访问缺页地址,处理器获取中断向量号 14->
    查询 idt(trap.[hc]),中断表由`idt_init`建立->
        查询`__vectors`(vector.S)->
            向量号入栈,跳转到`__alltraps`(trapentry.S)->
                `__alltraps`存储当前进程状态,调用trap->
                    `trap.c` 分发,处理中断
```

### 1.2 产生`page fault`异常的原因有哪些?

- 越界(非法)
- 写只读页的“非法地址”访问(非法)
- 数据被临时换出到磁盘上(合法)
- 没有分配内存(合法)


### 1.3 如何判断这些异常原因?

`page_fault`函数不知道哪些是“合法”的虚拟页，原因是ucore还缺少一定的数据结构来描述这种不在物理内存中的“合法”虚拟页。为此ucore通过建立`mm_struct`和`vma_struct`数据结构，描述了ucore模拟应用程序运行所需的合法内存空间。

当访问内存产生page fault异常时，可获得访问的内存的方式（读或写）以及具体的虚拟内存地址，这样ucore就可以查询此地址，看是否属于vma_struct数据结构中描述的合法地址范围中，如果在，则可根据具体情况进行请求调页/页换入换出处理（这就是练习2涉及的部分）；如果不在，则报错.(参考[链接](https://chyyuu.gitbooks.io/ucore_os_docs/content/lab3/lab3_3_3_data_structures.html))

### 1.4 用`mm_struct`和`vma_struct`解决合法性描述的问题

![](https://github.com/libinyl/ucore-study/blob/master/images/%E8%99%9A%E6%8B%9F%E5%9C%B0%E5%9D%80%E7%A9%BA%E9%97%B4%E5%92%8C%E7%89%A9%E7%90%86%E5%9C%B0%E5%9D%80%E7%A9%BA%E9%97%B4%E7%9A%84%E7%A4%BA%E6%84%8F%E5%9B%BE.png?raw=true)

每个进程维护着一个 `mm_struct`,作为内存描述块的头结点;以此为头节点,每个当前用到的合法的虚拟页都对应一串`vma_struct`.注意图中的倒数第二页虚拟内存是合法的,但是其没有对应的页表,也就是尚未分配.此时访问该虚拟内存就会触发 page_fault.

由图可知,**vma_struct 链表的作用是描述了虚拟内存的合法性,但实际是否已经分配了物理内存仍然是透明的.** 每块合法的连续的 page 组成一个 `vma`.

**考察`vma_struct`**

```
struct vma_struct {
    // 每个内存描述快都维护着头节点
    struct mm_struct *vm_mm;
    uintptr_t vm_start;     // vma 开始地址,与 PGSIZE 对齐
    uintptr_t vm_end;       // vma 结束地址,与 PGSIZE 对齐, vm_start < vm_end
    uint32_t vm_flags;      // vma 标志位
    //linear list link which sorted by start addr of vma
    list_entry_t list_link;
};
```
`vm_flags`的定义在`vmm.h`中:

```
#define VM_READ   0x00000001  //只读
#define VM_WRITE  0x00000002  //可读写
#define VM_EXEC   0x00000004  //可执行
```

**考察`vma_struct`**

```C
struct mm_struct {
    // 双向链表头
    list_entry_t mmap_list;
    // 当前被访问的 vma,出于性能考虑(局部性)添加进来.
    struct vma_struct *mmap_cache;
    pde_t *pgdir;   // mmap_list中的 vma 的 PDT
    int map_count;  // mmap_list中的 vma 的数量
    void *sm_priv;  // the private data for swap manager
};
```

### 1.5 内存描述符的操作函数

- vma_struct
  - vma_create
  - insert_vma_struct
  - find_vma
- mm_struct
  - mm_create
  - mm_destroy

### 1.6 产生页访问异常的原因以及基本动作

- 目标页帧不存在（页表项全为0，即该线性地址与物理地址尚未建立映射或者已经撤销)；
- 相应的物理页帧不在内存中（页表项非空，但Present标志位=0，比如在swap分区或磁盘文件上)
- 不满足访问权限(此时页表项P标志=1，但低权限的程序试图访问高权限的地址空间，或者有程序试图写只读页面).

当出现上面情况之一，那么就会产生页面page fault（#PF）异常。以下是执行流程:

1. CPU把产生异常的线性地址存储在CR2中
2. CPU在当前内核栈保存当前被打断的程序现场(EFLAGS，CS，EIP，errorCode)
3. CPU把异常中断号0xE对应的中断服务例程的地址（vectors.S中的标号vector14处）加载到CS和EIP寄存器中，开始执行中断服务例程
4. ucore开始处理异常中断:
5. 保存硬件没有保存的寄存器
   1. 在vectors.S中的标号vector14处先把中断号压入内核栈
   2. 然后再在trapentry.S中的标号__alltraps处把DS、ES和其他通用寄存器都压栈
   3. 自此，被打断的程序执行现场（context）被保存在内核栈中
6. trap函数开始中断服务例程的处理流程

>trap--> trap_dispatch-->pgfault_handler-->do_pgfault

**CR2 是什么?**

CR2 是`Page Fault`专用的线性地址寄存器.

参考 2.5:

>CR2 — Contains the page-fault linear address (the linear address that caused a page fault).

![](https://github.com/libinyl/CS-notes/blob/master/images/intel/v3/Figure%202-7.%20Control%20Registers.png?raw=true)


**errorcode 怎样表示页访问异常的类型?**

### 1.7 errorcode的表示

参考手册 6.13,errorcode 的通用定义:

![](https://github.com/libinyl/CS-notes/blob/master/images/intel/v3/Figure%206-6.%20Error%20Code.png?raw=true)


**EXT** External event (**bit 0**),值为 1 时表示硬件外部中断.

**IDT** Descriptor location (**bit 1**), 置为 1 时表示errorcode 的索引部分引用的是 IDT 的一个门描述符,置为 0 时表示引用 GDT 或者当前 LDT 的描述符.

**TI** (**bit 2**)只在 IDT 位为 0 时有用.此时 TI 表示errorcode 的索引部分是 LDT,为 1 是是 GDT.

**注意 Page fault 的错误码比较特殊** ,格式如下,参考 6.15 节 p6-40 **Interrupt 14—Page-Fault Exception (#PF)** 一章

![](https://github.com/libinyl/CS-notes/blob/master/images/intel/v3/Figure%206-9.%20Page-Fault%20Error%20Code.png?raw=true)

### 1.8 分析`do_pgfault`函数的逻辑


call graph:

```
trap_dispatch->
    pgfault_handler->
        do_pgfault
```

`do_pgfault`函数原型:

```C
int
do_pgfault(struct mm_struct *mm, uint32_t error_code, uintptr_t addr);
```

有了上面的分析,do_pgfault函数的参数也就清晰了,分别是:

- `mm_struct`: 进程内存合法性记录表的头结点
- `error_code`: 错误码,说明了异常的类型
- `addr`: cr2 寄存器中记录的当前要访问的目标线性地址

我们的 `do_pgfault`函数的处理流程也就清晰了:

1. 判断 `addr` 合法性
   1. 在 `mm_struct`指向的链表中搜索`addr`.若搜到且权限正确,则判定为合法
2. 若合法,则建立虚实映射关系
   1. 分配一个空闲的内存页,修改页表,完成映射,刷新 TLB


可用的工具函数:

```C
// 给定内存控制块,在其中查找 addr 对应的 vma
// (vma->vm_start <= addr <= vma_vm_end)
struct vma_struct *
find_vma(struct mm_struct *mm, uintptr_t addr);
```

**考察错误码**

我们关注的的逻辑集中在后两位。枚举后两位，可以得到的状态如下.对应附上了处理方式.

- **00**
  - fault 原因: 对一个 "不存在"的地址进行了 读 操作
- **01**：
  - fault 原因: 对一个 "不存在"的地址进行了 写 操作
- **10**：
  - fault 原因: 对一个存在的地址进行了 读 操作,但触发了页保护(权限)错误--实打实的权限问题,这是不应该出现的
- **11**
  - fault 原因: 对一个存在的地址进行了 写 操作,但触发了页保护(权限)错误

源代码中对四种状态与 vm 中的 flag 位进行了校验,排除了不应该存在的情况,这里省略不表,通过校验的条件如下:

```
IF  (write an existed addr ) OR
    (write an non_existed addr && addr is writable) OR
    (read  an non_existed addr && addr is readable)
THEN
    continue process
```


**练习1 代码**

终于来到了写 lab3 代码的时候.**如何建立 addr 的虚实映射关系?**

1. 获取这个 addr 对应的 page table entry,如果没有就新分配一个物理页作为 page table.
2. 获取后判断这个 pte 是否为空,若为空,则需要建立映射 用到`pgdir_alloc_page`函数
   

**考察pgdir_alloc_page**



```
//给定线性地址和 page directory,为这个地址进行映射!
// 所谓映射,就是要建立好页表和页表值.
struct Page *
pgdir_alloc_page(pde_t *pgdir, uintptr_t la, uint32_t perm)
```

注意其调用的`page_insert`含义是把新申请的物理 page与线性地址 关联起来,"insert"一个新的受管理的物理页.



最终的代码:

```
// 定位 pte 的地址
if ((ptep = get_pte(mm->pgdir, addr, 1)) == NULL) {
    cprintf("get_pte in do_pgfault failed\n");
    goto failed;
}
// 考察 pte,若未映射过则重新映射.
if (*ptep == 0) { // if the phy addr isn't exist, then alloc a page & map the phy addr with logical addr
    if (pgdir_alloc_page(mm->pgdir, addr, perm) == NULL) {
        cprintf("pgdir_alloc_page in do_pgfault failed\n");
        goto failed;
    }
}
else...
```

此函数尚未完成,下半部分需要先了解页面替换算法才能完成.


## 练习 2 完成 FIFO 页面替换算法

### 2.1 什么是置换?

当出现缺页异常,需要调入新页面,但是物理内存已满时,需要把某个物理页面与磁盘中的数据进行置换.

### 2.2 哪些页可以被换出?

- 被内核直接使用的内核空间的页面不可换出
- 映射至用户空间且被用户程序直接访问的页面可以被交换

### 2.3 页换入换出机制是怎样的?

要实现页换入换出,至少要有以下接口:

1. `swap` 层
2. `swapfs` 层,专用于 swap 的模拟文件系统,提供了以 `page` 为单位读写的接口,代码位于`swapfs.[hc]`
3. 磁盘读写接口,以字节为单位读写,代码位于`ide.[hc]`


### 2.4 磁盘给出了怎样的接口?怎样进行磁盘的读写?

磁盘读写接口位于`ide.[hc]`文件中,具体可参考我的 lab8 实验笔记.

### 2.5 `swapfs`层提供了怎样的接口?

提供了以`swap_entry_t`(的高 8 位)为磁盘扇区号,`page`为内存读写基址的读写接口.

```C
// 把 entry<<8 号的扇区开始的一页加载到内存 page 处
swapfs_read(swap_entry_t entry, struct Page *page) {
    return ide_read_secs(SWAP_DEV_NO, swap_offset(entry) * PAGE_NSECT, page2kva(page), PAGE_NSECT);
}
```

``` C
// 把 内存 page 处开始的一页写入到磁盘的 entry<<8 号扇区
int
swapfs_write(swap_entry_t entry, struct Page *page) {
    return ide_write_secs(SWAP_DEV_NO, swap_offset(entry) * PAGE_NSECT, page2kva(page), PAGE_NSECT);
}
```

`PAGE_NSECT`:  = (PGSIZE / SECTSIZE), 每个`page`占用的扇区数,实际值为 8.

关于`swap_offset`:

```C
// swap_entry_t 右移 8 位作为 page 的数量
#define swap_offset(entry) ({                                       \
               size_t __offset = (entry >> 8);                        \
               if (!(__offset > 0 && __offset < max_swap_offset)) {    \
                    panic("invalid swap_entry_t = %08x.\n", entry);    \
               }                                                    \
               __offset;                                            \
          })
```

`swap_entry_t`的含义:

```
----------------------------
offset  |   reserved    | 0 |
----------------------------
24bits      7bits         1bits
```

`offset`给出了扇区号.

### 2.5 swap层提供了哪些接口?

考察`swap.c`:
```

int swap_out(struct mm_struct *mm, int n, int in_tick);
```
```
// 把虚拟地址 addr 对应的磁盘上的扇区读取到虚拟地址处.辅助参数:mm,用于确定 page directory;出参: ptr_result,
int swap_in(struct mm_struct *mm, uintptr_t addr, struct Page **ptr_result);

```

### 2.6 何时触发换入?何时触发换出?

需要区分合法性与存在性的概念.

`check_mm_struct`维护了所有的合法的虚拟内存空间集合;页表描述了此页是否在内存中出现.

如果访问地址合法,但不存在于内存,说明在磁盘中.此时触发**换入**.

对于(消极)换出,则是当系统分配内存时发现内存不够用了,没有空闲的物理页可分配,此时需要把一些内存的数据放入到磁盘中,触发**换入**操作.

### 2.7 如何表示物理页的使用情况?

拓展`Page`结构:

```
struct Page {
    int ref;                        // 引用计数
    uint32_t flags;                 // 页状态
    unsigned int property;          // first fit 物理内存管理算法使用,空闲块的数量
    list_entry_t page_link;         // free list link
    list_entry_t pra_page_link;     // used for pra (page replace algorithm)
    uintptr_t pra_vaddr;            // used for pra (page replace algorithm)
};
```
注意新增的`pra_page_link`和`pra_vaddr`.






考察"抽象"页交换管理器`struct swap_manager`:

```C
struct swap_manager
{
    const char *name;
    // 全局状态初始化
    int (*init)            (void);
    /* Initialize the priv data inside mm_struct */
    // 初始化 mm_struct 中的 sm_priv
    int (*init_mm)         (struct mm_struct *mm);
    /* Called when tick interrupt occured */
    int (*tick_event)      (struct mm_struct *mm);
    /* Called when map a swappable page into the mm_struct */
    int (*map_swappable)   (struct mm_struct *mm, uintptr_t addr, struct Page *page, int swap_in);
    /* When a page is marked as shared, this routine is called to
    * delete the addr entry from the swap manager */
    int (*set_unswappable) (struct mm_struct *mm, uintptr_t addr);
    /* Try to swap out a page, return then victim */
    int (*swap_out_victim) (struct mm_struct *mm, struct Page **ptr_page, int in_tick);
    /* check the page relpacement algorithm */
    int (*check_swap)(void);     
};
```


### 2.4 ucore 如何表示一个页被换到的了磁盘上?

ucore 借助一个与 PTE 结构类似的结构`swap_entry_t`来表示虚拟页与磁盘页的映射关系.它的存在位(PTE_P)来表示物理页的状态.

1. 当 PTE_P = 1 时,相应的物理页在内存中
2. 当 PTE_P = 0 时,相应的物理页在外存中

也就是说我们的`PTE`有两种不同的语义.当 PTE_P = 1 时,位含义如下:

![](/images/PTE.png)

当 PTE_P = 0 时,位含义如下:
### 2.5 如何换入(swap_in)?

考察函数:

```
int
swap_in(struct mm_struct *mm, uintptr_t addr, struct Page **ptr_result);
```


### 2.6 page fault 的下半部分

```
else {
        // 末位为 0,说明这是一个用于表示交换页的 entry.
        // 从磁盘加载数据,插入页中,建立映射.
    if(swap_init_ok) {
        struct Page *page=NULL;
        if ((ret = swap_in(mm, addr, &page)) != 0) {
            cprintf("swap_in in do_pgfault failed\n");
            goto failed;
        }    
        page_insert(mm->pgdir, page, addr, perm);
        swap_map_swappable(mm, addr, page, 1);
        page->pra_vaddr = addr;
    }
    else {
        cprintf("no swap_init_ok but ptep is %x, failed\n",*ptep);
        goto failed;
    }
}
```


未完待续...



























## 参考资料

- [uCore操作系统编程实验手记（三)](https://blog.csdn.net/hezxyz/article/details/96686194)
- 实验指导书[链接](https://chyyuu.gitbooks.io/ucore_os_docs/content/lab3/lab3_2_1_exercises.html)
- *Linux内核设计与实现* Chapter 15
- *OSTEP*