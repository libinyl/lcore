操作系统是一个基于事件的软件，这里操作系统需要响应的事件包括三类：外设中断、CPU执行异常（比如访存错误）、陷入（系统调用）

## 机制: 系统调用

## 机制: 执行二进制文件

函数原型:

```
do_execve(const char *name, size_t len, unsigned char *binary, size_t size)
```

1. 合法性检查
2. 设置页表
3. 