#ifndef __KERN_PROCESS_PROC_H__
#define __KERN_PROCESS_PROC_H__

#include <defs.h>
#include <list.h>
#include <trap.h>
#include <memlayout.h>
#include <skew_heap.h>


// 进程生命周期中的状态
enum proc_state {
    PROC_UNINIT = 0,  // 尚未初始化             uninitialized
    PROC_SLEEPING,    // 睡眠中,位于就绪队列      sleeping
    PROC_RUNNABLE,    // 就绪,位于就绪队列,(也可能正在运行,不位于就绪队列)  runnable(maybe running)
    PROC_ZOMBIE,      // 即将死亡,等待父进程回收资源                        almost dead, and wait parent proc to reclaim his resource
};

// Saved registers for kernel context switches.
// Don't need to save all the %fs etc. segment registers,
// because they are constant across kernel contexts.
// Save all the regular registers so we don't need to care
// which are caller save, but not the return register %eax.
// (Not saving %eax just simplifies the switching code.)
// The layout of context must match code in switch.S.
/**
 * 进程上下文信息(执行现场),用于进程切换时保存当前状态.
 * 
 * 此结构的布局与 switch.S 中严格一致.
 * 
 * 一旦调度器选择某个进程执行,就会用此结构中的信息.
 */ 
struct context {
    uint32_t eip;   // 上次停止执行时下一条指令的地址,即返回地址
    uint32_t esp;   // 上次停止执行的 esp
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
};

#define PROC_NAME_LEN               50
#define MAX_PROCESS                 4096
#define MAX_PID                     (MAX_PROCESS * 2)

extern list_entry_t proc_list;

struct inode;

struct proc_struct {
    enum proc_state state;                      // 进程状态
    int pid;                                    // 进程 ID
    int runs;                                   // the running times of Process
    uintptr_t kstack;                           // 每个进程都有独立的内核栈,位于内核地址空间内.不共享. cpu的 tr 寄存器维护tss结构的地址ts 此结构保存当前内核栈指针.每个进程在其内核栈以 trapframe 的形式保存当前的状态.
    volatile bool need_resched;                 // 是否期待cpu 重新调度(以暂时释放在本进程的计算资源) . bool value: need to be rescheduled to release CPU?
    struct proc_struct *parent;                 // 父进程
    struct mm_struct *mm;                       // 进程的内存描述符
    struct context context;                     // Switch here to run process,用于进程间切换
    struct trapframe *tf;                       // 当前中断的中断帧,总是指向内核栈的某个位置.每个进程在内核栈以 trapframe 的形式保存当前的状态.
    uintptr_t cr3;                              // 当前进程的PDT 基址
    uint32_t flags;                             // Process flag
    char name[PROC_NAME_LEN + 1];               // 进程名称
    list_entry_t list_link;                     // Process link list
    list_entry_t hash_link;                     // Process hash list
    int exit_code;                              // exit code (be sent to parent proc)
    uint32_t wait_state;                        // 等待状态
    struct proc_struct *cptr, *yptr, *optr;     // children, younger,older 进程
    struct run_queue *rq;                       // 包含当前进程的运行队列
    list_entry_t run_link;                      // run queue 运行队列链接
    int time_slice;                             // 该进程当前可运行的时间片,每次timer 到时递减.初始为 5.从 5 到 0 期间称为时间片.到期则让给别的进程->进程置于rq 队尾,重置为 5. time slice for occupying the CPU
    skew_heap_entry_t lab6_run_pool;            // FOR LAB6 ONLY: the entry in the run pool
    uint32_t lab6_stride;                       // FOR LAB6 ONLY: the current stride of the process
    uint32_t lab6_priority;                     // FOR LAB6 ONLY: the priority of process, set by lab6_set_priority(uint32_t)
    struct files_struct *filesp;                // 文件结构
};

#define PF_EXITING                  0x00000001      // getting shutdown

// 等待状态(等待原因))
#define WT_INTERRUPTED               0x80000000                    // the wait state could be interrupted
#define WT_CHILD                    (0x00000001 | WT_INTERRUPTED)  // wait child process
#define WT_KSEM                      0x00000100                    // wait kernel semaphore
#define WT_TIMER                    (0x00000002 | WT_INTERRUPTED)  // wait timer
#define WT_KBD                      (0x00000004 | WT_INTERRUPTED)  // wait the input of keyboard

#define le2proc(le, member)         \
    to_struct((le), struct proc_struct, member)

extern struct proc_struct *idleproc, *initproc, *current;

void proc_init(void);
void proc_run(struct proc_struct *proc);
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);

char *set_proc_name(struct proc_struct *proc, const char *name);
char *get_proc_name(struct proc_struct *proc);
void cpu_idle(void) __attribute__((noreturn));

struct proc_struct *find_proc(int pid);
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
int do_exit(int error_code);
int do_yield(void);
int do_execve(const char *name, int argc, const char **argv);
int do_wait(int pid, int *code_store);
int do_kill(int pid);
//FOR LAB6, set the process's priority (bigger value will get more CPU time)
void lab6_set_priority(uint32_t priority);
int do_sleep(unsigned int time);
#endif /* !__KERN_PROCESS_PROC_H__ */

