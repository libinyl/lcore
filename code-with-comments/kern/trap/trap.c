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
#define LOG_INT_INFO 0

static void print_ticks() {
    cprintf("%d ticks\n",TICK_NUM);
#ifdef DEBUG_GRADE
    cprintf("End of Test.\n");
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
    logline("初始化开始:中断向量表");

    extern uintptr_t __vectors[];
    int i;

    for (i = 0; i < sizeof(idt) / sizeof(struct gatedesc); i ++) {
        SETGATE(idt[i], 0, GD_KTEXT, __vectors[i], DPL_KERNEL);
    }
    SETGATE(idt[T_SYSCALL], 1, GD_KTEXT, __vectors[T_SYSCALL], DPL_USER);
    lidt(&idt_pd);

    // 输出信息
    if(LOG_INT_INFO){
        log("vec_num\tis_trap\tcode_seg\thandle_addr\tDPL\n");
        for (i = 0; i < sizeof(idt) / sizeof(struct gatedesc); i ++) {
            log("0x%x\t",i);
            log("%s\t", idt[i].gd_type==STS_TG32?"y":"n");
            log("%s\t", "GD_KTEXT");
            log("0x%x\t", __vectors[i]);
            log("%d",idt[i].gd_dpl);
            log("\n");
        }
    }
    logline("初始化完毕:中断向量表");
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
    cprintf("trapframe at %p\n", tf);
    print_regs(&tf->tf_regs);
    cprintf("  ds   0x----%04x\n", tf->tf_ds);
    cprintf("  es   0x----%04x\n", tf->tf_es);
    cprintf("  fs   0x----%04x\n", tf->tf_fs);
    cprintf("  gs   0x----%04x\n", tf->tf_gs);
    cprintf("  trap 0x%08x %s\n", tf->tf_trapno, trapname(tf->tf_trapno));
    cprintf("  err  0x%08x\n", tf->tf_err);
    cprintf("  eip  0x%08x\n", tf->tf_eip);
    cprintf("  cs   0x----%04x\n", tf->tf_cs);
    cprintf("  flag 0x%08x ", tf->tf_eflags);

    int i, j;
    for (i = 0, j = 1; i < sizeof(IA32flags) / sizeof(IA32flags[0]); i ++, j <<= 1) {
        if ((tf->tf_eflags & j) && IA32flags[i] != NULL) {
            cprintf("%s,", IA32flags[i]);
        }
    }
    cprintf("IOPL=%d\n", (tf->tf_eflags & FL_IOPL_MASK) >> 12);

    if (!trap_in_kernel(tf)) {
        cprintf("  esp  0x%08x\n", tf->tf_esp);
        cprintf("  ss   0x----%04x\n", tf->tf_ss);
    }
}

void
print_regs(struct pushregs *regs) {
    cprintf("  edi  0x%08x\n", regs->reg_edi);
    cprintf("  esi  0x%08x\n", regs->reg_esi);
    cprintf("  ebp  0x%08x\n", regs->reg_ebp);
    cprintf("  oesp 0x%08x\n", regs->reg_oesp);
    cprintf("  ebx  0x%08x\n", regs->reg_ebx);
    cprintf("  edx  0x%08x\n", regs->reg_edx);
    cprintf("  ecx  0x%08x\n", regs->reg_ecx);
    cprintf("  eax  0x%08x\n", regs->reg_eax);
}

static inline void
print_pgfault(struct trapframe *tf) {
    log("缺页异常信息:\n");

    /* error_code:
     * bit 0 == 0 means no page found, 1 means protection fault
     * bit 1 == 0 means read, 1 means write
     * bit 2 == 0 means kernel, 1 means user
     * */
    log("   page fault at 0x%08x,解析错误码,得到触发原因: %c/%c [%s].\n", rcr2(),
            (tf->tf_err & 4) ? 'U' : 'K',
            (tf->tf_err & 2) ? 'W' : 'R',
            (tf->tf_err & 1) ? "protection fault" : "no page found");
}

/**
 * page fault 处理函数.
 * 
 * 
 * 
 */ 
static int
pgfault_handler(struct trapframe *tf) {
    log("pgfault_handler: 开始处理缺页;\n");

    extern struct mm_struct *check_mm_struct;
    if(check_mm_struct !=NULL) { //used for test check_swap
            print_pgfault(tf);
        }
    struct mm_struct *mm;
    if (check_mm_struct != NULL) {
        assert(current == idleproc);
        mm = check_mm_struct;
    }
    else {
        if (current == NULL) {
            print_trapframe(tf);
            print_pgfault(tf);
            panic("unhandled page fault.\n");
        }
        mm = current->mm;
    }
    log("已获取触发缺页的 mm_struct\n");
    return do_pgfault(mm, tf->tf_err, rcr2());
}

static volatile int in_swap_tick_event = 0;
extern struct mm_struct *check_mm_struct;

static void
trap_dispatch(struct trapframe *tf) {
    //log("开始分发中断.中断号:%u\n",tf->tf_trapno);
    char c;

    int ret=0;

    switch (tf->tf_trapno) {
    case T_PGFLT:  //page fault
        log("内核检测到缺页异常中断.\n");
        if ((ret = pgfault_handler(tf)) != 0) {
            print_trapframe(tf);
            if (current == NULL) {
                panic("handle pgfault failed. ret=%d\n", ret);
            }
            else {
                if (trap_in_kernel(tf)) {
                    panic("handle pgfault failed in kernel mode. ret=%d\n", ret);
                }
                cprintf("killed by kernel.\n");
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
        // if(ticks%(100)==0){
        //     cprintf("触发了秒级时钟中断.\n");
        //     //调试 hack: 降低系统的进程切换时间
        //     // ticks 每次增加时过去了 10 毫秒,那么是 100 的倍数时经过 1 秒
        //     // n秒即是 100*n
        //     run_timer_list();
        // }
        break;
    case IRQ_OFFSET + IRQ_COM1:
        //c = cons_getc();
        //cprintf("serial [%03d] %c\n", c, c);
        //break;
    case IRQ_OFFSET + IRQ_KBD:
        c = cons_getc();
        //cprintf("kbd [%03d] %c\n", c, c);
        {
          extern void dev_stdin_write(char c);
          dev_stdin_write(c);
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
            cprintf("unhandled trap.\n");
            do_exit(-E_KILLED);
        }
        // in kernel, it must be a mistake
        panic("unexpected trap in kernel.\n");

    }
}

/**
 * 处理并分发一个中断或异常,包括所有异常
 * 如果返回,则trapentry.S 恢复 cpu 之前的状态(栈上保存,以 trapframe 的结构),然后执行 iret返回.
 */ 
void
trap(struct trapframe *tf) {
    //log("陷阱预处理,维护中断嵌套\n");
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

