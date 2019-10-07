# ucore Lab4 实验笔记

## 一个进程,何时通知内核让出资源(调用 schedule)?

当该进程没有其他事情可做的时候.

比如系统调用 do_exit,把自己的资源尽可能清理干净后,剩下无法完全清理的资源,只能等待父进程回收,这时应当让出计算资源.

再如do_wait,等待子进程退出的时候,确实没有其他事情可做,也应当让出资源.

另外就是用户进程主动调用`sleep`的情况.

## 何时进入运行队列?

- wakeup_proc
- 6

## 调度器的基本操作

- 在就绪进程集合中选择
- 进入就绪进程集合
- 离开就绪进程集合

## 问题: 何时调用 schedule(),何时仅仅添加到就绪队列enqueue?

- 前者的含义是"立即运行下一个就绪进程",一些特殊情况往往必须要调用,比如`exit`,`wait`,`sleep`等,如果仅仅是添加到队列中,当前进程会白白浪费时间.
- 后者意味着可以有所延迟.

## 内核抢占点

1. 用户调度: 

- `fork`
- `exit`

## 等待的原因有哪些?

- 
- 等待子进程
- 等待内核信号量
- 等待 timer
- 等待键盘输入

```
#define WT_INTERRUPTED               0x80000000                    // the wait state could be interrupted
#define WT_CHILD                    (0x00000001 | WT_INTERRUPTED)  // wait child process
#define WT_KSEM                      0x00000100                    // wait kernel semaphore
#define WT_TIMER                    (0x00000002 | WT_INTERRUPTED)  // wait timer
#define WT_KBD                      (0x00000004 | WT_INTERRUPTED)  // wait the input of keyboard
```

## wakup_proc、 shedule、 run_timer_list 之间的关系是什么?

好像只有时钟中断才是最"不可抵抗"的因素,每次时钟中断都会把控制权交给内核,准确的说是交给了时钟中断处理函数`run_timer_list`.

这里有思维上的转变.当中断的数据结构和控制器初始化完毕之后,我们的主视角其实不再是"内核进程","用户进程",而是从时钟中断处理函数出发的时间线.

## timer_list是在何时初始化的?

sched_init