# ucore Lab8 实验笔记

## IO 设备

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

## ide.c


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

### 参考资料

- *OSTEP*
- [Information Technology -
AT Attachment
with Packet Interface - 6](https://pdos.csail.mit.edu/6.828/2018/readings/hardware/ATA-d1410r3a.pdf)