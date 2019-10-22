## ucore 拓展之——日志输出及控制，调试逻辑优化

说实话我比较怀疑 ucore 的作者移除了原有的日志模块，否则靠一条条 `cprintf` 来调试真的太难受了。.. 不过也有可能是故意留给学生来处理的？不管怎样，在我尝试增加了日志模块后（尽管非常简陋，约 100 行代码），及时地输出一些调试信息对我理解整个系统起了很大帮助。这里简单介绍下，也欢迎大家试用。

另外在文末给出一行命令，用于优化 gdb 的调试效果。

日志控制代码位于`kdebug.[hc]`, 实现了 1) *基本的日志输出；* 2) *模块级开关控制；* 3) *区域开关控制*

**基本使用**

使用方法：只需`#incdlue <kdebug.h>`, 然后就可以像使用`cprintf`一样使用`LOG`来打印调试信息了。另附一个加了一个 tab 的 `LOG_TAB`。

**模块级开关控制**

在 `kdebug.h` 中可见形如`#define IS_LOG_GLOBAL_ON 1`的宏，只需分别配置为 1 或 0 即可开关对应模块的日志输出。这里把`kern`下的每个文件夹看做一个模块。

**区域级开关控制**

有时想临时屏蔽某个函数或者某个文件的日志输出，这时只需用宏`_NO_LOG_START`和`_NO_LOG_END`包裹对应的代码区域就可以了。注意这里加了一个小检测，**你必须成对使用它们，否则不会编译通过。**

代码链接见下，欢迎（酌情）使用~

- [kdebug.h](https://github.com/libinyl/lcore/blob/master/code-with-comments/kern/debug/kdebug.h)
- [kdebug.c](https://github.com/libinyl/lcore/blob/master/code-with-comments/kern/debug/kdebug.c)

-------

## 关于 gdb 的调试优化

源码中给出的调试方法`make debug[-nox]`把 QEMU 作为后台 job 启动，并把输出重定向到标准输出，导致在 tui 模式下输出中文会乱码；此外还有其他奇奇怪怪的乱码问题。如果有小伙伴觉得不太爽的话，可以在 makefile 中加一条：

```
debug-gdb: $(UCOREIMG) $(SWAPIMG) $(SFSIMG)
	$(V)$(TERMINAL) -c "$(GDB) -q -x tools/gdbinit"
```

这样，打开一个 terminal, 键入`make dbg4ec`, 再打开一个 terminal, 键入`make debug-gdb`即可把日志信息输出到第一个终端，而在第二个终端用 tui 轻松调试了。