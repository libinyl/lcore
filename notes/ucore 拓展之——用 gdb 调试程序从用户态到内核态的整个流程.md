## ucore 拓展之——用 gdb 调试程序从用户态到内核态再回到用户态的整个流程

说起来你可能不信, 为了调试用户态程序, 我第一反应是在 ucore 上装个 gdb! 当我还在故作深沉考察可行性的时候,stackoverflow 已经给了我一句劝告: gdb 比你想象的要更强大.

每个程序员, 每次解决一个未曾谋面的新问题后, 都会持续大概 5 分钟的自我陶醉, 我可真太牛逼了, 然后冷静分析下来发现, 其实这已经是二三十年前的技术了...但不管怎样, 都要有从不知道到知道的过程, 姑且记录下来.

以 `ls` 为例, 起码要向控制台输出当前文件列表, 即多次调用 `printf`; 而`printf`会进行系统调用`write`,再往后就是中断的生成, 派发和处理, 再往后io系统向`stdin`输出信息...如何跟踪?

**答案: 多用 `(gdb)file` 命令.**

1. 在`Makefile`中添加下列目标, 令 qemu 把输出重定向到串口的标准输出:

```makefile
dbg-u2k: $(UCOREIMG) $(SWAPIMG) $(SFSIMG)
	$(V)$(QEMU) -S -s -serial mon:stdio $(QEMUOPTS)
```

2. 清空 `.gdbinit`, 除非了解它的作用;
3. 定位到 ucore 项目根目录下, 执行 `make dbg-u2k`,  ucore 编译完毕并被 qemu 加载,停留在第一条指令之前;
4. 此时在另一终端执行`gdb bin/kernel`, 并`target remote localhost:1234`连接上 qemu, 观察到熟悉的提示信息.事实上到此为止与往常没有明显不同. 现在可以键入`c`,让内核自由运行,直到运行了shell.
5. 现在的状态是什么? 你可以枉顾 gdb 的存在,在 ucore 执行你的用户程序,如下图:

![](https://github.com/libinyl/lcore/blob/master/images/gdb%20%E8%B0%83%E8%AF%95%E7%94%A8%E6%88%B7%E7%A8%8B%E5%BA%8F.jpg?raw=1)

1. 但是此时 gdb 加载的是 kernel 的符号.现在只需`Ctrl C`中断 gdb,再键入`file disk0/ls`,即可加载 ls 的符号! 这是...热加载?
2. 此时键入`b main`,就可以像调试普通用户态程序一样调试 `ls` 了, 如下:

![](https://github.com/libinyl/lcore/blob/master/images/gdb%E8%B0%83%E8%AF%95%E7%94%A8%E6%88%B7%E7%A8%8B%E5%BA%8F%202.png?raw=1)

1. 同样,当在用户程序遇到系统调用时不用慌,跟进去,用 file切成内核即可!比如我们跟踪到了函数`lsstat`, 想跟入 `printf` 一探究竟:

![](https://github.com/libinyl/lcore/blob/master/images/gdb%E8%B0%83%E8%AF%95%E7%94%A8%E6%88%B7%E6%80%81%E7%A8%8B%E5%BA%8F3.png?raw=1)

最终我们一路跟踪到`user/libs/syscall.c`的`syscall`, 即将陷入中断.此时再执行`n`,就跟不到代码了.

![](https://github.com/libinyl/lcore/blob/master/images/gdb%E8%B0%83%E8%AF%95%E7%94%A8%E6%88%B7%E6%80%81%E7%A8%8B%E5%BA%8F%204.png?raw=1)

此时只需键入`file bin/kernel`,就进入了内核态的调试环境!再打个断点, 如`b trap`, 就接住这个系统调用了!

![](https://github.com/libinyl/lcore/blob/master/images/gdb%E8%B0%83%E8%AF%95%E7%94%A8%E6%88%B7%E6%80%81%E7%A8%8B%E5%BA%8F%205.png?raw=1)

我想,linux 内核调试起来大概也是如此的流程?


## 相关原理

**当在 gdb 命令窗口键入 Ctrl C, 发生了什么?**

首先我们可以看到 gdb 的输出: 

> Program received signal SIGINT, Interrupt.

事实是: **GDB帮用户拦截了致命的信号.**

键入`(gdb) info signals`可以查看 gdb 帮我们拦截的信号:

```
Signal        Stop	Print	Pass to program	Description

SIGHUP        Yes	Yes	Yes		Hangup
SIGINT        Yes	Yes	No		Interrupt
SIGQUIT       Yes	Yes	Yes		Quit
SIGILL        Yes	Yes	Yes		Illegal instruction
SIGTRAP       Yes	Yes	No		Trace/breakpoint trap
SIGABRT       Yes	Yes	Yes		Aborted
...
```

可以观察到, `Ctrl C` 对应的 *SIGINT* 信号被 gdb 所捕获并输出, 并没有真正传递给进程.所以我们可以利用这个间隙来调整 gdb 的选项,就像刚才一样加载其他文件的符号,有了从用户态到内核态来去自由的机会.不过这也许只是手段之一吧,以后有了其他发现再回来更新.


## 更多资料

- [Debugging with GDB, 5.4 Signals](https://sourceware.org/gdb/onlinedocs/gdb/Signals.html) 
- [Stack Overflow: Debugging user-code on xv6 with gdb](https://stackoverflow.com/questions/10534798/debugging-user-code-on-xv6-with-gdb)