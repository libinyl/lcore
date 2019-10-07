# ucore Lab4~Lab5 进程实现与管理

这是个大坑.我感觉整个操作系统的实现都是围绕着进程的,从进程大量的字段就可见一斑.

这里把 Lab4 和 Lab5 综合一下,作为对实验指导书的补充和思考.

## 1 什么是进程?进程的数据结构是怎样的?内核怎样管理进程?

**概述**

实现进程是为了实现 CPU 的虚拟化,让 ucore 分时共享 CPU,实现多条控制流并发执行.

每个进程都有其生命周期,经历从出生到死亡的过程,所谓进程就是这个过程的执行过程.为了记录、描述和管理程序执行的动态变化过程，需要有一个数据结构，这就是进程控制块。进程与进程控制块是一一对应的。

**进程控制块的设计**

对内,进程**拥有资源**,对外,进程**被内核调度**.

所以,一个进程控制块的字段分两方面,一种是对资源的描述,包括地址空间,栈地址,上下文等;一种是调度状态的描述,即当前状态,标识,与其他进程的关系等.

**进程控制块**

```C
struct proc_struct {

    /************ 进程静态属性 ************/
    int pid;                        // 进程 id
    char name[PROC_NAME_LEN + 1];   // 进程名称

    /************ 内存和文件资源 ************/
    uintptr_t kstack;               // 进程内核栈.对于用户进程,是特权级发生改变时保存被打断的用户信息栈
    struct mm_struct *mm;           // 进程的内存管理描述符,只用于用户态进程
    struct context context;         // 进程上下文,主要用于进程间切换时保存状态
    struct trapframe *tf;           // 当前中断的中断帧 Trap frame,主要用于中断时保存状态
    uintptr_t cr3;                  // 页目录(一级页表)基址
    struct files_struct *filesp;    // 文件控制块

    /************ 进程间结构维护 ************/
    struct proc_struct *parent;     // 父进程.树形结构.内核的 idleproc 没有父进程.
    struct proc_struct *cptr, *yptr, *optr;     // children, younger,older 进程
    int exit_code;                  // 退出代码 被送往父进程
    list_entry_t list_link;         // 进程链表
    list_entry_t hash_link;         // 进程哈希表

    /************ 进程调度 ************/
    enum proc_state state;          // 进程状态
    uint32_t wait_state;            // 等待状态
    int runs;                       // 运行次数
    volatile bool need_resched;     // 是否需要重新调度以释放 cpu?意思是运行其他进程而暂停本进程
    struct run_queue *rq;           // 包含当前进程的运行队列
    list_entry_t run_link;          // 运行队列链接
    int time_slice;                 // time slice for occupying the CPU
    skew_heap_entry_t lab6_run_pool;// lab6: the entry in the run pool
    uint32_t lab6_stride;           // lab6 the current stride of the process
    uint32_t lab6_priority;         // lab6: process 权重, 被lab6_set_priority(uint32_t)设置
    uint32_t flags;                 // 标志位
};
```

在 ucore 中,内核进程,用户进程共用此结构.不考虑进程调度,在初始化完毕后,进程管理机制可由下图表示:

