#include <proc.h>
#include <kmalloc.h>
#include <string.h>
#include <sync.h>
#include <pmm.h>
#include <error.h>
#include <sched.h>
#include <elf.h>
#include <vmm.h>
#include <trap.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <fs.h>
#include <vfs.h>
#include <sysfile.h>
#include <kdebug.h>

/**
 * ----------进程/线性机制的设计原理-----------
 * 
 * ucore 实现了简化版的 Linux 进程/线程机制.
 * 
 * 每个进程包含:
 * - 独立的地址空间
 * - 至少一个线程
 * - 内核数据
 * - 进程状态(用于上下文切换)
 * - 文件
 * 
 * process state       :     meaning               -- reason
 *  PROC_UNINIT     :   uninitialized           -- alloc_proc
 *  PROC_SLEEPING   :   sleeping                -- try_free_pages, do_wait, do_sleep
 *  PROC_RUNNABLE   :   runnable(maybe running) -- proc_init, wakeup_proc, 
 *  PROC_ZOMBIE     :   almost dead             -- do_exit
 * 
 * 
 * 
 */ 

/* ------------- process/thread mechanism design&implementation -------------
(an simplified Linux process/thread mechanism )
introduction:
  ucore implements a simple process/thread mechanism. process contains the independent memory sapce, at least one threads
for execution, the kernel data(for management), processor state (for context switch), files(in lab6), etc. ucore needs to
manage all these details efficiently. In ucore, a thread is just a special kind of process(share process's memory).
------------------------------
process state       :     meaning               -- reason
    PROC_UNINIT     :   uninitialized           -- alloc_proc
    PROC_SLEEPING   :   sleeping                -- try_free_pages, do_wait, do_sleep
    PROC_RUNNABLE   :   runnable(maybe running) -- proc_init, wakeup_proc, 
    PROC_ZOMBIE     :   almost dead             -- do_exit

-----------------------------
process state changing:
                                            
  alloc_proc                                 RUNNING
      |                                   +--<----<--+
      |                                   | proc_run |
      V                                   +-->---->--+ 
PROC_UNINIT -- proc_init/wakeup_proc --> PROC_RUNNABLE -- try_free_pages/do_wait/do_sleep --> PROC_SLEEPING --
                                           A      |                                                           |
                                           |      +--- do_exit --> PROC_ZOMBIE                                |
                                           |                                                                  | 
                                           -----------------------wakeup_proc----------------------------------
-----------------------------
process relations
parent:           proc->parent  (proc is children)
children:         proc->cptr    (proc is parent)
older sibling:    proc->optr    (proc is younger sibling)
younger sibling:  proc->yptr    (proc is older sibling)
-----------------------------
related syscall for process:
SYS_exit        : process exit,                           -->do_exit
SYS_fork        : create child process, dup mm            -->do_fork-->wakeup_proc
SYS_wait        : wait process                            -->do_wait
SYS_exec        : after fork, process execute a program   -->load a program and refresh the mm
SYS_clone       : create child thread                     -->do_fork-->wakeup_proc
SYS_yield       : process flag itself need resecheduling, -- proc->need_sched=1, then scheduler will rescheule this process
SYS_sleep       : process sleep                           -->do_sleep 
SYS_kill        : kill process                            -->do_kill-->proc->flags |= PF_EXITING
                                                                 -->wakeup_proc-->do_wait-->do_exit   
SYS_getpid      : get the process's pid

*/

// the process set's list
list_entry_t proc_list;

#define HASH_SHIFT          10
#define HASH_LIST_SIZE      (1 << HASH_SHIFT)
#define pid_hashfn(x)       (hash32(x, HASH_SHIFT))

// has list for process set based on pid
static list_entry_t hash_list[HASH_LIST_SIZE];

// idle proc
struct proc_struct *idleproc = NULL;
// init proc
struct proc_struct *initproc = NULL;
// current proc
struct proc_struct *current = NULL;

static int nr_process = 0;

void kernel_thread_entry(void);
void forkrets(struct trapframe *tf);
void switch_to(struct context *from, struct context *to);

// alloc_proc - alloc a proc_struct and init all fields of proc_struct
/**
 * 初始化新的进程
 * 
 */ 
static struct proc_struct *
alloc_proc(void) {
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));
    if (proc != NULL) {
     //LAB5 YOUR CODE : (update LAB4 steps)
    /*
     * below fields(add in LAB5) in proc_struct need to be initialized	
     *       uint32_t wait_state;                        // waiting state
     *       struct proc_struct *cptr, *yptr, *optr;     // relations between processes
	 */
    //LAB8:EXERCISE2 YOUR CODE HINT:need add some code to init fs in proc_struct, ...
        proc->state = PROC_UNINIT;
        proc->pid = -1;
        proc->runs = 0;
        proc->kstack = 0;
        proc->need_resched = 0;
        proc->parent = NULL;
        proc->mm = NULL;
        memset(&(proc->context), 0, sizeof(struct context));
        proc->tf = NULL;
        proc->cr3 = boot_cr3;   // 内核空间对多有内核线程可见
        proc->flags = 0;
        memset(proc->name, 0, PROC_NAME_LEN);
        proc->wait_state = 0;
        proc->cptr = proc->optr = proc->yptr = NULL;
        proc->rq = NULL;
        list_init(&(proc->run_link));
        proc->time_slice = 0;
        proc->lab6_run_pool.left = proc->lab6_run_pool.right = proc->lab6_run_pool.parent = NULL;
        proc->lab6_stride = 0;
        proc->lab6_priority = 0;
        proc->filesp = NULL;
    }
    return proc;
}

// set_proc_name - set the name of proc
char *
set_proc_name(struct proc_struct *proc, const char *name) {
    memset(proc->name, 0, sizeof(proc->name));
    return memcpy(proc->name, name, PROC_NAME_LEN);
}

// get_proc_name - get the name of proc
char *
get_proc_name(struct proc_struct *proc) {
    static char name[PROC_NAME_LEN + 1];
    memset(name, 0, sizeof(name));
    return memcpy(name, proc->name, PROC_NAME_LEN);
}

