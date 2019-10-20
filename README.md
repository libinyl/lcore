## lcore - a simple toy OS

*inspired by ucore*

此 OS 纯个人娱乐 & 笔记向, 内含大量学习过程做的注释, 部分可能有失准确, 仅供参考.

## 一些可能有用的图示:

*进程数据结构*
![](https://github.com/libinyl/ucore-study/blob/master/images/%E8%BF%9B%E7%A8%8B%E6%95%B0%E6%8D%AE%E7%BB%93%E6%9E%84%201.png?raw=1)


*VFS 与 SFS*

![](https://github.com/libinyl/ucore-study/blob/master/images/VFS%20%E4%B8%8E%20SFS%20%E7%9A%84%E8%A1%94%E6%8E%A5.png?raw=1)

*文件系统示意图*
![](https://github.com/libinyl/ucore-study/blob/master/images/%E7%9B%AE%E5%BD%95%E6%A0%91%E7%9A%84%E7%A3%81%E7%9B%98%E7%BA%A7%E8%A1%A8%E7%A4%BA2.png?raw=1)

## 部分日志输出

```shell
--------------历史过程--------------

CPU上电,BIOS 自检
BIOS 从 #0 sector 把 bootloader 加载到 0x7c00 并执行

bootloader:
	A20地址线打开
	探测物理内存分布
	初始化 boot-time GDT
	使能 32 bit 保护模式
	设置 C 语言环境, 栈相关指针
	bootmain 将 KERNEL 的 elf header 从第二块扇区加载到内存 64KB 处
	解析 elf header 信息, 将 kernel 完全加载
	控制权转移到 kernel entry

kernel entry:
	设置内核环境的页表, 使能分页
	设置内核栈基址和栈指针
	控制权交给 kern_init


--------------内核设计规格--------------

	text start          : 	0xc0100000 = 3072 M + 1024K
	entry               : 	0xc0100036 = 3072 M + 1024K
	etext               : 	0xc0116003 = 3072 M + 1112K
	edata               : 	0xc015c000 = 3072 M + 1392K
	end(.bss 结束))   : 	0xc0160324 = 3072 M + 1408K
	内核文件预计占用最大内存	:	4MB
	内核文件实际占用内存		:	385 KB
	内核可管理物理内存大小上限	:	0x38000000 Byte = 896 MB
	内核虚拟地址区间(B)		:	[0xc0000000 , 0xf8000000)
	内核虚拟地址区间(M)		:	[3072 M, 3968 M)
	内存分页大小			:	4096 B



--------------初始化开始:内存管理模块--------------

目标: 建立完整的虚拟内存机制.
	已维护内核页表物理地址.
	物理内存管理器名称: default_pmm_manager
	物理内存管理器实例- default_pmm_manager 初始化完毕.


--------------初始化开始:分页式虚拟地址空间管理--------------

目标: 根据探测得到的物理空间分布,初始化 pages 表格.

	1. 确定 pages 基址. 通过 ends 向上取整得到, 位于 end 之上, 这意味着从此就已经突破了内核文件本身的内存空间,开始动态分配内存
	2. 确定 page 数 npages,即 可管理内存的页数.
		2.1 确定实际管理的物理内存大小maxpa.即向上取探测结果中的最大可用地址,但不得大于管理上限 KMEMSIZE. maxpa = min{maxpa, KMEMSIZE}.
		2.2 npage = maxpa/PAGESIZE.
		3. 确定可管理内存中每个空闲 page 的属性,便于日后的换入换出的调度; 加入到 freelist 中.

1) e820map信息报告:

   共探测到6块内存区域:

	区间[0]:[00000000, 0009fbff], 大小: 0x0009fc00 Byte, 类型: 1, 	系可用内存.
		调整已知物理空间最大值 maxpa 至 0x0009fc00 = 639 K = 0 M
	区间[1]:[0009fc00, 0009ffff], 大小: 0x00000400 Byte, 类型: 2, 	系不可用内存.
	区间[2]:[000f0000, 000fffff], 大小: 0x00010000 Byte, 类型: 2, 	系不可用内存.
	区间[3]:[00100000, 07fdffff], 大小: 0x07ee0000 Byte, 类型: 1, 	系可用内存.
		调整已知物理空间最大值 maxpa 至 0x07fe0000 = 130944 K = 127 M
	区间[4]:[07fe0000, 07ffffff], 大小: 0x00020000 Byte, 类型: 2, 	系不可用内存.
	区间[5]:[fffc0000, ffffffff], 大小: 0x00040000 Byte, 类型: 2, 	系不可用内存.

2) 物理内存维护表格 pages 初始化:

	实际管理物理内存大小 maxpa = 0x07fe0000 = 127M
	需要管理的内存页数 npage = maxpa/PGSIZE = 32736
	内核文件地址边界 end: 0xc0160324
	表格起始地址 pages = ROUNDUP(end) = 0xc0161000 = 3073 M
	pages 表格自身内核虚拟地址区间 [pages,pages*n): [0xc0161000, 0xc0280b80)B,已被设置为不可交换.
	pages 表格结束于物理地址 freemem :0x00280b80B ≈ 2M. 也是后序可用内存的起始地址.

	考察管理区间, 将空闲区域标记为可用.
	考察区间: [00000000,0009fc00):		此区间不可用, 原因: 边界非法.
	考察区间: [0009fc00,000a0000):		此区间不可用, 原因: BIOS 认定非可用内存.
	考察区间: [000f0000,00100000):		此区间不可用, 原因: BIOS 认定非可用内存.
	考察区间: [00100000,07fe0000):		此区间可用, 大小为 0x07d5f000 B = 128380 KB = 125 MB = 32095 page.
		default_init_memmap:
			已将一块连续地址空间加入 freelist,起始: 0xc0166a24, page 数:32095.
			� page 数:32095
	考察区间: [07fe0000,08000000):		此区间不可用, 原因: BIOS 认定非可用内存.
	考察区间: [fffc0000,100000000):		此区间不可用, 原因: BIOS 认定非可用内存.


--------------初始化完毕:分页式虚拟地址空间管理--------------

	default_check(): succeed!
	check_alloc_page() succeeded!
	check_pgdir() succeeded!

开始建立一级页表自映射: [VPT, VPT + 4MB) => [PADDR(boot_pgdir), PADDR(boot_pgdir) + 4MB).
自映射完毕,当前页表:

	--- 页表信息 begin ---

	PDE(001) c0000000-c0400000 00400000 urw
	  |-- PTE(00400) c0000000-c0400000 00400000 -rw
	PDE(001) fac00000-fb000000 00400000 -rw
	  |-- PTE(00001) faf00000-faf01000 00001000 urw
	  |-- PTE(00001) fafeb000-fafec000 00001000 -rw

	--- 页表信息 end ---


开始建立区间映射: [KERNBASE, KERNBASE + KERNBASE) => [0, KMEMSIZE).
	映射区间[0x00000000,0x00000000 + 0x38000000 ) => [0xc0000000, 0xc0000000 + 0x38000000 )
	区间长度 = 896 M
	校准后得到需映射页数 = 229376
	映射完毕, 直接按照可管理内存上限映射. 内核虚拟地址的 1G 内存空间对应一级页表的上 1/4, 即  256 项; 对应 256 个二级页表, 占用空间 256 * 1024 = 256KB


--------------完毕: 内核区域映射--------------



--------------初始化开始: 全局段描述表&TSS--------------



--------------初始化完毕: 全局段描述表&TSS--------------

check_boot_pgdir() succeeded!

	--- 页表信息 begin ---

	PDE(0e0) c0000000-f8000000 38000000 urw
	  |-- PTE(38000) c0000000-f8000000 38000000 -rw
	PDE(001) fac00000-fb000000 00400000 -rw
	  |-- PTE(000e0) faf00000-fafe0000 000e0000 urw
	  |-- PTE(00001) fafeb000-fafec000 00001000 -rw

	--- 页表信息 end ---



--------------初始化完毕:内存管理模块--------------



--------------初始化完毕:中断控制器--------------



--------------初始化开始:中断向量表--------------

	vec_num	is_trap	code_seg	handle_addr	DPL
	0x0	n	GD_KTEXT	0xc010341a	0
	...	...	...	...	...		...
	0x80	y	GD_KTEXT	0xc010388a...	...	...	...	...		...
	0xff	n	GD_KTEXT	0xc0103e7e	0


--------------初始化完毕:中断向量表--------------



--------------测试开始:虚拟内存管理模块(vmm)--------------



--------------开始测试: page fault--------------


	--- 页表信息 begin ---

	PDE(0e0) c0000000-f8000000 38000000 urw
	  |-- PTE(38000) c0000000-f8000000 38000000 -rw
	PDE(001) fac00000-fb000000 00400000 -rw
	  |-- PTE(000e0) faf00000-fafe0000 000e0000 urw
	  |-- PTE(00001) fafeb000-fafec000 00001000 -rw

	--- 页表信息 end ---

内核检测到缺页异常中断.
pgfault_handler: 开始处理缺页;
缺页异常信息:
   page fault at 0x00000100,解析错误码,得到触发原因: K/W [no page found].
已获取触发缺页的 mm_struct


--------------测试通过: page fault--------------



--------------测试结束:虚拟内存管理模块(vmm)--------------



--------------初始化开始:进程调度器--------------



--------------初始化完毕:进程调度器--------------



--------------初始化开始:内核线程--------------



--------------初始化结束:内核线程--------------



--------------初始化开始:ide 磁盘控制器--------------



--------------初始化开始:ide 磁盘控制器--------------



--------------初始化开始:交换分区--------------

内核检测到缺页异常中断.
pgfault_handler: 开始处理缺页;
缺页异常信息:
   page fault at 0x00001000,解析错误码,得到触发原因: K/W [no page found].
已获取触发缺页的 mm_struct
内核检测到缺页异常中断.
pgfault_handler: 开始处理缺页;
缺页异常信息:
   page fault at 0x00002000,解析错误码,得到触发原因: K/W [no page found].
已获取触发缺页的 mm_struct
内核检测到缺页异常中断.
pgfault_handler: 开始处理缺页;
缺页异常信息:
   page fault at 0x00003000,解析错误码,得到触发原因: K/W [no page found].
已获取触发缺页的 mm_struct
内核检测到缺页异常中断.
pgfault_handler: 开始处理缺页;
缺页异常信息:
   page fault at 0x00004000,解析错误码,得到触发原因: K/W [no page found].
已获取触发缺页的 mm_struct
内核检测到缺页异常中断.
pgfault_handler: 开始处理缺页;
缺页异常信息:
   page fault at 0x00005000,解析错误码,得到触发原因: K/W [no page found].
已获取触发缺页的 mm_struct
内核检测到缺页异常中断.
pgfault_handler: 开始处理缺页;
缺页异常信息:
   page fault at 0x00001000,解析错误码,得到触发原因: K/W [no page found].
已获取触发缺页的 mm_struct
内核检测到缺页异常中断.
pgfault_handler: 开始处理缺页;
缺页异常信息:
   page fault at 0x00002000,解析错误码,得到触发原因: K/W [no page found].
已获取触发缺页的 mm_struct
内核检测到缺页异常中断.
pgfault_handler: 开始处理缺页;
缺页异常信息:
   page fault at 0x00003000,解析错误码,得到触发原因: K/W [no page found].
已获取触发缺页的 mm_struct
内核检测到缺页异常中断.
pgfault_handler: 开始处理缺页;
缺页异常信息:
   page fault at 0x00004000,解析错误码,得到触发原因: K/W [no page found].
已获取触发缺页的 mm_struct
内核检测到缺页异常中断.
pgfault_handler: 开始处理缺页;
缺页异常信息:
   page fault at 0x00005000,解析错误码,得到触发原因: K/W [no page found].
已获取触发缺页的 mm_struct
内核检测到缺页异常中断.
pgfault_handler: 开始处理缺页;
缺页异常信息:
   page fault at 0x00001000,解析错误码,得到触发原因: K/R [no page found].
已获取触发缺页的 mm_struct


--------------初始化完毕:交换分区--------------



--------------初始化开始:文件系统--------------



--------------初始化完毕:文件系统--------------



--------------初始化开始:时钟控制器--------------



--------------初始化完毕:时钟控制器--------------

user sh is running!!!
```

## todo

- make debug-nox 会卡在 Continuing, 待修复. 使用不带 nox 的版本可以调试.