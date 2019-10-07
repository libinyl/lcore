#ifndef __KERN_TRAP_TRAP_H__
#define __KERN_TRAP_TRAP_H__

#include <defs.h>

/* Trap Numbers */

/** 处理器定义的中断号 **/
#define T_DIVIDE                0   // divide error
#define T_DEBUG                 1   // debug exception
#define T_NMI                   2   // non-maskable interrupt
#define T_BRKPT                 3   // breakpoint
#define T_OFLOW                 4   // overflow
#define T_BOUND                 5   // bounds check
#define T_ILLOP                 6   // illegal opcode
#define T_DEVICE                7   // device not available
#define T_DBLFLT                8   // double fault
// #define T_COPROC             9   // reserved (not used since 486)
#define T_TSS                   10  // invalid task switch segment
#define T_SEGNP                 11  // segment not present
#define T_STACK                 12  // stack exception
#define T_GPFLT                 13  // general protection fault
#define T_PGFLT                 14  // page fault
// #define T_RES                15  // reserved
#define T_FPERR                 16  // floating point error
#define T_ALIGN                 17  // aligment check
#define T_MCHK                  18  // machine check
#define T_SIMDERR               19  // SIMD floating point error

/* Hardware IRQ numbers. We receive these as (IRQ_OFFSET + IRQ_xx) */
// IRQ = Interrupt Request
#define IRQ_OFFSET              32  // IRQ 0 corresponds to int IRQ_OFFSET

#define IRQ_TIMER               0
#define IRQ_KBD                 1
#define IRQ_COM1                4
#define IRQ_IDE1                14
#define IRQ_IDE2                15
#define IRQ_ERROR               19
#define IRQ_SPURIOUS            31

/* *
 * These are arbitrarily chosen, but with care not to overlap
 * processor defined exceptions or interrupt vectors.
 * */
#define T_SWITCH_TOU                120    // user/kernel switch
#define T_SWITCH_TOK                121    // user/kernel switch

/* 执行pushal 指令时,这些寄存器值入栈 */
/* 参考 syscall,这些都是系统调用时传入的参数和返回值参数;eax 接收返回值 */
struct pushregs {
    uint32_t reg_edi;
    uint32_t reg_esi;
    uint32_t reg_ebp;
    uint32_t reg_oesp;          /* Useless */
    uint32_t reg_ebx;
    uint32_t reg_edx;
    uint32_t reg_ecx;
    uint32_t reg_eax;
};

struct trapframe {
    struct pushregs tf_regs;    // pushal
    uint16_t tf_gs;
    uint16_t tf_padding0;
    uint16_t tf_fs;
    uint16_t tf_padding1;
    uint16_t tf_es;
    uint16_t tf_padding2;
    uint16_t tf_ds;
    uint16_t tf_padding3;
    uint32_t tf_trapno;
    /* 下列字段由 x86 硬件定义 */
    uint32_t tf_err;
    uintptr_t tf_eip;   // 产生中断后
    uint16_t tf_cs;
    uint16_t tf_padding4;
    uint32_t tf_eflags;
    /* 下列字段近用于跨级别调用,如从 user 跨入 kernel */
    uintptr_t tf_esp;
    uint16_t tf_ss;
    uint16_t tf_padding5;
} __attribute__((packed));//取消结构在编译过程中的优化对齐,按照实际结构对齐.

void idt_init(void);
void print_trapframe(struct trapframe *tf);
void print_regs(struct pushregs *regs);
bool trap_in_kernel(struct trapframe *tf);

#endif /* !__KERN_TRAP_TRAP_H__ */

