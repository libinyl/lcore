## lcore - ucore with log & comments

*inspired by ucore*

此 OS 纯个人娱乐 & 笔记向, 内含大量学习过程做的注释和日志, 部分参数相较 ucore 原 repo 修改, 可能有失准确, 仅供参考.

## 一些可能有用的图示:

*进程数据结构*
![](https://github.com/libinyl/ucore-study/blob/master/images/%E8%BF%9B%E7%A8%8B%E6%95%B0%E6%8D%AE%E7%BB%93%E6%9E%84%201.png?raw=1)


*VFS 与 SFS*

![](https://github.com/libinyl/ucore-study/blob/master/images/VFS%20%E4%B8%8E%20SFS%20%E7%9A%84%E8%A1%94%E6%8E%A5.png?raw=1)

*文件系统示意图*
![](https://github.com/libinyl/ucore-study/blob/master/images/%E7%9B%AE%E5%BD%95%E6%A0%91%E7%9A%84%E7%A3%81%E7%9B%98%E7%BA%A7%E8%A1%A8%E7%A4%BA2.png?raw=1)

## 部分日志输出

```shell
----------文件系统生成程序----------
参数: bin/sfs.img, disk0

----------bin/sfs.img (disk0) 成功.----------

控制台输出初始化完毕,获得输出调试信息能力

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
	etext               : 	0xc01167c6 = 3072 M + 1113K
	edata               : 	0xc015d000 = 3072 M + 1396K
	end(.bss 结束))   : 	0xc0161324 = 3072 M + 1412K
	内核文件预计占用最大内存	:	4MB
	内核文件实际占用内存		:	389 KB
	内核可管理物理内存大小上限	:	0x38000000 Byte = 896 MB
	内核虚拟地址区间(B)		:	[0xc0000000 , 0xf8000000)
	内核虚拟地址区间(M)		:	[3072 M, 3968 M)
	内存分页大小			:	4096 B



--------------初始化开始:内存管理模块--------------

目标: 建立完整的虚拟内存机制.
	已维护内核页表物理地址;当前页表只临时维护了 KERNBASE 起的 4M 映射,页表内容:

一级页表内容:

	索引	二级页表物理基址	存在位	读写性	特权级
	768	0x0015c000		1	rw	u

	物理内存管理器实例- default_pmm_manager 初始化完毕.


--------------初始化开始:内存分页记账--------------

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
	内核文件地址边界 end: 0xc0161324
	表格起始地址 pages = ROUNDUP(end) = 0xc0162000 = 3073 M
	pages 表格自身内核虚拟地址区间 [pages,pages*n): [0xc0162000, 0xc0281b80)B,已被设置为不可交换.
	pages 表格结束于物�eemem :0x00281b80B ≈ 2M. 也是后序可用内存的起始地址.

	考察管理区间, 将空闲区域标记为可用.
	考察区间: [00000000,0009fc00):		此区间不可用, 原因: 边界非法.
	考察区间: [0009fc00,000a0000):		此区间不可用, 原因: BIOS 认定非可用内存.
	考察区间: [000f0000,00100000):		此区间不可用, 原因: BIOS 认定非可用内存.
	考察区间: [00100000,07fe0000):		此区间可用, 大小为 0x07d5e000 B = 128376 KB = 125 MB = 32094 page.
		default_init_memmap:
			已将一块连续地址空间加入 freelist,起始: 0xc0167a48, page 数:32094.
			当前空闲 page 数:32094
	考察区间: [07fe0000,08000000):		此区间不可用, 原因: BIOS 认定非可用内存.
	考察区间: [fffc0000,100000000):		此区间不可用, 原因: BIOS 认定非可用内存.


--------------初始化完毕: 内存分页记账--------------

	default_check()     : succeed!
	check_alloc_page()  : succeed!
	check_pgdir()       : succeed!

开始建立一级页表自映射: [VPT, VPT + 4MB) => [PADDR(boot_pgdir), PADDR(boot_pgdir) + 4MB).

自映射完毕.

一级页表内容:

	索引	二级页表物理基址	存在位	读写性	特权级
	1003	0x0015b000		1	rw	u
	768	0x0015c000		1	rw	u



--------------开始: 内核区域映射--------------

	映射区间[0xc0000000,0xc0000000 + 0x38000000 ) => [0x00000000, 0x00000000 + 0x38000000 )
	区间长度 = 896 M
	校准后映射区间: [0xc0000000, 0x00000000), 页数:229376
	校准后映射区间: [0xc0000000,0xc0000000 + 0x38000000 ) => [0x00000000, 0x00000000 + 0x38000000 )
	映射完毕, 直接按照可管理内存上限映射. 虚存对一级页表比例: [KERNBASE, KERNBASE + KMEMSIZE) <=> [768, 896) <=> [3/4, 7/8)


--------------完毕: 内核区域映射--------------


一级页表内容:

	索引	二级页表物理基址	存在位	读写性	特权级
	1003	0x0015b000		1	rw	u
	991	0x00360000		1	rw	u
	990	0x0035f000		1	rw	u
	989	0x0035e000		1	rw	u
	988	0x0035d000		1	rw	u
	987	0x0035c000		1	rw	u
	......
	768	0x0015c000		1	rw	u



--------------初始化开始: 全局段描述表&TSS--------------

	1. 设置内存中的 ts 结构 ts.ts_esp0 = bootstacktop
	2. 设置内存中的 ts 结构 ts.ts_ss0 = KERNEL_DS
	3. 设置 GDT 表中的 TSS 一项, 维护内存 ts 地址
	4. 加载 TSS 段选择子到 TR 寄存器
	5. 更新所有段寄存器的段选择子值为 0,即平铺结构


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
	0x0	n	GD_KTEXT	0xc0103480	0
	...	nt...		...		0
	0x80	y	GD_KTEXT	0xc01038f0	3
	...	nt...		...		0
	0xff	n	GD_KTEXT	0xc0103ee4	0


--------------初始化完毕:中断向量表--------------



--------------测试开始:虚拟内存管理模块(vmm)--------------

   开始测试 vma结构.
   测试点: 是否正确把 vma插入到 mm,是否有重叠,是否能从 mm 找到某个地址所在的 vma.
   当前空闲 page 数:31871
初始化了一个 mm_struct.
   从 5 到 50, 以及从 55 到 500,每隔 5 个字节创建一个 vma, 长度是 2;全部插入到 mm 链表中.
   插入结束,mm 所维护的 vma 数量为100
check_vma_struct() succeeded!


--------------开始测试: page fault--------------

   当前页表状态:

	--- 页表信息 begin ---

	PDE(0e0) c0000000-f8000000 38000000 urw
	  |-- PTE(38000) c0000000-f8000000 38000000 -rw
	PDE(001) fac00000-fb000000 00400000 -rw
	  |-- PTE(000e0) faf00000-fafe0000 000e0000 urw
	  |-- PTE(00001) fafeb000-fafec000 00001000 -rw

	--- 页表信息 end ---

初始可用页数: 31870.
初始化了一个 mm_struct.
此mm_struct的一级页表地址是: 0xc015b000.
   创建一个页目录项对应大小的 vma(1024*4KB=4MB), 物理地址区间是[0,4M), flag=write,并插入到 mm 中.
   开始对此区域进行写入.
预计缺页情况:内核检测到缺页异常中断.
pgfault_handler: 开始处理缺页;
缺页异常信息:
   page fault at 0x00000100,解析错误码,得到触发原因: K/W [no page found].
已获取触发缺页的 mm_struct
do_pgfault:  分析并处理缺页.cpu 已将触发异常的地址置于 cr2 寄存器,值为: x00000100.
此异常错误码: 2
已通过find_vma获取此地址在 mm_struct 中对应的 vma.
开始恢复缺页异常: 建立对应虚拟地址的页表即可.
已得到此地址的页表项
   写入完毕,即将移除页表项
   已移除addr所属内存页的页表项


--------------测试通过: page fault--------------

check_vmm() succeeded.


--------------测试结束:虚拟内存管理模块(vmm)--------------



--------------初始化开始:进程调度器--------------

	初始化队列: timer_list
	初始化队列: run_queue
		数值 max_time_slice = 5
	sched class: stride_scheduler


--------------初始化完毕:进程调度器--------------



--------------初始化开始: 内核线程--------------

proc_init:
	初始化队列: proc_list
	内核初始线程描述: idleproc
	pid: 0
	name: idle
	state: PROC_RUNNABLE
	kstack: 0xc0158000
	当前总进程数: 1
	current 进程pid: idle

kernel_thread begin:
	为 do_fork 准备 trapframe:
	代码段选择子: 0x00000008
	数据段选择子: 0x00000010
	该内核线程起始地址初始化: 0xc010f1d0
	入口点初始化 :0xc010cfb8
trapframe准备完毕,即将进入 do_fork:

do_fork begin:
	1. 分配 PCB
	2. 指定父进程: current
	3. 设置内核栈空间: 2 page
setup_kstack: kstack = new page(2)	4. 设置 file_struct.
copy_fs begin:
	是否共享父进程的文件控制块? 否,分配新 fs.
	此控制块引用计数值已更新: 1
copy_fs end
	4. 设置的 mm_struct
	copy_mm:
		创建选项:
		是否共享父进程 mm? 否, 进程是内核线程,共享一个 mm
.	5. 设置 trapframe 结构
	6. 将新进程维护到: proc_list.
	7. 唤醒新进程,进程创建结束.

do_fork end
kernel_thread返回 pid:1


--------------初始化开始:ide 磁盘控制器--------------



--------------初始化开始:ide 磁盘控制器--------------



--------------初始化开始:交换分区--------------

初始化了一个 mm_struct.
内核检测到缺页异常中断.
pgfault_handler: 开始处理缺页;
缺页异常信息:
   page fault at 0x00001000,解析错误码,得到触发原因: K/W [no page found].
已获取触发缺页的 mm_struct
do_pgfault:  分析并处理缺页.cpu 已将触发异常的地址置于 cr2 寄存器,值为: x00001000.
此异常错误码: 2
已通过find_vma获取此地址在 mm_struct 中对应的 vma.
开始恢复缺页异常: 建立对应虚拟地址的页表即可.
已得到此地址的页表项
内核检测到缺页异常中断.
pgfault_handler: 开始处理缺页;
缺页异常信息:
   page fault at 0x00002000,解析错误码,得到触发原因: K/W [no page found].
已获取触发缺页的 mm_struct
do_pgfault:  分析并处理缺页.cpu 已将触发异常的地址置于 cr2 寄存器,值为: x00002000.
此异常错误码: 2
已通过find_vma获取此地址在 mm_struct 中对应的 vma.
开始恢复缺页异常: 建立对应虚拟地址的页表即可.
已得到此地址的页表项
内核检测到缺页异常中断.
pgfault_handler: 开始处理缺页;
缺页异常信息:
   page fault at 0x00003000,解析错误码,得到触发原因: K/W [no page found].
已获取触发缺页的 mm_struct
...


--------------初始化完毕:交换分区--------------



--------------初始化开始:文件系统--------------



--------------初始化完毕:文件系统--------------



--------------初始化开始:时钟控制器--------------



--------------初始化完毕:时钟控制器--------------

进入 cpu_idle 函数

kernel_thread begin:
	为 do_fork 准备 trapframe:
	代码段选择子: 0x00000008
	数据段选择子: 0x00000010
	该内核线程起始地址初始化: 0xc010f15f
	入口点初始化 :0xc010cfb8
trapframe准备完毕,即将进入 do_fork:

do_fork begin:
	1. 分配 PCB
	2. 指定父进程: current
	3. 设置内核栈空间: 2 page
setup_kstack: kstack = new page(2)	4. 设置 file_struct.
copy_fs begin:
	是否共享父进程的文件控制块? 否,分配新 fs.
	此控制块引用计数值已更新: 1
copy_fs end
	4. 设置的 mm_struct
	copy_mm:
		创建选项:
		是否共享父进程 mm? 否, 进程是内核线程,共享一个 mm
.	5. 设置 trapframe 结构
	6. 将新进程维护到: proc_list.
	7. 唤醒新进程,进程创建结束.

do_fork end

kernel_thread begin:
	为 do_fork 准备 trapframe:
	代码段选择子: 0x00000008
	数据段选择子: 0x00000010
	该内核线程起始地址初始化: 0xc010a6ab
	入口点初始化 :0xc010cfb8
trapframe准备完毕,即将进入 do_fork:

do_fork begin:
	1. 分配 PCB
	2. 指定父进程: current
	3. 设置内核栈空间: 2 page
setup_kstack: kstack = new page(2)	4. 设置 file_struct.
copy_fs begin:
	是否共享父进程的文件控制块? 否,分配新 fs.
	此控制块引用计数值已更新: 1
copy_fs end
	4. 设置的 mm_struct
	copy_mm:
		创建选项:
		是否共享父进程 mm? 否, 进程是内核线程,共享一个 mm
.	5. 设置 trapframe 结构
	6. 将新进程维护到: proc_list.
	7. 唤醒新进程,进程创建结束.

do_fork end

kernel_thread begin:
	为 do_fork 准备 trapframe:
	代码段选择子: 0x00000008
	数据段选择子: 0x00000010
	该内核线程起始地址初始化: 0xc010a6ab
	入口点初始化 :0xc010cfb8
trapframe准备完毕,即将进入 do_fork:

do_fork begin:
	1. 分配 PCB
	2. 指定父进程: current
	3. 设置内核栈空间: 2 page
setup_kstack: kstack = new page(2)	4. 设置 file_struct.
copy_fs begin:
	是否共享父进程的文件控制块? 否,分配新 fs.
	此控制块引用计数值已更新: 1
copy_fs end
	4. 设置的 mm_struct
	copy_mm:
		创建选项:
		是否共享父进程 mm? 否, 进程是内核线程,共享一个 mm
.	5. 设置 trapframe 结构
	6. 将新进程维护到: proc_list.
	7. 唤醒新进程,进程创建结束.

do_fork end


...
do_sleep:
当前进程信息:do_sleep:
当前进程信息:do_sleep:
当前进程信息:do_sleep:
当前进程信息:do_sleep:
当前进程信息:do_sleep:
当前进程信息:do_sleep:
当前进程信息:do_sleep:
当前进程信息:kernel_execve: pid = 2, name = "sh".
初始化了一个 mm_struct.
user sh is running!!!
```

## 可配置项

配置项 | 路径
----|---
日志控制 | `kern/debug/kdebug.h`
调度策略配置 | `kern/schedule/sched.h`

## todo

- make debug-nox 会卡在 Continuing, 待修复. 使用不带 nox 的版本可以调试.

## 知乎专栏

https://zhuanlan.zhihu.com/ucore