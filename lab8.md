# ucore Lab8 实验笔记

## 磁盘驱动层

一个理想的 IO 设备是什么样子的,向上层提供了什么接口?

![](/images/Canonical&#32;Device.png)

一个简化的设备接口提供 3 个寄存器:

- 状态:status
- 命令:command
- 数据:data


对于磁盘而言,其状态具体可以参考 *Information Technology -
AT Attachment
with Packet Interface - 6* 7.15.6节.

通常有

简称 |   含义
---|---
BSY | Busy
DRDY | Dirty
DF | Device Fault
\#  | Command dependent
DRQ | Data request
ERR | Error

由于操作系统不知道磁盘属于什么状态,只能**轮询(polling)**设备.

如何解决轮询引入的性能问题(阻塞)?**中断**.使当前的进程进入阻塞状态,让 cpu 执行其他进程.而磁盘持续它自己的动作.

此处可参考 *OSTEP* 第 36.3,讲得简洁清晰.

### ide.c


```
// 从ideno号的 ide 设备的第 secno 个扇区开始,
// 读取 nsecs 个扇区
// 到缓冲区指针 dst 开始的地址处
int
ide_read_secs(unsigned short ideno, uint32_t secno, void *dst, size_t nsecs)
```

```
// 与读取类似,只是变成写入到磁盘
int
ide_write_secs(unsigned short ideno, uint32_t secno, const void *src, size_t nsecs)
```

### 文件系统层级

每个目录有对应的层级:

```C
// 通用文件系统访问接口层
//      提供从用户空间到文件系统的标准访问接口,
//      让应用程序通过一个简单的接口获得 ucore 内核的文件系统服务.
usr/libs/file.c
usr/libs/syscall.c
kern/syscall/syscall.c

// 文件系统抽象层 VFS
//      向上提供一致的系统调用和其他内核模块
//      向下提供同样的抽象函数指针列表和数据结构屏蔽不同文件系统的实现细节
kern/fs/sysfile.c
kern/fs/file.c
kern/fs/vfs/inode.h

// Simple FS 文件系统实现层
//      一个基于索引方式的简单文件系统实例.
//      向下访问外设接口
kern/fs/sfs/sfs_inode.c
kern/fs/sfs/sfs_io.c

// 文件系统I/O外设备接口层
//      向上提供外设访问接口屏蔽硬件细节
kern/fs/devs/dev.h
kern/fs/devs/dev_disk0.c

// 驱动层
kern/driver/ide.c
```

详细如图:

![](/images/文件系统架构.png)

![](/images/文件系统设计图.png)

### 文件系统+磁盘区块

![](/磁盘区块.png)

```
superblock
```

### 不变量

设备: 磁盘设备,标准输入(键盘),标准输出(console)

https://qemu.weilnetz.de/doc/qemu-doc.html

QEMUOPTS = -hda $(UCOREIMG) 
-drive file=$(SWAPIMG),media=disk,cache=writeback 
-drive file=$(SFSIMG),media=disk,cache=writeback 

https://wiki.gentoo.org/wiki/QEMU/Options#Hard_drive
https://github.com/qemu/qemu/blob/master/docs/qdev-device-use.txt

### 什么是初始化

1. 元数据结构初始化,比如链表头初始化
2. 函数指针就位,指向具体的函数
3. 数据结构就位,可能涉及内存分配


### inode 管理器 inode.[hc]

### 子知识

- 假设每个设备有其自己的文件系统.
- 文件系统有两类: boot filesystem,和其他系统启动后其他设备上的文件系统.
- inode 是资源单位
- 磁盘的寻址单位是扇区编号

### 进程读文件

- 获取字节所在的数据块

### 参考资料

- *OSTEP*
- [Information Technology -
AT Attachment
with Packet Interface - 6](https://pdos.csail.mit.edu/6.828/2018/readings/hardware/ATA-d1410r3a.pdf)