// set_links - set the relation links of process
static void
set_links(struct proc_struct *proc) {
    list_add(&proc_list, &(proc->list_link));
    proc->yptr = NULL;
    if ((proc->optr = proc->parent->cptr) != NULL) {
        proc->optr->yptr = proc;
    }
    proc->parent->cptr = proc;
    nr_process ++;
}

// remove_links - clean the relation links of process
static void
remove_links(struct proc_struct *proc) {
    list_del(&(proc->list_link));
    if (proc->optr != NULL) {
        proc->optr->yptr = proc->yptr;
    }
    if (proc->yptr != NULL) {
        proc->yptr->optr = proc->optr;
    }
    else {
       proc->parent->cptr = proc->optr;
    }
    nr_process --;
}

/**
 * 为进程分配一个全剧唯一(?)的 pid
 */ 
static int
get_pid(void) {
    static_assert(MAX_PID > MAX_PROCESS);
    struct proc_struct *proc;
    list_entry_t *list = &proc_list, *le;
    static int next_safe = MAX_PID, last_pid = MAX_PID;
    if (++ last_pid >= MAX_PID) {
        last_pid = 1;
        goto inside;
    }
    if (last_pid >= next_safe) {
    inside:
        next_safe = MAX_PID;
    repeat:
        le = list;
        while ((le = list_next(le)) != list) {
            proc = le2proc(le, list_link);
            if (proc->pid == last_pid) {
                if (++ last_pid >= next_safe) {
                    if (last_pid >= MAX_PID) {
                        last_pid = 1;
                    }
                    next_safe = MAX_PID;
                    goto repeat;
                }
            }
            else if (proc->pid > last_pid && next_safe > proc->pid) {
                next_safe = proc->pid;
            }
        }
    }
    return last_pid;
}

/**
 * 运行指定的进程 proc
 * 
 * 方式: 切换上下文.只用于schedule
 * 
 * 1. 更新全局 current 变量
 * 2. 设置内核栈(tss)
 * 3. 配置新进程的栈指针,和cr3
 * 4. 调用 switch_to 切换进程上下文
 * 
 * Q: switch_to 之后, switch 到了哪里? 
 *    到了参数中 proc->context 指定的地址.这个地址是哪里? 就是这个 proc 上次来到这里的 switch_to, 本身作为 from 一方保存的context! 所以看起来会继续执行当前代码!
 */ 
void
proc_run(struct proc_struct *proc) {
    LOG("proc_run begin:\n");
    LOG_TAB("pid: %d\n", proc->pid);
    if (proc != current) {
        bool intr_flag;
        struct proc_struct *prev = current, *next = proc;
        local_intr_save(intr_flag);
        {
            LOG("pid=%d, 切换前\n", current->pid);
            current = proc;     //1 更新 current
            LOG_TAB("已更新 current\n");
            load_esp0(next->kstack + KSTACKSIZE);           // 2 当前进程的内核栈顶
            LOG_TAB("已更新 kstack\n");
            lcr3(next->cr3);    //3 用于用户进程页表切换
            // 把当前环境保存在 from,把 to 的状态加载到环境上
            switch_to(&(prev->context), &(next->context));  //4 执行完之后,当前进程已经是下一个进程了. 注意,context 的初始值是什么?
            LOG("pid=%d, 切换后\n", proc->pid);
        }
        local_intr_restore(intr_flag);
    }
    LOG("proc_run end.\n");
}

// forkret -- the first kernel entry point of a new thread/process
// NOTE: the addr of forkret is setted in copy_thread function
//       after switch_to, the current proc will execute here.
/**
 * 
 * 此函数作为新进程的内核入口,在copy_thread函数中被赋给新进程的 context.
 * 当调用switch_to后,将会在此执行.
 */ 
static void
forkret(void) {
    forkrets(current->tf);
}

// hash_proc - add proc into proc hash_list
static void
hash_proc(struct proc_struct *proc) {
    list_add(hash_list + pid_hashfn(proc->pid), &(proc->hash_link));
}

// unhash_proc - delete proc from proc hash_list
static void
unhash_proc(struct proc_struct *proc) {
    list_del(&(proc->hash_link));
}

// find_proc - find proc frome proc hash_list according to pid
// 根据 pid 找进程-哈希表
struct proc_struct *
find_proc(int pid) {
    if (0 < pid && pid < MAX_PID) {
        list_entry_t *list = hash_list + pid_hashfn(pid), *le = list;
        while ((le = list_next(le)) != list) {
            struct proc_struct *proc = le2proc(le, hash_link);
            if (proc->pid == pid) {
                return proc;
            }
        }
    }
    return NULL;
}

// 创建内核线程,以函数 fn 为控制流.
//      构造新的内核态的 trapframe,用于 do_fork 中复制.
/**
 * 内核栈结构是个重要但貌似被忽视的话题.
 * kernel_thread 专门用于初始化内核线程,它们共用一个代码段,数据段,mm等,但入口不同;每个线程有自己的栈空间.
 * 想要执行内核线程, 至少需要指定 1)函数地址 2)函数参数
 * 
 */ 
int
kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags) {
    LOG("\nkernel_thread begin:\n");
    struct trapframe tf;
    memset(&tf, 0, sizeof(struct trapframe));
    // 初始化内核中断帧
    tf.tf_cs = KERNEL_CS;                           // 内核代码段
    tf.tf_ds = tf.tf_es = tf.tf_ss = KERNEL_DS;     // 内核数据段
    tf.tf_regs.reg_ebx = (uint32_t)fn;
    tf.tf_regs.reg_edx = (uint32_t)arg;
    tf.tf_eip = (uint32_t)kernel_thread_entry;      // 内核线程入口点

    LOG_TAB("为 do_fork 准备 trapframe:\n");
    LOG_TAB("内核线程共享代码段选择子: 0x%08lx\n",KERNEL_CS);
    LOG_TAB("内核线程共享数据段选择子: 0x%08lx\n",KERNEL_DS);
    LOG_TAB("指定起始函数: 0x%08lx\n",(uintptr_t *)fn);
    LOG_TAB("指定 eip 入口: 0x%08lx\n",kernel_thread_entry);
    LOG("trapframe准备完毕,即将进入 do_fork. 指定选项: CLONE_VM, 即内核线程共享 mm_struct.\n");

    return do_fork(clone_flags | CLONE_VM, 0, &tf);
}

