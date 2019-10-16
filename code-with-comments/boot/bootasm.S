#include <asm.h>

# CPU 启动: 跳转到 32 位保护模式,调用 C 代码.
# BIOS 从硬盘上的第一块扇区把此代码加载到物理内存 0x7c00 处,并在实模式下运行,初始 cs=0,ip=7c00.

# 参考 https://www.cnblogs.com/maruixin/p/3175894.html

.set PROT_MODE_CSEG,        0x8                     # 内核代码段选择子 kernel code segment selector
.set PROT_MODE_DSEG,        0x10                    # 内核数据段选择子 kernel data segment selector
.set CR0_PE_ON,             0x1                     # 保护模式位 protected mode enable flag
.set SMAP,                  0x534d4150

# 第一条指令指定地址应为 0:7c00, 即运行态bootloader 的起始地址
.globl start
start:
.code16                                             # 当前 CPU 处于实模式,所以需要在 16-bit 模式下编译.
    cli                                             # 关中断
    cld                                             # 设置字符串操作是递增方向(direct flag标志位清零)

    # 设置重要的段寄存器(置零)(DS, ES, SS)
    xorw %ax, %ax                                   # Segment number zero
    movw %ax, %ds                                   # -> Data Segment
    movw %ax, %es                                   # -> Extra Segment
    movw %ax, %ss                                   # -> Stack Segment

    # Enable A20:
    #  For backwards compatibility with the earliest PCs, physical
    #  address line 20 is tied low, so that addresses higher than
    #  1MB wrap around to zero by default. This code undoes this.
seta20.1:
    inb $0x64, %al                                  # Wait for not busy
    testb $0x2, %al
    jnz seta20.1

    movb $0xd1, %al                                 # 0xd1 -> port 0x64
    outb %al, $0x64

seta20.2:
    inb $0x64, %al                                  # Wait for not busy
    testb $0x2, %al
    jnz seta20.2

    movb $0xdf, %al                                 # 0xdf -> port 0x60
    outb %al, $0x60

probe_memory:
    movl $0, 0x8000
    xorl %ebx, %ebx
    movw $0x8004, %di
start_probe:
    movl $0xE820, %eax
    movl $20, %ecx
    movl $SMAP, %edx
    int $0x15
    jnc cont
    movw $12345, 0x8000
    jmp finish_probe
cont:
    addw $20, %di
    incl 0x8000
    cmpl $0, %ebx
    jnz start_probe
finish_probe:

    # Switch from real to protected mode, using a bootstrap GDT
    # and segment translation that makes virtual addresses
    # identical to physical addresses, so that the
    # effective memory map does not change during the switch.
    # 从实模式切换到保护模式.
    # 切换方式是设置 GDT 并
    lgdt gdtdesc
    movl %cr0, %eax
    orl $CR0_PE_ON, %eax
    movl %eax, %cr0

    # Jump to next instruction, but in 32-bit code segment.
    # Switches processor into 32-bit mode.
    # 跳转到下一个指令,但是是在 32 位模式下.
    ljmp $PROT_MODE_CSEG, $protcseg

.code32                                             # Assemble for 32-bit mode
protcseg:
    # 设置保护模式数据段寄存器
    movw $PROT_MODE_DSEG, %ax                       # Our data segment selector
    movw %ax, %ds                                   # -> DS: Data Segment
    movw %ax, %es                                   # -> ES: Extra Segment
    movw %ax, %fs                                   # -> FS
    movw %ax, %gs                                   # -> GS
    movw %ax, %ss                                   # -> SS: Stack Segment

    # Set up the stack pointer and call into C. The stack region is from 0--start(0x7c00)
    # 设置 sp(栈指针),调用 C 代码.
    movl $0x0, %ebp
    movl $start, %esp
    call bootmain

    # bootmain 不应返回至此.
spin:
    jmp spin

.data
# Bootstrap 阶段 GDT.
.p2align 2                                          # force 4 byte alignment
gdt:
    SEG_NULLASM                                     # null seg
    SEG_ASM(STA_X|STA_R, 0x0, 0xffffffff)           # code seg for bootloader and kernel
    SEG_ASM(STA_W, 0x0, 0xffffffff)                 # data seg for bootloader and kernel

gdtdesc:
    .word 0x17                                      # sizeof(gdt) - 1
    .long gdt                                       # address gdt
