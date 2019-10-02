# ucore Lab4 实验笔记

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