/**
 * 申请内核栈 = KSTACKPAGE 2 page
 * (供 TSS 段使用)
 */ 
static int
setup_kstack(struct proc_struct *proc) {
    struct Page *page = alloc_pages(KSTACKPAGE);
    if (page != NULL) {
        LOG_TAB("setup_kstack: kstack = new page(2)\n");
        proc->kstack = (uintptr_t)page2kva(page);
        return 0;
    }
    return -E_NO_MEM;
}

// put_kstack - free the memory space of process kernel stack
// 释放内核栈
static void
put_kstack(struct proc_struct *proc) {
    free_pages(kva2page((void *)(proc->kstack)), KSTACKPAGE);
}

// setup_pgdir - alloc one page as PDT
static int
setup_pgdir(struct mm_struct *mm) {
    struct Page *page;
    if ((page = alloc_page()) == NULL) {
        return -E_NO_MEM;
    }
    pde_t *pgdir = page2kva(page);
    memcpy(pgdir, boot_pgdir, PGSIZE);  // 直接复制内核一级页表
    pgdir[PDX(VPT)] = PADDR(pgdir) | PTE_P | PTE_W; // 自映射
    mm->pgdir = pgdir;
    return 0;
}

// put_pgdir - free the memory space of PDT
static void
put_pgdir(struct mm_struct *mm) {
    free_page(kva2page(mm->pgdir));
}

// copy_mm - process "proc" duplicate OR share process "current"'s mm according clone_flags
//         - if clone_flags & CLONE_VM, then "share" ; else "duplicate"
static int
copy_mm(uint32_t clone_flags, struct proc_struct *proc) {
    LOG("copy_mm start:\n");
    LOG_TAB("是否共享父进程 mm? ");
    struct mm_struct *mm, *oldmm = current->mm;

    /* current is a kernel thread */
    if (oldmm == NULL) {
        LOG("否, 进程是内核线程,共享一个 mm.\n");
        LOG("copy_mm end\n");
        return 0;
    }
    if (clone_flags & CLONE_VM) {
        LOG("是\n");
        mm = oldmm;
        goto good_mm;
    }
    LOG("否,创建新 mm\n");

    int ret = -E_NO_MEM;
    if ((mm = mm_create()) == NULL) {
        goto bad_mm;
    }
    if (setup_pgdir(mm) != 0) {
        goto bad_pgdir_cleanup_mm;
    }

    lock_mm(oldmm);
    {
        ret = dup_mmap(mm, oldmm);
    }
    unlock_mm(oldmm);

    if (ret != 0) {
        goto bad_dup_cleanup_mmap;
    }

good_mm:
    mm_count_inc(mm);
    proc->mm = mm;
    proc->cr3 = PADDR(mm->pgdir);
    LOG("copy_mm end\n");
    return 0;
bad_dup_cleanup_mmap:
    exit_mmap(mm);
    put_pgdir(mm);
bad_pgdir_cleanup_mm:
    mm_destroy(mm);
bad_mm:
    LOG("copy_mm end, bad_mm.\n");
    return ret;
}

// 1. 在进程内核栈顶构建 trapframe
// 2. 设置内核 entry point 和进程栈
/**
 * 本来 trapframe 和 context 都是作为"执行现场"在中断/进程切换时保存到内核栈的结构,待轮转回来之后再用于恢复状态,
 * 
 * 但对于新进程而言,直接利用了"恢复"这一步骤,相当于进行了初始化.
 * 
 */ 
static void
copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf) {
    proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE) - 1; // 先转化类型,再-1,正好空出一个 trapframe 的空间
    *(proc->tf) = *tf;
    proc->tf->tf_regs.reg_eax = 0;  // 子进程的返回值
    proc->tf->tf_esp = esp;         
    proc->tf->tf_eflags |= FL_IF;   // 使能中断.意思是此内核进程在执行时可以相应中断

    //context 的其他字段都是 0
    proc->context.eip = (uintptr_t)forkret;     // eip ->forkret->用 tf 恢复的现场
    proc->context.esp = (uintptr_t)(proc->tf);  // 紧邻 tf 之下.内核栈的基址就是 kstack,而不是 bp. process 的context初始化时每个值都是 0.
}

//copy_fs&put_fs function used by do_fork in LAB8
static int
copy_fs(uint32_t clone_flags, struct proc_struct *proc) {
    LOG("copy_fs begin:\n");
    LOG_TAB("是否共享父进程的文件控制块? ");

    struct files_struct *filesp, *old_filesp = current->filesp;
    assert(old_filesp != NULL);

    if (clone_flags & CLONE_FS) {
        filesp = old_filesp;
        LOG("是,指向父进程的 fs.\n");
        goto good_files_struct;
    }
    LOG("否,分配新 fs.\n");
    int ret = -E_NO_MEM;
    if ((filesp = files_create()) == NULL) {
        goto bad_files_struct;
    }

    if ((ret = dup_files(filesp, old_filesp)) != 0) {
        goto bad_dup_cleanup_fs;
    }

good_files_struct:
    files_count_inc(filesp);
    proc->filesp = filesp;
    LOG_TAB("此控制块引用计数值已更新: %d\n",filesp->files_count);
    LOG("copy_fs end\n");
    return 0;

bad_dup_cleanup_fs:
    files_destroy(filesp);
bad_files_struct:
    LOG("copy_fs end, bad_files_struct\n");
    return ret;
}

static void
put_fs(struct proc_struct *proc) {
    struct files_struct *filesp = proc->filesp;
    if (filesp != NULL) {
        if (files_count_dec(filesp) == 0) {
            files_destroy(filesp);
        }
    }
}

/* do_fork -     parent process for a new child process
 * @clone_flags: used to guide how to clone the child process
 * @stack:       the parent's user stack pointer. if stack==0, It means to fork a kernel thread.
 * @tf:          the trapframe info, which will be copied to child process's proc->tf
 */
