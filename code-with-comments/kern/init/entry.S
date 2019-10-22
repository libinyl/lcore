# 直到建立虚拟地址空间之前, 地址的访问都是按照在 bootasm.S 设置的段映射关系,且在 bootloader 中elf 加载时做了变换,否则无法执行如此高地址的代码.
# 参考 lab2 附录 C
# 事实上,这个 entry.S 就已经定义了内核的整体结构--代码段,数据段等.
# memlayout.h 则更完整地描述了包含用户态程序地址的结构.
# 汇编指令可参考 https://docs.oracle.com/cd/E26502_01/html/E28388/eoiyg.html

#include <mmu.h>
#include <memlayout.h>

#define REALLOC(x) (x - KERNBASE)

.text
.globl kern_entry
kern_entry:

    # 内核入口目标: 作为内核入口点, 建立好 boot 阶段的映射关系,把虚拟内存 [0, 4M)和[KERNBASE, KERNBASE+4M)都映射到物理内存[0, 4M)上.
    # 原因: 
    #       0) bootloader 建立的是临时的偏移式分段虚拟地址(bootmain.c, 几处& 0xFFFFFF),现在转化为由 MMU 管理的真正的地址翻译,
    #       1) 高地址映射:内核运行期地址, 起始于 KERNBASE.
    #       2) 低地址映射: 在 movl %eax, %cr0 之后还有几个指令需要执行,如果不映射的话就访问不到了.

    # 初始化内核环境的页表
    movl $REALLOC(__boot_pgdir), %eax
    movl %eax, %cr3

    # 使能 paging.
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

    # 内核栈就绪, 调用首个 C 函数
    call kern_init

# should never get here
spin:
    jmp spin


# ↓ 注意这里, .data 段很多结构的布局被固定了.
# ↓ 
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
# 第一个页表项的上限是KERNBASE,共占用 768 个 entry,共 3072Byte<PAGESIZE,加上第二个页表项,再加上对齐,也没超过1 个 PAGESIZE.
# 而一个 PAGESIZE 可以容纳 4K/4=1K 个 entry. KERNBASE 大概在其中 3/4 的位置,还可以容纳 1K - 768 = 256 个 entry,即 1G 的容量.

# 实际 nm 输出
# c0158000 d __boot_pt1
# c0157c00 d __second_PDE
# c0157000 D __boot_pgdir
# 也可以得到验证, 一级页表共占用 0x1000Byte=4KB


.section .data.pgdir
.align PGSIZE
__boot_pgdir:
.globl __boot_pgdir
    # 第一个一级页表项,把虚拟地址 0 ~ 4M 临时映射到 0 ~ 4M. 在跳到 kern_init 之前就已抹除.
    .long REALLOC(__boot_pt1) + (PTE_P | PTE_U | PTE_W)
    # 从 0 到KERNBASE,中间间隔了 KERNBASE/4M = 3072/4=768 项,共 768*4B = 3072Byte,不到一个 PAGESIZE.
    # 为何最后还要<<2 ?每个页表项占用 1 个long,是 32bit,从 byte 到 long 需要*4,即<<2
    .space (KERNBASE >> PGSHIFT >> 10 << 2) - (. - __boot_pgdir) # 与 KERNBASE 的一级页表项对齐
__second_PDE:
    # 第二个一级页表项,把虚拟地址 KERNBASE + (0 ~ 4M) 映射到物理地址 0 ~ 4M
    .long REALLOC(__boot_pt1) + (PTE_P | PTE_U | PTE_W)
    .space PGSIZE - (. - __boot_pgdir) # 与 PAGESIZE 对齐.

# ↓ 两个一级页表项都指向下边的二级页表项

# boot 阶段临时单个二级页表内容,专门用于映射至 [0,4MB)
# 定义: 一个二级页表 1024 项,即按 1K 再次分页
# 每项的内容: 从 0 开始,每项的值递增 4096,即 i * PGSIZE,辅以属性
.set i, 0
__boot_pt1:
.globl __boot_pt1
.rept 1024
    .long i * PGSIZE + (PTE_P | PTE_W)
    .set i, i + 1
.endr

# 关于页表的一些初步概念和计算,参考Intel 手册 Figure 4-4,Table 4-5,Table 4-6
# Q 为什么要定义页表?
#       - 页表维护了虚拟地址 va 到物理地址 pa 的映射,使得va=>pa可以不再是线性映射,而是通过页表进行自由设置.引入页表后好处多多.
#       - 节省空间.比如对于一些只读代码片段,如果有多块程序访问,完全可以映射到同一块物理内存,实现代码共享,节省空间.
#       - 扩展空间.比如在 ucore 中,页表的第 0 bit的置位情况就被解读为此页内存是存在与内存中还是存在与硬盘(swap)中.通过引入磁盘交换分区,并通过页表记录当前页位置,实现可用内存的扩充.
# Q cpu 定义了page entry 的意义.
#       - 首先定义分页的单位,通常是 4KB.也可以选择4MB 的大页,但目前不常见.
#       - 以 4KB 为单位分页意味着 va 的后 12 位一定是用于最后的具体寻址,因为 4K = 2^2*2^10 = 1<<12,12 个 bit 才足够对 4K 的寻址.
#       - x86 架构定义page entry高 20 位用于索引,低 12 位标记一些属性. 属性之一是定义 page 的单位,通常是 4KB.
#       - 20 位用于索引, 意味着每个页表最多可以容纳 1 << 20 = 1M 个 entry.
#       - 每个 page entry 是 32bit=4Byte.
# Q 如果定义了一级页表,每个页表项映射多少空间?
#       - 当然是 4KB,也就是 1 页.换句话说,不管几级页表,最后一个页表中的页表项一定对应4KB.
# Q 如果定义了两级页表,那么一级页表的页表项映射多少空间?
#       - 视可用内存而定.
#       - 比如现有 4GB 内存, 如果全部用页表映射,需要多少个页表?需要 4G/4K=1M个.现在将这 1M 个entry,再按 1K 分页,则需要 1M/1K=1K 个一级页表项,这些一级页表项需要10bit 来存储他们的地址.事实上这就是 ucore的做法.
#       - 按照上面的计算方式,则每个 1 级页表项维护 1K 个二级页表项,对应物理内存共 1K*4K = 4MB.
# Q 现有 X Byte大小物理空间,计划用二级页表映射,那么一级页表需要多大空间?
#       - 若X 大于 4MB,需要 X/4MB 个一级页表 entry,每个 entry 是 32bit=4Byte, 则共需要 X/1K/4MB*4Byte= X/1K 字节.实际计算时注意考虑与 PAGESIZE 对齐.
# - 编程时需要注意什么?
#       - 两级页表应看做同一抽象层级,比如 get_pte,中间应该屏蔽多级页表的转换过程.