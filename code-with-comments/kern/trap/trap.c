#include <defs.h>
#include <mmu.h>
#include <memlayout.h>
#include <clock.h>
#include <trap.h>
#include <x86.h>
#include <stdio.h>
#include <assert.h>
#include <console.h>
#include <vmm.h>
#include <swap.h>
#include <kdebug.h>
#include <unistd.h>
#include <syscall.h>
#include <error.h>
#include <sched.h>
#include <sync.h>
#include <proc.h>

#define TICK_NUM 100

static void print_ticks() {
    LOG("%d ticks\n",TICK_NUM);
#ifdef DEBUG_GRADE
    LOG("End of Test.\n");
    panic("EOT: kernel seems ok.");
#endif
}

/* *
 * 中断描述符表: Interrupt descriptor table:
 *
 * Must be built at run time because shifted function addresses can't
 * be represented in relocation records.
 * */
static struct gatedesc idt[256] = {{0}};

static struct pseudodesc idt_pd = {
    sizeof(idt) - 1, (uintptr_t)idt
};

/* idt_init - initialize IDT to each of the entry points in kern/trap/vectors.S */
void
idt_init(void) {
    /**
     * 中断处理函数的入口地址定义在__vectors,位于kern/trap/vector.S
     */ 
    LOG_LINE("初始化开始:中断向量表");
    LOG("idt_init:\n\n");

    extern uintptr_t __vectors[];
    int i;

    for (i = 0; i < sizeof(idt) / sizeof(struct gatedesc); i ++) {
        SETGATE(idt[i], 0, GD_KTEXT, __vectors[i], DPL_KERNEL);
    }
    SETGATE(idt[T_SYSCALL], 1, GD_KTEXT, __vectors[T_SYSCALL], DPL_USER);
    lidt(&idt_pd);

    // 输出信息

    LOG_TAB("vec_num\tis_trap\tcode_seg\thandle_addr\tDPL\n");
    for (i = 0; i < sizeof(idt) / sizeof(struct gatedesc); i ++) {
        if(i == 0 || i == T_SYSCALL || i == sizeof(idt) / sizeof(struct gatedesc) -1){

            LOG_TAB("0x%x",i);
            LOG_TAB("%s", idt[i].gd_type==STS_TG32? "y" : "n");
            LOG_TAB("%s", "GD_KTEXT");
            LOG_TAB("0x%x", __vectors[i]);
            LOG_TAB("%d",idt[i].gd_dpl);
            LOG("\n");
            if(i != sizeof(idt) / sizeof(struct gatedesc) -1)
                LOG_TAB("...\tn\t...\t\t...\t\t0\n");
        }
    }

    LOG_LINE("初始化完毕:中断向量表");
}

/**
 * 中断名列表
 */ 
static const char *
trapname(int trapno) {
    static const char * const excnames[] = {
        "Divide error",
        "Debug",
        "Non-Maskable Interrupt",
        "Breakpoint",
        "Overflow",
        "BOUND Range Exceeded",
        "Invalid Opcode",
        "Device Not Available",
        "Double Fault",
        "Coprocessor Segment Overrun",
        "Invalid TSS",
        "Segment Not Present",
        "Stack Fault",
        "General Protection",
        "Page Fault",
        "(unknown trap)",
        "x87 FPU Floating-Point Error",
        "Alignment Check",
        "Machine-Check",
        "SIMD Floating-Point Exception"
    };

    if (trapno < sizeof(excnames)/sizeof(const char * const)) {
        return excnames[trapno];
    }
    if (trapno >= IRQ_OFFSET && trapno < IRQ_OFFSET + 16) {
        return "Hardware Interrupt";
    }
    return "(unknown trap)";
}

/* trap_in_kernel - test if trap happened in kernel */
bool
trap_in_kernel(struct trapframe *tf) {
    return (tf->tf_cs == (uint16_t)KERNEL_CS);
}

static const char *IA32flags[] = {
    "CF", NULL, "PF", NULL, "AF", NULL, "ZF", "SF",
    "TF", "IF", "DF", "OF", NULL, NULL, "NT", NULL,
    "RF", "VM", "AC", "VIF", "VIP", "ID", NULL, NULL,
};