/**
 * 
 * fork 的结果,从父进程的视角看,是初始化新的进程结构并*加入到运行队列*里.(wakeup_proc),并初始化时间片
 * 
 * stack: 父进程的stack pointer.如果值为 0,意味着这是在 fork 一个内核进程.
 * fork 最终改变的队列: rq 就绪队列.
 * 进程的主要结构要么 share,要么 duplicate.
 * 
 * 步骤:
 *      - 构造新的 PCB
 *      - 分配并设置内核栈(tss)
 *      - 复制内存描述符(mm)
 *      - 设置上下文,用户栈地址,进程入口(即cotext的 ip 和 sp,待调度时切换过去)
 *      - 中断帧(复制来自 kernel_thread 的初始值并调整)
 */ 
int
do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf) {
    LOG("\ndo_fork begin:\n");
    int ret = -E_NO_FREE_PROC;
    struct proc_struct *proc;
    if (nr_process >= MAX_PROCESS) {
        goto fork_out;
    }
    ret = -E_NO_MEM;
    /*
     *   alloc_proc:   create a proc struct and init fields (lab4:exercise1)
     *   setup_kstack: alloc pages with size KSTACKPAGE as process kernel stack
     *   copy_mm:      process "proc" duplicate OR share process "current"'s mm according clone_flags
     *                 if clone_flags & CLONE_VM, then "share" ; else "duplicate"
     *   copy_thread:  setup the trapframe on the  process's kernel stack top and
     *                 setup the kernel entry point and stack of process
     *   hash_proc:    add proc into proc hash_list
     *   get_pid:      alloc a unique pid for process
     *   wakeup_proc:  set proc->state = PROC_RUNNABLE
     * VARIABLES:
     *   proc_list:    the process set's list
     *   nr_process:   the number of process set
     */

	//LAB5 YOUR CODE : (update LAB4 steps)
   /* Some Functions
    *    set_links:  set the relation links of process.  ALSO SEE: remove_links:  lean the relation links of process 
    *    -------------------
	*    update step 1: set child proc's parent to current process, make sure current process's wait_state is 0
	*    update step 5: insert proc_struct into hash_list && proc_list, set the relation links of process
    */
   LOG_TAB("1. 分配 PCB\n");
    if ((proc = alloc_proc()) == NULL) {
        goto fork_out;
    }
    // 设置新进程的父进程为当前进程
    LOG_TAB("2. 指定父进程: current\n");
    proc->parent = current;
    assert(current->wait_state == 0);
    // 建立内核栈空间,并用proc->kstack维护,(指向栈底,低地址)
    LOG_TAB("3. 设置内核栈空间: 2 page\n");
    if (setup_kstack(proc) != 0) {
        goto bad_fork_cleanup_proc;
    }
    // 设置文件系统数据结构
    LOG_TAB("4. 设置 file_struct.\n");
    if (copy_fs(clone_flags, proc) != 0) { //for LAB8
        goto bad_fork_cleanup_kstack;
    }
    // 设置父进程的内存结构
    LOG_TAB("4. 设置 mm_struct\n");
    if (copy_mm(clone_flags, proc) != 0) {
        goto bad_fork_cleanup_fs;
    }
    // 设置父进程的 trapframe 和 context
    LOG_TAB("5. 设置 trapframe 和 context\n");
    copy_thread(proc, stack, tf);

    // 添加到全局进程维护表中
    LOG_TAB("6. 将新进程维护到: proc_list.\n");

    bool intr_flag;
    local_intr_save(intr_flag);
    {
        proc->pid = get_pid();
        hash_proc(proc);
        set_links(proc);

    }
    local_intr_restore(intr_flag);

    // 把子进程更新进程状态为 RUNNABLE 并添加到就绪队列
    LOG_TAB("7. 唤醒新进程,进程创建结束.\n");
    wakeup_proc(proc);

    // 子进程不会执行至此
    ret = proc->pid;    // 对父进程返回子进程的 pid
fork_out:
    LOG("\ndo_fork end\n");
    return ret;

bad_fork_cleanup_fs:  //for LAB8
    put_fs(proc);
bad_fork_cleanup_kstack:
    put_kstack(proc);
bad_fork_cleanup_proc:
    kfree(proc);
    LOG("\ndo_fork bad end\n");
    goto fork_out;
}

// do_exit - called by sys_exit
//   1. call exit_mmap & put_pgdir & mm_destroy to free the almost all memory space of process
//   2. set process' state as PROC_ZOMBIE, then call wakeup_proc(parent) to ask parent reclaim itself.
//   3. call scheduler to switch to other process
/**
 * 进程退出
 * 
 * 1. 释放进程的(几乎)所有资源
 * 2. 设置状态为僵尸,然后唤醒父进程(由父进程来清理子进程的资源.子进程本身不可能在存活状态下清理自己的资源)
 * 3. 调用调度器来切换到其他进程
 */ 

/* 
 * 参考 <unix 网络编程 卷 1> 5.9 节:
 * 
 * 如果一个(父)进程终止，而该进程有子进程处于僵死状态，
 * 那么这些子进程的父进程ID将被重置为1（init进程）。
 * 的init进程将清理它们（也就是说init进程将wait它们，从而去除它们的僵死状态)
 * 
 * 如果过于依赖此机制, 相当于把清理僵尸进程的责任推给了内核.这占用了内核的资源
 * 更好的做法是 fork 之后就 wait 或 waitpid,以清理子进程.
 * 
 * 网络编程时通常要异步处理,子进程exit 之后会向父进程发送信号,父进程捕获此信号并执行 wait,清理此进程.
 * 为何要依赖信号异步处理?因为父进程正阻塞在 accept 函数,而不是阻塞在 wait.
 * 
 * 然而 ucore 没有信号机制.
 * 如果父进程实现了 wait,那么内核会把清理工作分配给父进程实现.
 * 如果没有,则调整子进程的父进程为 init,再有 init 清理子进程.   
 */
