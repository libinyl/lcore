# 直到建立虚拟地址空间之前, 地址的访问都是按照在 bootasm.S 设置的段映射关系,且在 bootloader 中elf 加载时做了变换,否则无法执行如此高地址的代码.
# 参考 lab2 附录 C


#include <mmu.h>
#include <memlayout.h>

#define REALLOC(x) (x - KERNBASE)

.text
.globl kern_entry
kern_entry:

    # 目标: 作为内核入口点, 建立好 boot 阶段的映射关系,把虚拟内存 [0, 4M)和[KERNBASE, KERNBASE+4M)都映射到物理内存[0, 4M)上.
    # 原因: 
    #       1) 高地址映射:内核运行地址执行了从 KERNBASE 起始.
    #       2) 低地址映射: 在 movl %eax, %cr0 之后还有几个指令需要执行,如果不映射的话就访问不到了.

    # 加载 boot pgdir 的物理地址到 cr3
    # 执行下面这几条指令时,还没有开启虚拟地址机制.
    movl $REALLOC(__boot_pgdir), %eax
    movl %eax, %cr3

    # 使能 paging
    movl %cr0, %eax
    orl $(CR0_PE | CR0_PG | CR0_AM | CR0_WP | CR0_NE | CR0_TS | CR0_EM | CR0_MP), %eax
    andl $~(CR0_TS | CR0_EM), %eax
    movl %eax, %cr0

    # update eip
    # now, eip = 0x1.....
    # 执行下面这条指令时,虽然访问的仍然是处于[0,4MB)的空间,但已经是虚拟地址了,会经过页表的查找.如果不把[0,4MB)->[0,4MB),则这几条指令无法执行.
    # 为了更新 eip
    leal next, %eax
    # set eip = KERNBASE + 0x1.....
    jmp *%eax
next:
    # jmp 过来之后,
    # 取消虚拟地址 0 ~ 4M 的映射,即将 __boot_pgdir 置零.
    xorl %eax, %eax
    movl %eax, __boot_pgdir

    # set ebp, esp
    movl $0x0, %ebp
    # 内核栈范围是 [bootstack, bootstacktop)
    # 内核栈大小是 KSTACKSIZE (8KB), 在memlayout.h 中定义.
    movl $bootstacktop, %esp
    # now kernel stack is ready , call the first C function
    call kern_init

# should never get here
spin:
    jmp spin

.data
.align PGSIZE
    .globl bootstack
bootstack:
    .space KSTACKSIZE
    .globl bootstacktop
bootstacktop:

# 内核内置一级页表.
# 每个一级页表对应 1024 个二级页表,即 4MB
# 我们只需要映射两块,把对虚拟内存 [0,4M)和[KERNBASE, KERNBASE+4M)都映射到物理内存[0,4M)上.
# 所以只需 2 个一级页表项.
# 但这两个一级页表项之间存在间隔.
# 虚拟地址[0,4M)对应的一级页表项索引是 0

.section .data.pgdir
.align PGSIZE
__boot_pgdir:
.globl __boot_pgdir
    # 第一个一级页表项,把虚拟地址 0 ~ 4M 临时映射到 0 ~ 4M,
    .long REALLOC(__boot_pt1) + (PTE_P | PTE_U | PTE_W)
    # 从 0 到KERNBASE,中间间隔了 KERNBASE/4M 项. 
    .space (KERNBASE >> PGSHIFT >> 10 << 2) - (. - __boot_pgdir) # pad to PDE of KERNBASE
    # 第二个一级页表项,把虚拟地址 KERNBASE + (0 ~ 4M) 映射到物理地址 0 ~ 4M
    .long REALLOC(__boot_pt1) + (PTE_P | PTE_U | PTE_W)
    .space PGSIZE - (. - __boot_pgdir) # pad to PGSIZE

# ↓ 两个一级页表项都指向下边的二级页表项

# boot 阶段单个二级页表内容
# 一个二级页表 1024 项
# 每项的内容: 从 0 开始,每项的值递增 4096,即 i * PGSIZE,辅以属性
.set i, 0
__boot_pt1:
.rept 1024
    .long i * PGSIZE + (PTE_P | PTE_W)
    .set i, i + 1
.endr