void
print_trapframe(struct trapframe *tf) {
    LOG("trapframe at %p\n", tf);
    print_regs(&tf->tf_regs);
    LOG("  ds   0x----%04x\n", tf->tf_ds);
    LOG("  es   0x----%04x\n", tf->tf_es);
    LOG("  fs   0x----%04x\n", tf->tf_fs);
    LOG("  gs   0x----%04x\n", tf->tf_gs);
    LOG("  trap 0x%08x %s\n", tf->tf_trapno, trapname(tf->tf_trapno));
    LOG("  err  0x%08x\n", tf->tf_err);
    LOG("  eip  0x%08x\n", tf->tf_eip);
    LOG("  cs   0x----%04x\n", tf->tf_cs);
    LOG("  flag 0x%08x ", tf->tf_eflags);

    int i, j;
    for (i = 0, j = 1; i < sizeof(IA32flags) / sizeof(IA32flags[0]); i ++, j <<= 1) {
        if ((tf->tf_eflags & j) && IA32flags[i] != NULL) {
            LOG("%s,", IA32flags[i]);
        }
    }
    LOG("IOPL=%d\n", (tf->tf_eflags & FL_IOPL_MASK) >> 12);

    if (!trap_in_kernel(tf)) {
        LOG("  esp  0x%08x\n", tf->tf_esp);
        LOG("  ss   0x----%04x\n", tf->tf_ss);
    }
}

void
print_regs(struct pushregs *regs) {
    LOG("  edi  0x%08x\n", regs->reg_edi);
    LOG("  esi  0x%08x\n", regs->reg_esi);
    LOG("  ebp  0x%08x\n", regs->reg_ebp);
    LOG("  oesp 0x%08x\n", regs->reg_oesp);
    LOG("  ebx  0x%08x\n", regs->reg_ebx);
    LOG("  edx  0x%08x\n", regs->reg_edx);
    LOG("  ecx  0x%08x\n", regs->reg_ecx);
    LOG("  eax  0x%08x\n", regs->reg_eax);
}

static inline void
print_pgfault(struct trapframe *tf) {
    LOG("缺页异常详细信息:\n");

    /* error_code:
     * bit 0 == 0 means no page found, 1 means protection fault
     * bit 1 == 0 means read, 1 means write
     * bit 2 == 0 means kernel, 1 means user
     * */
    LOG_TAB("page fault at 0x%08x,解析错误码,得到触发原因: %c/%c [%s].\n", rcr2(),
            (tf->tf_err & 4) ? 'U' : 'K',
            (tf->tf_err & 2) ? 'W' : 'R',
            (tf->tf_err & 1) ? "protection fault" : "no page found");
}

/**
 * 
 * 当 vma 标记虚存可用,而处理器又找不到对应的页表条目, 或者权限不正确, 会触发 page fault 中断.
 * 
 * 目标: 尽可能恢复异常,让处理器可以正确访问此地址.
 * 
 * 陷入此中断时, 处理器带来了哪些信息?
 *     1) 触发异常的地址, 放置在 cr2 寄存器
 *     2) 错误码, 标识 page fault 的具体类型或原因
 * 
 * 我们需要如何处理?
 *     1) page fault 的类型(具体触发原因)多种多样,我们需要分类讨论. 
 *        对于处理器给中断的"输入",仍需要做具体校验,判断是否有可能恢复.比如,如果触发 page fault 的 va 没有对应的 vma,那就根本无法恢复.
 *        所以我们第一步就要确认维护这块虚存的 mm_struct;
 *     2) 然后把mm_struct和中断带来的信息解析出来, 交给下一层处理.
 * 
 */ 
static int
pgfault_handler(struct trapframe *tf) {
    LOG("pgfault_handler begin:\n");
    LOG_TAB("开始确定 mm_struct\n");

    extern struct mm_struct *check_mm_struct;
    if(check_mm_struct !=NULL) { //used for test check_swap
            print_pgfault(tf);
        }
    struct mm_struct *mm;
    if (check_mm_struct != NULL) {
        assert(current == idleproc);
        mm = check_mm_struct;
        LOG_TAB("检测到 check_mm_struct, 设置 mm = check_mm_struct\n");
    }
    else {
        if (current == NULL) {
            print_trapframe(tf);
            print_pgfault(tf);
            panic("unhandled page fault.\n");
        }
        mm = current->mm;
        LOG_TAB("设置 mm = current->mm\n");
        
    }
    LOG_TAB("mm_struct 确定完毕\n");
    LOG_TAB("page fault 错误码 tf->tf_err = 0x%08lx\n", tf->tf_err);
    LOG_TAB("page fault 异常地址: 0x%08lx", rcr2());
    return do_pgfault(mm, tf->tf_err, rcr2());
}