int
do_exit(int error_code) {
    if (current == idleproc) {
        panic("idleproc exit.\n");
    }
    if (current == initproc) {
        panic("initproc exit.\n");
    }
    // 回收用户进程内存资源
    struct mm_struct *mm = current->mm;
    if (mm != NULL) {
        lcr3(boot_cr3);                 // 1 切换到内核页表
        if (mm_count_dec(mm) == 0) {    // 2 mm_count-1=0 说明此 mm 没有被其他进程共享,可以被释放
            exit_mmap(mm);
            put_pgdir(mm);
            mm_destroy(mm);
        }
        current->mm = NULL;             // 表示当前进程的内存已释放完毕
    }
    put_fs(current); //for LAB8
    current->state = PROC_ZOMBIE;       // 一旦进程设置为 PROC_ZOMBIE 就无力回天了,无法在此被调度,只能等死
    current->exit_code = error_code;
    // 
    bool intr_flag;
    struct proc_struct *proc;
    local_intr_save(intr_flag);
    {
        // 唤醒父进程,来调用 wait,杀死自己
        proc = current->parent;
        if (proc->wait_state == WT_CHILD) {
            wakeup_proc(proc);
        }
        // 

        while (current->cptr != NULL) {
            proc = current->cptr;
            current->cptr = proc->optr;
    
            proc->yptr = NULL;
            if ((proc->optr = initproc->cptr) != NULL) {
                initproc->cptr->yptr = proc;
            }
            proc->parent = initproc;
            initproc->cptr = proc;
            if (proc->state == PROC_ZOMBIE) {
                if (initproc->wait_state == WT_CHILD) {
                    wakeup_proc(initproc);
                }
            }
        }
    }
    local_intr_restore(intr_flag);
    
    schedule();
    panic("do_exit will not return!! %d.\n", current->pid);
}

//load_icode_read is used by load_icode in LAB8
/**
 * 
 * 调用文件系统,从磁盘加载数据
 * 
 * 从文件 fd 的 offset 处读取 len 个字节到 buf 中
 */ 
static int
load_icode_read(int fd, void *buf, size_t len, off_t offset) {
    // 1. 定位到 offset
    int ret;
    if ((ret = sysfile_seek(fd, offset, LSEEK_SET)) != 0) {
        return ret;
    }
    // 2. 读取到 buf
    if ((ret = sysfile_read(fd, buf, len)) != len) {
        return (ret < 0) ? ret : -1;
    }
    return 0;
}

// load_icode -  called by sys_exec-->do_execve

