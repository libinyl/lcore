## ucore 分析之——文件系统的面向对象实现（一）: 手写虚函数表

这篇文章的灵感来自 @冯东 大佬的回答：

C++中有哪些设计精良的部分（精华），还有哪些是不值得花费很多时间探究的知识点？ - 冯东的回答 - 知乎
https://www.zhihu.com/question/32271535/answer/55386211

看了回答中推荐的文章之后，我才稍微更加理解了用 C 写面向对象代码的方式。这种编码风格强烈地体现在文件系统的实现--我们需要处理太多类型的设备，太多不同类型的文件系统，最后却统统抽象出一个"文件", 人类真的是懒惰到家了，真棒👍!

## 设备的统一

系统需要与数不胜数的设备打交道。有的以字符为单位读写，像键盘，串口，调制解调器，打印机之类，我们称之为**字符设备**, 它们往往可以顺序读写，但通常没法随机读写；有的以固定大小的块为单位读写，像磁盘，U 盘，SD 卡等等，每个块都有编址，所以可以方便地随机读写，在任意位置读写一定长度的数据，我们称之为**块设备**; 还有更复杂的**网络设备**, 操作的单位是网络报文，系统通过网络接口支持各层级的协议。

历史上对不同类型的设备可能需要分别处理，意味着开发者面临着多种 API, 一些早期的系统甚至需要用不同的命令处理位于不同软盘的文件。而 Unix 把一些进程间通信的概念（管道，套接字，共享内存等）也集成到文件的 api 管理范畴，open 一个文件，进程就多维护一个文件描述符，作为对二进制流的 IO 接口。从 Unix 之后，文件不再仅仅是指持久化存储的数据，更是涵盖了多种具体类型的，强大而简单的抽象。

![](https://github.com/libinyl/lcore/blob/master/images/%E6%96%87%E4%BB%B6%E7%B3%BB%E7%BB%9F%E5%88%86%E6%9E%90%203.png?raw=1)

## 文件系统的统一

想知道你的 linux 系统支持哪些文件系统，只需执行 `cat /proc/filesystems`, 我安装的 ubuntu 显示有 33 种--是的，需要再向上抽象一层，否则操作系统真的要分别管理它们，这太累了。linux 确实做了抽象，可以参考 [Overview of the Linux Virtual File System](https://www.kernel.org/doc/Documentation/filesystems/vfs.txt) 了解细节。总之，为了支持各种类型的文件系统，以及为了提供更丰富的功能，一个 VFS（虚拟文件系统）势在必得。

![](https://github.com/libinyl/lcore/blob/master/images/%E6%96%87%E4%BB%B6%E7%B3%BB%E7%BB%9F%E5%88%86%E6%9E%90%202.png?raw=1)


## 文件的组织

人类渴望简单，简单就是符合直觉。(待第二篇补充...)

## ucore 的文件系统

(以下内容假设读者了解 ucore 源码)

我们知道文件描述符于进程而言的重要性，也熟悉用法：`open`, `read`,`llseek`, `write`, `close`, 一气呵成。这些都是在与`VFS`打交道, 它帮我们分配`fd`, 根据路径名找到对应的`inode`, 最后返回 `fd`. 在这个过程中, `VFS` 不需要了解下层具体的文件系统如何实现`read`, `write`, 只要调用就好; 也不需要关心是什么文件, 只要解析(resolve)到具体的`inode`就好:

![](https://github.com/libinyl/lcore/blob/master/images/%E6%96%87%E4%BB%B6%E7%B3%BB%E7%BB%9F%E5%88%86%E6%9E%90%201.png?raw=1)


**VFS**

从(ucore)操作系统的视角来看, VFS 的实现基于以下概念:

- 最抽象的管理单位是虚拟设备`vdev`, 用链表类结构(`vdev_list`)维护;
- 每个 `vdev` 都有一个字符串类型的名字用于标识(identify);
- 每个具体设备都抽象为`inode`,被`vdev`维护;
- `vdev`分为可挂载(mountable)和不可挂载的;
- 每个可挂载的`vdev`都可以被其对应的文件系统识别并挂载, 用 `fs`字段维护.

其实这就是`vfs_dev_t`结构:

```C
typedef struct {
    const char *devname;	// 设备名称
    struct inode *devnode;	// 代表设备
    struct fs *fs;			// 可挂载设备挂载后的文件系统结构
    bool mountable;			// 是否可挂载
    list_entry_t vdev_link;	// 链表节点
} vfs_dev_t;
```

对于不可挂载的设备而言, `fs` 会被初始化为 `NULL`.


**inode**

我感觉 inode 的概念比较复杂, 而复杂的原因是其在不同语境下似乎有不同的语义. 由于之前写 `class` 用的是 C++ 或 Java, 刚开始接触 inode 的时候真心难以理解. 这时开头那篇文章才把我指点清楚, inode 的复杂含义背后是面向对象的设计思想.


## 插播: C 与 C++ 在面向对象方面的对比, 以 inode 为例

**struct 与 class 的区别**

如果是仅在 C++语境下讨论它们的区别, 那就是几乎没有区别, 仅仅是成员默认访问权限的区别;而如果是 C 的 `struct` 与 C++ 的 `class` 的对比, 思维上会有很大的差异: C 的`struct` 忠实地记录了实际的内存结构, 我们在编程时真的是 **面向内存编程**, 为了实现继承和多态, 往往要付出成倍的代价; 而 C++ 的 `class` 则可以提供足够抽象的描述, 隐去很多繁杂的细节, 但也有考虑过多的负担.

以 `inode` 为例. 如果用 C++ 改写代码,最终形成的类图是这样的:

![](https://github.com/libinyl/lcore/blob/master/images/%E6%96%87%E4%BB%B6%E7%B3%BB%E7%BB%9F%E7%B1%BB%E5%9B%BEfs.png?raw=1)

![](https://github.com/libinyl/lcore/blob/master/images/%E6%96%87%E4%BB%B6%E7%B3%BB%E7%BB%9F%E7%B1%BB%E5%9B%BEinode.png?raw=1)

代码见链接(只突出继承关系, 无具体实现):

https://github.com/libinyl/lcore/tree/master/blogcode/%E6%96%87%E4%BB%B6%E7%B3%BB%E7%BB%9F%E5%88%86%E6%9E%90

inode 有以下含义:

..未完待续...





## 参考资料

- [ph7spot: in-unix-everything-is-a-file](https://ph7spot.com/musings/in-unix-everything-is-a-file)
- [yarchive: everything_is_file](https://yarchive.net/comp/linux/everything_is_file.html)
- [Wikipedia: Computer_file](https://en.wikipedia.org/wiki/Computer_file)
- [Wikipedia: Everything_is_a_file](https://en.wikipedia.org/wiki/Everything_is_a_file)
- [Wikipedia: File_systems_supported_by_the_Linux_kernel](https://en.wikipedia.org/wiki/Category:File_systems_supported_by_the_Linux_kernel)