static volatile int in_swap_tick_event = 0;
extern struct mm_struct *check_mm_struct;

static void
trap_dispatch(struct trapframe *tf) {
    //LOG("trap_dispatch start:\n");
    //LOG("开始分发中断.中断号:%u\n",tf->tf_trapno);
    char c;

    int ret=0;

    switch (tf->tf_trapno) {
    case T_PGFLT:  //page fault
        LOG("内核检测到缺页异常中断.\n");
        if ((ret = pgfault_handler(tf)) != 0) {
            print_trapframe(tf);
            if (current == NULL) {
                panic("handle pgfault failed. ret=%d\n", ret);
            }
            else {
                if (trap_in_kernel(tf)) {
                    panic("handle pgfault failed in kernel mode. ret=%d\n", ret);
                }
                LOG("killed by kernel.\n");
                panic("handle user mode pgfault failed. ret=%d\n", ret); 
                do_exit(-E_KILLED);
            }
        }
        break;
    case T_SYSCALL:
        syscall();
        break;
    case IRQ_OFFSET + IRQ_TIMER:
#if 0
    LAB3 : If some page replacement algorithm(such as CLOCK PRA) need tick to change the priority of pages, 
    then you can add code here. 
#endif
        /**
         * 时钟中断处理.
         * 每经过 1 个时钟周期TICK_NUM,应当设置当前进程current->need_resched = 1,在 trap 返回前刷新调度.
         * kern/driver/clock.c
         * 
         * 1)更新系统时间;
         * 2)遍历 timer, and trigger the timers which are end to call scheduler.
         * 
         */ 
        ticks ++;
        assert(current != NULL);
         if(ticks%(100)==0){
        //     LOG("触发了秒级时钟中断.\n");
        //     //调试 hack: 降低系统的进程切换时间
        //     // ticks 每次增加时过去了 10 毫秒,那么是 100 的倍数时经过 1 秒
        //     // n秒即是 100*n
             run_timer_list();
         }
        break;
    case IRQ_OFFSET + IRQ_COM1:
        //c = cons_getc();
        //LOG("serial [%03d] %c\n", c, c);
        //break;
    case IRQ_OFFSET + IRQ_KBD:
        c = cons_getc();
        //LOG("\nkbd [%03d] %c\n", c, c);
        {
          extern void dev_stdin_write(char c);
          dev_stdin_write(c);   //输入回显
        }
        break;
    //LAB1 CHALLENGE 1 : YOUR CODE you should modify below codes.
    case T_SWITCH_TOU:
    case T_SWITCH_TOK:
        panic("T_SWITCH_** ??\n");
        break;
    case IRQ_OFFSET + IRQ_IDE1:
    case IRQ_OFFSET + IRQ_IDE2:
        /* do nothing */
        break;
    default:
        print_trapframe(tf);
        if (current != NULL) {
            LOG("unhandled trap.\n");
            do_exit(-E_KILLED);
        }
        // in kernel, it must be a mistake
        panic("unexpected trap in kernel.\n");

    }
    //LOG("trap_dispatch end\n");
}

/**
 * 处理并分发一个中断或异常,包括所有异常
 * 如果返回,则trapentry.S 恢复 cpu 之前的状态(栈上保存,以 trapframe 的结构),然后执行 iret返回.
 */ 
void
trap(struct trapframe *tf) {
    //LOG("陷阱预处理,维护中断嵌套\n");
    // 基于陷阱的类型,分发 trapframe
    if (current == NULL) {
        trap_dispatch(tf);
    }
    else {
        // 在栈上维护一个 trapframe 链,用于处理中断嵌套
        //1 暂存当前进程的上一个 tf
        struct trapframe *otf = current->tf;
        //2 更新当前进程的 tf
        current->tf = tf;
    
        bool in_kernel = trap_in_kernel(tf);

        // 处理当前tf
        trap_dispatch(tf);
        // 恢复上一个 tf
        current->tf = otf;

        // 只有用户态进程可以抢占.内核进程不可抢占
        if (!in_kernel) {
            // 检查进程标志,是否被标记为希望"被"退出
            if (current->flags & PF_EXITING) {
                do_exit(-E_KILLED);
            }
            // 每次系统调用,当前进程都可能被标记为应被调度.此时进行调度刷新工作
            if (current->need_resched) {
                schedule();
            }
        }
    }
}