static int
load_icode(int fd, int argc, char **kargv) {
    LOG("load_icode begin:\n");
    /* LAB8:EXERCISE2 YOUR CODE  HINT:how to load the file with handler fd  in to process's memory? how to setup argc/argv?
     * MACROs or Functions:
     *  mm_create        - create a mm
     *  setup_pgdir      - setup pgdir in mm
     *  load_icode_read  - read raw data content of program file
     *  mm_map           - build new vma
     *  pgdir_alloc_page - allocate new memory for  TEXT/DATA/BSS/stack parts
     *  lcr3             - update Page Directory Addr Register -- CR3
     */
	/* (1) create a new mm for current process
     * (2) create a new PDT, and mm->pgdir= kernel virtual addr of PDT
     * (3) copy TEXT/DATA/BSS parts in binary to memory space of process
     *    (3.1) read raw data content in file and resolve elfhdr
     *    (3.2) read raw data content in file and resolve proghdr based on info in elfhdr
     *    (3.3) call mm_map to build vma related to TEXT/DATA
     *    (3.4) callpgdir_alloc_page to allocate page for TEXT/DATA, read contents in file
     *          and copy them into the new allocated pages
     *    (3.5) callpgdir_alloc_page to allocate pages for BSS, memset zero in these pages
     * (4) call mm_map to setup user stack, and put parameters into user stack
     * (5) setup current process's mm, cr3, reset pgidr (using lcr3 MARCO)
     * (6) setup uargc and uargv in user stacks
     * (7) setup trapframe for user environment
     * (8) if up steps failed, you should cleanup the env.
     */
    assert(argc >= 0 && argc <= EXEC_MAX_ARG_NUM);

    if (current->mm != NULL) {
        panic("load_icode: current->mm must be empty.\n");
    }

    int ret = -E_NO_MEM;
    struct mm_struct *mm;

    /** 创建并初始化 mm **/
    if ((mm = mm_create()) == NULL) {
        goto bad_mm;
    }
    LOG_TAB("初始化: mm\n");
    /**  **/
    if (setup_pgdir(mm) != 0) {
        goto bad_pgdir_cleanup_mm;
    }
    LOG_TAB("\t初始化: 页表, 即 mm->pgdir\n");

    struct Page *page;

    struct elfhdr __elf, *elf = &__elf;
    // (从磁盘)加载 elf 文件头
    if ((ret = load_icode_read(fd, elf, sizeof(struct elfhdr), 0)) != 0) {
        goto bad_elf_cleanup_pgdir;
    }
    LOG_TAB("已加载: elf header\n");

    if (elf->e_magic != ELF_MAGIC) {
        ret = -E_INVAL_ELF;
        goto bad_elf_cleanup_pgdir;
    }
    LOG_TAB("已验证: elf header 有效\n");

    // (从磁盘)加载所有 elf program header
    struct proghdr __ph, *ph = &__ph;
    uint32_t vm_flags, perm, phnum;
    LOG_TAB("开始加载 elf program:\n");
    for (phnum = 0; phnum < elf->e_phnum; phnum ++) {
        LOG_TAB("正在加载第 %d 个 program, 共 %d 个.\n",phnum+1, elf->e_phnum);
        off_t phoff = elf->e_phoff + sizeof(struct proghdr) * phnum;
        if ((ret = load_icode_read(fd, ph, sizeof(struct proghdr), phoff)) != 0) {
            goto bad_cleanup_mmap;
        }
        LOG_TAB("\t已加载: program header\n");
        
        /*** elf program header 校验 begin ***/
        if (ph->p_type != ELF_PT_LOAD) {
            continue ;
        }
        if (ph->p_filesz > ph->p_memsz) {
            ret = -E_INVAL_ELF;
            goto bad_cleanup_mmap;
        }
        if (ph->p_filesz == 0) {
            continue ;
        }
        LOG_TAB("\t已校验: program header\n");
        /*** elf program header 校验 end ***/
        // 根据elf 标志位 确认内存描述符属性
        vm_flags = 0, perm = PTE_U;
        if (ph->p_flags & ELF_PF_X) vm_flags |= VM_EXEC;
        if (ph->p_flags & ELF_PF_W) vm_flags |= VM_WRITE;
        if (ph->p_flags & ELF_PF_R) vm_flags |= VM_READ;
        if (vm_flags & VM_WRITE) perm |= PTE_W;
        // 
        if ((ret = mm_map(mm, ph->p_va, ph->p_memsz, vm_flags, NULL)) != 0) {
            goto bad_cleanup_mmap;
        }
        LOG_TAB("\t已建立 mm: [0x%08lx,0x%08lx)\n", ph->p_va, ph->p_va + ph->p_memsz);
        off_t offset = ph->p_offset;
        size_t off, size;
        uintptr_t start = ph->p_va, end, la = ROUNDDOWN(start, PGSIZE);

        ret = -E_NO_MEM;

        end = ph->p_va + ph->p_filesz;
        while (start < end) {
            if ((page = pgdir_alloc_page(mm->pgdir, la, perm)) == NULL) {
                ret = -E_NO_MEM;
                goto bad_cleanup_mmap;
            }
            off = start - la, size = PGSIZE - off, la += PGSIZE;
            if (end < la) {
                size -= la - end;
            }
            if ((ret = load_icode_read(fd, page2kva(page) + off, size, offset)) != 0) {
                goto bad_cleanup_mmap;
            }
            start += size, offset += size;
        }
        end = ph->p_va + ph->p_memsz;

        if (start < la) {
            /* ph->p_memsz == ph->p_filesz */
            if (start == end) {
                continue ;
            }
            off = start + PGSIZE - la, size = PGSIZE - off;
            if (end < la) {
                size -= la - end;
            }
            memset(page2kva(page) + off, 0, size);
            start += size;
            assert((end < la && start == end) || (end >= la && start == la));
        }
        while (start < end) {
            if ((page = pgdir_alloc_page(mm->pgdir, la, perm)) == NULL) {
                ret = -E_NO_MEM;
                goto bad_cleanup_mmap;
            }
            off = start - la, size = PGSIZE - off, la += PGSIZE;
            if (end < la) {
                size -= la - end;
            }
            memset(page2kva(page) + off, 0, size);
            start += size;
        }
        LOG_TAB("\t已建立: 页表\n");
    }
    sysfile_close(fd);

    vm_flags = VM_READ | VM_WRITE | VM_STACK;
    if ((ret = mm_map(mm, USTACKTOP - USTACKSIZE, USTACKSIZE, vm_flags, NULL)) != 0) {
        goto bad_cleanup_mmap;
    }
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP-PGSIZE , PTE_USER) != NULL);
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP-2*PGSIZE , PTE_USER) != NULL);
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP-3*PGSIZE , PTE_USER) != NULL);
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP-4*PGSIZE , PTE_USER) != NULL);
    
    mm_count_inc(mm);// mm 引用计数
    // 安装 mm
    current->mm = mm;
    current->cr3 = PADDR(mm->pgdir);
    lcr3(PADDR(mm->pgdir));

    //计算 argc, argv
    uint32_t argv_size=0, i;
    for (i = 0; i < argc; i ++) {
        argv_size += strnlen(kargv[i],EXEC_MAX_ARG_LEN + 1)+1;
    }
    // USTACKTOP 为固定值,每个进程的用户栈范围都相同
    uintptr_t stacktop = USTACKTOP - (argv_size/sizeof(long)+1)*sizeof(long); // 用户栈顶,
    char** uargv=(char **)(stacktop  - argc * sizeof(char *));
    
    argv_size = 0;
    for (i = 0; i < argc; i ++) {
        uargv[i] = strcpy((char *)(stacktop + argv_size ), kargv[i]);
        argv_size +=  strnlen(kargv[i],EXEC_MAX_ARG_LEN + 1)+1;
    }
    
    stacktop = (uintptr_t)uargv - sizeof(int);
    *(int *)stacktop = argc;    // 在栈顶处压入参数

    //构造返回用户态的中断帧,用于从内核态返回,进入用户态,执行此执行文件的新的进程,在__alltraps 中应用.都是虚拟地址.
    struct trapframe *tf = current->tf;
    memset(tf, 0, sizeof(struct trapframe));
    tf->tf_cs = USER_CS;                            // 用户代码段
    tf->tf_ds = tf->tf_es = tf->tf_ss = USER_DS;    // 用户数据段
    tf->tf_esp = stacktop;                          // 用户栈
    tf->tf_eip = elf->e_entry;                      // 此 elf 的入口
    tf->tf_eflags = FL_IF;
    ret = 0;
    LOG("load_icode end.\n");
out:
    return ret;
bad_cleanup_mmap:
    exit_mmap(mm);
bad_elf_cleanup_pgdir:
    put_pgdir(mm);
bad_pgdir_cleanup_mm:
    mm_destroy(mm);
bad_mm:
    goto out;
}

// this function isn't very correct in LAB8
static void
put_kargv(int argc, char **kargv) {
    while (argc > 0) {
        kfree(kargv[-- argc]);
    }
}

static int
copy_kargv(struct mm_struct *mm, int argc, char **kargv, const char **argv) {
    int i, ret = -E_INVAL;
    if (!user_mem_check(mm, (uintptr_t)argv, sizeof(const char *) * argc, 0)) {
        return ret;
    }
    for (i = 0; i < argc; i ++) {
        char *buffer;
        if ((buffer = kmalloc(EXEC_MAX_ARG_LEN + 1)) == NULL) {
            goto failed_nomem;
        }
        if (!copy_string(mm, buffer, argv[i], EXEC_MAX_ARG_LEN + 1)) {
            kfree(buffer);
            goto failed_cleanup;
        }
        kargv[i] = buffer;
    }
    return 0;

failed_nomem:
    ret = -E_NO_MEM;
failed_cleanup:
    put_kargv(i, kargv);
    return ret;
}

// do_execve - call exit_mmap(mm)&put_pgdir(mm) to reclaim memory space of current process
//           - call load_icode to setup new memory space accroding binary prog.
/**
 * 核心中的核心:执行可执行文件
 * 
 * 什么是执行? 执行的条件是什么?
 * 对于执行的系统调用(exec),内核的作用是准备用户环境.这与其他的系统调用思维方式可能有所区别.
 * 通常系统调用是一种"服务",不改变用户进程本身.
 * 但是 exec 系统调用的目的就是将可执行文件实例化为一个程序,包括设置它的内存资源,设置进程的各种属性.
 * 一个进程在 fork 之后,发起了 exec 系统调用,意味着它本身的属性会被重新设置.
 * 最后把当前进程初始化完毕,返回给用户一个执行环境.见 load_icode.
 * 
 * name: 程序名称
 * argc: 参数数量
 * argv[0]: path
 * 
 */ 