![](/images/进程数据结构&#32;1.png)

## 3 进程的内部资源有哪些,是如何维护的?

**内存描述**

进程通过`mm_struct`描述内存结构.

**上下文**

即 `context`,一堆寄存器,用于上下文切换时保存进程信息.

**中断帧**

即`trapframe`, 用于中断时保存用户进程状态;



## 2 ucore 怎样维护进程,用到哪些变量?

更好的说法是内核线程.因为在 ucore 的实现,内核态的几个"进程"共用全局的变量和共享资源.

内核的三个进程直接由三个全局变量表示:

- **idle**: 用于进程调度
- **initproc**: 用于创建用户进程
- **current**: 维护"当前"进程

而用户态进程统一由全局的`proc_list`和`proc.c`私有的`hash_list`维护.

## 3 进程

## 创建一个新的(内核)进程要考虑什么?

内核进程也是进程,创建内核进程调用`kernel_thread`函数,实际是对`do_fork`的封装.那么如何创建一个普通进程?

对于一个进程的资源,有的是可以共享的,有的是无法共享的.

首先考察一定无法共享的资源.什么资源一定无法共享?

- 进程控制块本身
- 内核栈空间
  

考察`do_fork`,可知新的 *用户态* 进程**共享**了"当前"进程的:

- trapframe
- stack pointer

新的进程**创建**了新的:

- 栈空间
- 文件描述块
- 内存描述符(mm等)

而创建内核进程时,文件描述块和内存

对于文件描述符的克隆结果如图所示:

![](/images/fork-文件描述块.png)

## 那么具体如何创建新的内核进程?

考察函数`kernel_thread`:

```C
// 创建内核线程,以函数 fn 为控制流.
//      构造新的内核态的 trapframe,用于 do_fork 中复制.
int
kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags) {
    struct trapframe;
    memset(&tf, 0, sizeof(struct trapframe));
    tf.tf_cs = KERNEL_CS;
    tf.tf_ds = tf.tf_es = tf.tf_ss = KERNEL_DS;
    tf.tf_regs.reg_ebx = (uint32_t)fn;
    tf.tf_regs.reg_edx = (uint32_t)arg;
    tf.tf_eip = (uint32_t)kernel_thread_entry;
    return do_fork(clone_flags | CLONE_VM, 0, &tf);
}
```





## 怎样区分内核进程和用户进程?

栈空间不同.

进程名称 | trapframe | kstack
-----|-----------|-------
"idle" | NULL | bootstack
"init" | tf_cs = KERNEL_CS;<br>tf.tf_ds = tf.tf_es = tf.tf_ss = KERNEL_DS;


内核栈基址: `bootstack`
用户进程栈基址: 新申请的 page.见`setup_kstack()`.

## fork 复制了哪些元素?

## 数据结构

- 内核线程控制块
- 内核线程控制块链表



所谓的上下文其实就是一堆寄存器.

```C
struct context {
    uint32_t eip;
    uint32_t esp;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
};
```

**进程控制块链表**

```C
// 均位于 kern/process/proc.c
static list_entry_t hash_list[HASH_LIST_SIZE];  // 所有进程的 hash_link 
static struct proc *current;                    // 当前占用CPU且处于“运行”状态进程控制块指针。通常这个变量是只读的，只有在进程切换的时候才进行修改，并且整个切换和修改过程需要保证操作的原子性，目前至少需要屏蔽中断。可以参考 switch_to 的实现。
list_entry_t proc_list;                         // 所有进程控制块的双向线性列表，proc_struct中的成员变量list_link将链接入这个链表中。
static struct proc *initproc;                   // 本实验中，指向一个内核线程。本实验以后，此指针将指向第一个用户态进程。
```

**进程状态枚举值**

```C
enum proc_state {
    PROC_UNINIT = 0,  // 未初始化
    PROC_SLEEPING,    // 睡眠
    PROC_RUNNABLE,    // 可运行或正在运行
    PROC_ZOMBIE,      // 几乎死亡,等待父进程回收资源
};
```

## 核心函数

kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags)
{
    struct trapframe tf;
    memset(&tf, 0, sizeof(struct trapframe));
    tf.tf_cs = KERNEL_CS;
    tf.tf_ds = tf_struct.tf_es = tf_struct.tf_ss = KERNEL_DS;
    tf.tf_regs.reg_ebx = (uint32_t)fn;
    tf.tf_regs.reg_edx = (uint32_t)arg;
    tf.tf_eip = (uint32_t)kernel_thread_entry;
    return do_fork(clone_flags | CLONE_VM, 0, &tf);
}

### 机制: 用内存来保存当前寄存器的值

### 核心函数: do_fork

原型:

```
int
do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
```

此函数的主要处理步骤:

1. 调用`alloc_proc`,申请一个新的进程控制块
2. 调用`setup_kstack`,申请一个内存栈 用于子进程.
3. 调用`copy_mm`来复制或共享 mm.(依据`clone_flag`)
4. 调用`copy_thread`来建立进程控制块的`tf`和`context`.
5. 向`hash_list`和`proc_list`中插入新申请的内存控制块
6. 调用`wakeup_proc`将新的子进程状态切换至`RUNNABLE`.
7. 返回值是子进程的 `pid`.

其中,第 4 步 上下文信息是如何处理的?

其中的核心步骤:`copy_thread`:

```
static void
copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf);
```

接受一个进程控制块,和父进程的栈指针值,和 父进程的 `trapframe`

执行一个新的内核线程,比简单的函数调用多了什么?

1. 独立的栈空间
2. 独立的上下文信息
3. 独立的中断帧
4. 独立的运行状态(state)
5. 被全局调度器和管理器统一管理

进程的"已创建"状态,也就是就绪之前的状态,意思是操作系统已经分配了资源,但还没有开始运行."死亡"状态则已经回收了资源.

资源:1)虚拟地址空间 2)其他资源

## 机制: 执行

执行一个可执行文件,

## 机制: 中断与调度

- 每次的中断处理都有可能将当前进程设置为期待被重新调度.
- 所以每次中断处理结束后,返回用户态之前,都会判断当前进程是否期待被调度.如果是,则进行调度.

## 进程间切换保存的context 与中断时保存的 trapframe 有什么不同?

前者是用户态进程-用户态进程的切换,只需保存少量寄存器;后者是用户态-内核态进程的切换,不仅要考虑寄存器,还要考虑内核栈地址,

## fork 的时候是如何实现按需分配内存的?