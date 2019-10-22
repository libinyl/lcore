#include <asm.h>

# CPU 启动: 跳转到 32 位保护模式,调用 C 代码.
# BIOS 从硬盘上的第一块扇区把此代码加载到物理内存 0x7c00 处,并在实模式下运行,初始 cs=0,ip=7c00.
# 此时使用分段式寻址方式,但初始状态是重置状态,全局描述符表区域被默认设置为,基址=0,limit=0FFFFH, 似乎是把整个内存空间都视作GDT,其本质上没有分段.
# 但我们要开启分段,否则无法执行 boot loader 和 kernel 的高地址代码.

# 具体指令作用可参考 https://www.cnblogs.com/maruixin/p/3175894.html
# 如何调试? b *0x7c00

.set PROT_MODE_CSEG,        0x8                     # 内核代码段选择子 kernel code segment selector
.set PROT_MODE_DSEG,        0x10                    # 内核数据段选择子 kernel data segment selector
.set CR0_PE_ON,             0x1                     # 保护模式位 protected mode enable flag
.set SMAP,                  0x534d4150

# 第一条指令指定地址应为 0:7c00, 即运行态bootloader 的起始地址
.globl start
start:
.code16                                             # 当前 CPU 处于实模式,所以需要在 16-bit 模式下编译.
    cli                                             # 关中断, 防止干扰开启A20,  和保证设置GDT的完整性
    cld                                             # 设置字符串操作是递增方向(direct flag标志位清零)

    # 设置重要的段寄存器(置零)(DS, ES, SS),全平铺
    xorw %ax, %ax                                   # Segment number zero
    movw %ax, %ds                                   # -> Data Segment
    movw %ax, %es                                   # -> Extra Segment
    movw %ax, %ss                                   # -> Stack Segment

    # 开启 A20:
    #  A20地址线并不是打开保护模式的关键, 但若不开A20, 无法使用1MB以上的内存.
    #  因为历史原因, 在计算机启动时, 是处于实模式下, A20是关闭状态, 超出1MB内存会回卷
    #  理论上, 打开A20的方法就是设置8042芯片输出端口(64h), 当该输出端口位1为1时就开启了A20信号线.
    #  但实际上, 当向8042芯片输出端口进行写操作时, 在键盘缓冲区中可能还有别的数据尚未处理, 因此必须先处理这些数据, 这也是关中断的原因之一.
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

# 探测内存分布
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
    # 但是切换到保护模式后,还会执行一些代码,才会调用 bootmain.
    lgdt gdtdesc

    # CR0中包含了6个预定义标志，0位是保护允许位PE(Protedted Enable)，用于启动保护模式，如果PE位置1，则保护模式启动.
    movl %cr0, %eax
    orl $CR0_PE_ON, %eax
    movl %eax, %cr0

    # Jump to next instruction, but in 32-bit code segment.
    # Switches processor into 32-bit mode.
    # 跳转到下一个指令,但是是在 32 位模式下.
    # 必须用 ljmp, 长转移指令
    ljmp $PROT_MODE_CSEG, $protcseg

.code32                                             # Assemble for 32-bit mode
protcseg:
    # 设置保护模式数据段寄存器
    # 每个数据段选择子设置为0x10.
    # 段选择子的格式是,高 12 位是 index.参考 x86 手册图 3-6.
    # 而所有数据段都位于 GDT 的第 2 项,所以把高 12 位设置为 2.
    movw $PROT_MODE_DSEG, %ax                       
    movw %ax, %ds                                   # -> DS: Data Segment
    movw %ax, %es                                   # -> ES: Extra Segment
    movw %ax, %fs                                   # -> FS
    movw %ax, %gs                                   # -> GS
    movw %ax, %ss                                   # -> SS: Stack Segment

    # 设置 sp(栈指针)为调用 C 代码.
    # 栈基址 = 0
    # 栈指针 = start. 为何栈基址设置为 0?事实上,只要一跳转到bootmain,栈基址指针会被立刻更新为sp.所以现在设置成什么无所谓.
    movl $0x0, %ebp
    movl $start, %esp
    # ok, 32位保护模式环境已经构建完毕(寄存器,GDT,保护模式位)
    call bootmain

    # bootmain 不应返回至此.
spin:
    jmp spin

# 截至加载 GDT 之前,整个物理内存的分段情况是base=0,limit=0FFFFH,即一大块,相当于没有分段.
# 但是 bootloader和 kernel 的代码段都是高位, 如果不更新 GDT的话无法访问.

.data
# Bootstrap 阶段 GDT,维护了 段选择子索引+段区间.寻址时,段寄存器拿着索引来找段区间,再加上偏移地址定位.
.p2align 2                                          # 强制 4 byte 对齐
gdt:
    SEG_NULLASM                                     # 第 0 项, null seg
    SEG_ASM(STA_X|STA_R, 0x0, 0xffffffff)           # 第 1 项, bootloader 和 kernel 的代码段, type = exec,read,  base = 0 ,limit = 0xffffffff = 4GB
    SEG_ASM(STA_W, 0x0, 0xffffffff)                 # 第 2 项, bootloader 和 kernel 的数据段, type = write,      base = 0 ,limit = 0xffffffff = 4GB

gdtdesc:
    .word 0x17                                      # sizeof(gdt) - 1
    .long gdt                                       # address gdt
