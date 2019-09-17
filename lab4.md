# ucore Lab4 实验笔记

**目的** 实现CPU 的虚拟化,让ucore 分时共享 CPU,实现多条控制流并发执行.

## 数据结构

- 内核线程控制块
- 内核线程控制块链表

**进程控制块**

```C
struct proc_struct {
    enum proc_state state;          // 进程状态
    int pid;                        // 进程 id
    int runs;                       // 运行次数
    uintptr_t kstack;               // 进程内核栈.对于用户进程,是特权级发生改变时保存被打断的用户信息栈
    volatile bool need_resched;     // 是否需要重新调度以释放 cpu?意思是运行其他进程而暂停本进程
    struct proc_struct *parent;     // 父进程.树形结构.内核的 idleproc 没有父进程.
    struct mm_struct *mm;           // 进程的内存管理描述符,只用于用户态进程
    struct context context;         // 进程上下文,用于Switch here to run process
    struct trapframe *tf;           // 当前中断的中断帧 Trap frame
    uintptr_t cr3;                  // 页目录(一级页表)基址
    uint32_t flags;                 // 标志位
    char name[PROC_NAME_LEN + 1];   // 进程名称
    list_entry_t list_link;         // 进程链表
    list_entry_t hash_link;         // 进程哈希表
};
```

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

