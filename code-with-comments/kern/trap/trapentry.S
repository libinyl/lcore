#include <memlayout.h>

# struct pushregs {
#     uint32_t reg_edi;
#     uint32_t reg_esi;
#     uint32_t reg_ebp;
#     uint32_t reg_oesp;          /* Useless */
#     uint32_t reg_ebx;
#     uint32_t reg_edx;
#     uint32_t reg_ecx;
#     uint32_t reg_eax;
# };

# struct trapframe {
#     struct pushregs tf_regs;  4 pushal
#     uint16_t tf_gs;           3
#     uint16_t tf_padding0;
#     uint16_t tf_fs;           2
#     uint16_t tf_padding1;
#     uint16_t tf_es;           1
#     uint16_t tf_padding2;
#     uint16_t tf_ds;           0
#     uint16_t tf_padding3;
#     uint32_t tf_trapno;
#     /* below here defined by x86 hardware */
#     uint32_t tf_err;
#     uintptr_t tf_eip;
#     uint16_t tf_cs;
#     uint16_t tf_padding4;
#     uint32_t tf_eflags;
#     /* below here only when crossing rings, such as from user to kernel */
#     uintptr_t tf_esp;
#     uint16_t tf_ss;
#     uint16_t tf_padding5;
# } __attribute__((packed));



# vectors.S 把所有 trap 发送到此
.text
.globl __alltraps
__alltraps:
    # 中断陷入执行至此,由于 int 指令的结果,栈上已有
    # | trapno  |
    # |  errno  |
    # |   eip   |
    # |   cs    |
    # |  eflags |
    # |   ss    |
    # |   sp    |

    # 继续 push 寄存器值,以在栈空间构造struct trapframe中断帧.每个 pushl 都压入一个 uint32,对应 trapframe 中两个 uint16.
    pushl %ds
    pushl %es
    pushl %fs
    pushl %gs
    # pushal = pushregs
    pushal

    # 设置内核的 ds,es段地址为GD_KDATA,准备内核环境
    movl $GD_KDATA, %eax
    movw %ax, %ds
    movw %ax, %es

    # 把刚刚构造的 trap frame 的地址作为参数传入trap(),并调用
    pushl %esp
    call trap

    # 清理调用 trap 前压栈的 esp
    popl %esp

# 通过__trapret从中断返回.
# 中断返回:
# 



.globl __trapret
__trapret:
    # 1. 恢复各种寄存器值
    popal

    popl %gs
    popl %fs
    popl %es
    popl %ds

    # 去掉 trap number 和 error code
    addl $0x8, %esp
    # int n 命令:
    #   1. 标志寄存器入栈
    #   2. CS,IP 入栈,(IP)=(n*4),  (CS)=(n*4+2)
    # iret  命令:
    #   1. 恢复 CS,IP
    #   2. 恢复标志寄存器
    #   3. 恢复 ESP,SS.(权限发生变化)
    iret

# forkrets(current->tf);
# 参数是当前进程的 tf
# 此处直接利用参数中的 tf,设置新的进程的各种状态
.globl forkrets
forkrets:
    # set stack to this new process's trapframe
    movl 4(%esp), %esp
    jmp __trapret