int
do_execve(const char *name, int argc, const char **argv) {
    LOG("do_execve begin:\n");
    static_assert(EXEC_MAX_ARG_LEN >= FS_MAX_FPATH_LEN);
    struct mm_struct *mm = current->mm;
    if (!(argc >= 1 && argc <= EXEC_MAX_ARG_NUM)) {
        return -E_INVAL;
    }

    char local_name[PROC_NAME_LEN + 1];
    memset(local_name, 0, sizeof(local_name));
    
    char *kargv[EXEC_MAX_ARG_NUM];
    const char *path;
    
    int ret = -E_INVAL;
    
    lock_mm(mm);
    if (name == NULL) {
        snprintf(local_name, sizeof(local_name), "<null> %d", current->pid);
    }
    else {
        if (!copy_string(mm, local_name, name, sizeof(local_name))) {
            unlock_mm(mm);
            return ret;
        }
    }
    if ((ret = copy_kargv(mm, argc, kargv, argv)) != 0) {
        unlock_mm(mm);
        return ret;
    }
    path = argv[0];
    unlock_mm(mm);
    files_closeall(current->filesp);

    /* sysfile_open will check the first argument path, thus we have to use a user-space pointer, and argv[0] may be incorrect */    
    int fd;
    if ((ret = fd = sysfile_open(path, O_RDONLY)) < 0) {
        goto execve_exit;
    }
    LOG_TAB("已 open 用户程序文件.\n");
    if (mm != NULL) {
        lcr3(boot_cr3);
        if (mm_count_dec(mm) == 0) {
            exit_mmap(mm);
            put_pgdir(mm);
            mm_destroy(mm);
        }
        current->mm = NULL;
    }
    ret= -E_NO_MEM;;
    if ((ret = load_icode(fd, argc, kargv)) != 0) {
        goto execve_exit;
    }
    put_kargv(argc, kargv);
    set_proc_name(current, local_name);
    LOG_TAB("已设置进程名称为%s.\n", local_name);
    LOG("do_execve end\n");
    return 0;

execve_exit:
    put_kargv(argc, kargv);
    do_exit(ret);
    panic("already exit: %e.\n", ret);
}

// do_yield - ask the scheduler to reschedule
int
do_yield(void) {
    current->need_resched = 1;
    return 0;
}

// do_wait - wait one OR any children with PROC_ZOMBIE state, and free memory space of kernel stack
//         - proc struct of this child.
// NOTE: only after do_wait function, all resources of the child proces are free.

// 等待一个或多个子进程僵死并清理.
//  一旦子进程变为PROC_ZOMBIE状态,此函数则清理子进程的资源.
// 若 pid 为 0,则用户调用的是 wait()函数
// 若 pid 不为 0 则用户调用的是 waitpid()函数
//
// 考虑因素: 如果子进程还有子进程,则需要睡眠等待.重新调度进程队列.
//
// 清理资源: 1 从内核的进程维护表中移除  2 释放栈空间 释放进程控制块空间
int
do_wait(int pid, int *code_store) {
    struct mm_struct *mm = current->mm;
    if (code_store != NULL) {
        if (!user_mem_check(mm, (uintptr_t)code_store, sizeof(int), 1)) {
            return -E_INVAL;
        }
    }

    struct proc_struct *proc;
    bool intr_flag, haskid;
repeat:
    haskid = 0;     // 是否有子进程
    if (pid != 0) { // 若调用的是 waitpid()
        proc = find_proc(pid);
        if (proc != NULL && proc->parent == current) {  // 若找到的进程是当前进程的子进程,
            haskid = 1;
            if (proc->state == PROC_ZOMBIE) {       // 必须是僵尸状态才可回收
                goto found;
            }
        }
    }
    else {      // 调用的是 wait(),则找到任意一个处于退出状态的子进程
        proc = current->cptr;
        for (; proc != NULL; proc = proc->optr) {
            haskid = 1;
            if (proc->state == PROC_ZOMBIE) {
                goto found;
            }
        }
    }
    if (haskid) {
        current->state = PROC_SLEEPING;
        current->wait_state = WT_CHILD;
        schedule();
        if (current->flags & PF_EXITING) {
            do_exit(-E_KILLED);
        }
        goto repeat;
    }
    return -E_BAD_PROC;

found:
    if (proc == idleproc || proc == initproc) {
        panic("wait idleproc or initproc.\n");
    }
    if (code_store != NULL) {
        *code_store = proc->exit_code;
    }
    local_intr_save(intr_flag);
    {
        unhash_proc(proc);
        remove_links(proc);
    }
    local_intr_restore(intr_flag);
    put_kstack(proc);
    kfree(proc);
    return 0;
}

// do_kill - kill process with pid by set this process's flags with PF_EXITING
/**
 * 如何 kill 一个进程?
 * 
 * 1. 设置其标志位状态为 PF_EXITING
 */ 
int
do_kill(int pid) {
    struct proc_struct *proc;
    if ((proc = find_proc(pid)) != NULL) {
        if (!(proc->flags & PF_EXITING)) {
            proc->flags |= PF_EXITING;
            if (proc->wait_state & WT_INTERRUPTED) {
                wakeup_proc(proc);
            }
            return 0;
        }
        return -E_KILLED;
    }
    return -E_INVAL;
}

// kernel_execve - do SYS_exec syscall to exec a user program called by user_main kernel_thread
/**
 * 执行用户程序
 * 
 * name: 
 */ 
static int
kernel_execve(const char *name, const char **argv) {
    int argc = 0, ret;
    while (argv[argc] != NULL) {
        argc ++;
    }
    asm volatile (
        "int %1;"
        : "=a" (ret)
        : "i" (T_SYSCALL), "0" (SYS_exec), "d" (name), "c" (argc), "b" (argv)
        : "memory");
    return ret;
}

