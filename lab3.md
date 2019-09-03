# ucore Lab3

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

###1.5 内存描述符的操作函数

-vma_struct
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

参考手册 6.13

![](https://github.com/libinyl/CS-notes/blob/master/images/intel/v3/Figure%206-6.%20Error%20Code.png?raw=true)


**EXT** External event (bit 0),值为 1 时表示硬件外部中断.

**IDT** Descriptor location (bit 1), 置为 1 时表示errorcode 的索引部分引用的是 IDT 的一个门描述符,置为 0 时表示引用 GDT 或者当前 LDT 的描述符.

**TI** 只在 IDT 位为 0 时有用.此时 TI 表示errorcode 的索引部分是 LDT,为 1 是是 GDT.

那么索引部分呢?

参考手册 6.15 节:

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
2. 
3. 在 `mm_struct`指向的链表中搜索`addr`


未完待续...


![](2019-09-03-20-43-47.png)



























## 参考资料

- 实验指导书[链接](https://chyyuu.gitbooks.io/ucore_os_docs/content/lab3/lab3_2_1_exercises.html)
- *Linux内核设计与实现* Chapter 15