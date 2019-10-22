## Quickstart

**编译并运行**

- `make qemu-nox`

**退出系统**

- `Ctrl + A,X`.

**gdb 调试**

调整 makefile,将 gnome 换成 zsh,选项变为 c,将 i386-elf-gdb 换成gdb.

建议调试方法:
1. 启动 1 个窗口,执行 make dbg4ec
2. 启动另一个窗口,执行 make debug-gdb, 即可分开gdb 输出和 qemu 输出,避免花屏,输出更加清晰

## CLion 环境搭建

参考 jetbrains [官方文档](https://www.jetbrains.com/help/clion/managing-makefile-projects.html)