#define __KERNEL_EXECVE(name, path, ...) ({                         \
const char *argv[] = {path, ##__VA_ARGS__, NULL};       \
                     LOG("kernel_execve: pid = %d, name = \"%s\".\n",    \
                             current->pid, name);                            \
                     kernel_execve(name, argv);                              \
})

#define KERNEL_EXECVE(x, ...)                   __KERNEL_EXECVE(#x, #x, ##__VA_ARGS__)

#define KERNEL_EXECVE2(x, ...)                  KERNEL_EXECVE(x, ##__VA_ARGS__)

#define __KERNEL_EXECVE3(x, s, ...)             KERNEL_EXECVE(x, #s, ##__VA_ARGS__)

#define KERNEL_EXECVE3(x, s, ...)               __KERNEL_EXECVE3(x, s, ##__VA_ARGS__)

// user_main - kernel thread used to exec a user program
// 最终通过 exec 执行程序
static int
user_main(void *arg) {
LOG("已进入: user_main:\n");
#ifdef TEST
#ifdef TESTSCRIPT
    KERNEL_EXECVE3(TEST, TESTSCRIPT);
#else
    KERNEL_EXECVE2(TEST);
#endif
#else
    KERNEL_EXECVE(sh);
#endif
    panic("user_main execve failed.\n");
}

// init_main - the second kernel thread used to create user_main kernel threads
// initproc 内核线程执行函数
static int
init_main(void *arg) {
    LOG("init_main start:\n");
    int ret;
    if ((ret = vfs_set_bootfs("disk0:")) != 0) {
        panic("set boot fs failed: %e.\n", ret);
    }
    
    size_t nr_free_pages_store = nr_free_pages();
    size_t kernel_allocated_store = kallocated();

    int pid = kernel_thread(user_main, NULL, 0);
    LOG_TAB("已创建内核态用户线程:user_main\n");
    if (pid <= 0) {
        panic("create user_main failed.\n");
    }
 extern void check_sync(void);
    check_sync();                // check philosopher sync problem

    while (do_wait(0, NULL) == 0) { // 作为所有子进程的父进程,清理所有子进程资源
        schedule();
    }

    fs_cleanup();
        
    LOG_TAB("all user-mode processes have quit.\n");
    assert(initproc->cptr == NULL && initproc->yptr == NULL && initproc->optr == NULL);
    assert(nr_process == 2);
    assert(list_next(&proc_list) == &(initproc->list_link));
    assert(list_prev(&proc_list) == &(initproc->list_link));
    assert(nr_free_pages_store == nr_free_pages());
    assert(kernel_allocated_store == kallocated());
    LOG_TAB("init check memory pass.\n");
    LOG("init_main end.\n");
    return 0;
}

/*
 * 初始化进程环境
 *  1. 创建 1st 内核进程 idleproc
 *  2. 创建 2nd 内核进程 init_main
 */
void
proc_init(void) {
    
    LOG_LINE("初始化开始: 内核线程");
    LOG("proc_init begin:\n");
    LOG_TAB("初始化队列: proc_list\n");

    int i;
    // 初始化进程表
    list_init(&proc_list);
    for (i = 0; i < HASH_LIST_SIZE; i ++) {
        list_init(hash_list + i);
    }
    // 初始化idle PCB
    if ((idleproc = alloc_proc()) == NULL) {
        panic("cannot alloc idleproc.\n");
    }

    idleproc->pid = 0;                              // 0 号线程
    idleproc->state = PROC_RUNNABLE;
    idleproc->kstack = (uintptr_t)bootstack;        // idle 的内核栈自一开始就有,以后其他所有process的内核栈都需重新分配
    idleproc->need_resched = 1;
    
    if ((idleproc->filesp = files_create()) == NULL) {
        panic("create filesp (idleproc) failed.\n");
    }
    files_count_inc(idleproc->filesp);
    
    set_proc_name(idleproc, "idle");
    nr_process ++;

    current = idleproc;


    LOG_TAB("内核初始线程描述: idleproc\n");
    LOG_TAB("pid: %d\n",idleproc->pid);
    LOG_TAB("name: %s\n",idleproc->name);
    LOG_TAB("state: %s\n","PROC_RUNNABLE");
    LOG_TAB("kstack: 0x%08lx\n",(uintptr_t)bootstack);
    LOG_TAB("当前总进程数: %d\n",nr_process);
    LOG_TAB("current 进程pid: %s\n",idleproc->name);

    int pid = kernel_thread(init_main, NULL, 0);
    LOG_TAB("kernel_thread返回 pid:%d\n", pid);
    LOG_TAB("内核线程 init 已创建\n.");
    if (pid <= 0) {
        panic("create init_main failed.\n");
    }

    initproc = find_proc(pid);
    set_proc_name(initproc, "init");

    assert(idleproc != NULL && idleproc->pid == 0);
    assert(initproc != NULL && initproc->pid == 1);
    LOG("proc_init end\n");
}

// idle: 闲散的内核进程,不断地检测"当前进程"是否被指定暂时放弃资源.
void
cpu_idle(void) {
    LOG("cpu_idle:\n");
    while (1) {
        if (current->need_resched) {
            schedule();
        }
    }
}

//FOR LAB6, set the process's priority (bigger value will get more CPU time) 
void
lab6_set_priority(uint32_t priority)
{
    if (priority == 0)
        current->lab6_priority = 1;
    else current->lab6_priority = priority;
}

// do_sleep - set current process state to sleep and add timer with "time"
//          - then call scheduler. if process run again, delete timer first.
int
do_sleep(unsigned int time) {
    LOG("do_sleep:\n");
    LOG("当前进程信息:");
    if (time == 0) {
        return 0;
    }
    bool intr_flag;
    local_intr_save(intr_flag);
    timer_t __timer, *timer = timer_init(&__timer, current, time);
    current->state = PROC_SLEEPING;
    current->wait_state = WT_TIMER;
    LOG_TAB("当前进程状态更新为: PROC_SLEEPING, 等待状态:WT_TIMER \n");
    add_timer(timer);
    LOG_TAB("已创建 timer, 指向 current, 初始时间片: %d\n", time);
    
    local_intr_restore(intr_flag);

    schedule();

    del_timer(timer);
    return 0;
}
