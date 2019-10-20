# ucore Lab8 实验笔记

## 1 文件系统的职责是什么?

高效地/可靠地/安全地/方便地 管理用户的持久化数据.

## 2 从用户视角,文件系统至少要提供哪些抽象,即如何"方便"?

- 文件(file), 本质是线性的字节序列,是操作系统管理数据的单位
- 目录(directory), 在用户看来是对深层文件或目录的链接.

## 3 从磁盘的持久化视角,逻辑上怎样组织文件数据?

**inode 与 data block/dir block**

- 文件的数据就是纯字节序列;目录的数据是一个数组,每个元素是一个键值对,即当前目录下的 ,<`inode`,`name`>,目录的数据被称为**目录项**(directory entry 或 dentry).
- 采取索引法管理文件,即把文件或目录索引化,每个文件或目录的索引称为`inode`,放在元数据区, 具体数据放在数据区.
- 磁盘被划分为一个个定长块,即 `block`.在 `ucore` 的实现中 `1 block = 4KB`.
- `inode`维护着自己的类型:文件,目录,链接.
- `inode`记录了每个文件或目录占用了哪个 `block`.
- 一个文件可能占用多个 `block`.
- 每个目录项占用一个 `block`.
- 每个设备应有其根目录,即 root-dir.

我个人理解, `inode` 是文件系统管理数据的基本单位,每个 `inode` 都有自己独立的编号`inumber`.对于给定的`inumber`,应该能够找到对应的`inode`.在`ucore`的实现中,`inumber` 的值等于其所在的块号.


例如,

```
/
├── dir1
│      └── file2
└── file1
```

如上文件树的磁盘级表示如下图:

![](https://github.com/libinyl/ucore-study/blob/master/images/%E7%9B%AE%E5%BD%95%E6%A0%91%E7%9A%84%E7%A3%81%E7%9B%98%E7%BA%A7%E8%A1%A8%E7%A4%BA1.png?raw=true)

把位置稍作调整即可更清晰地看出内在关系:

![](https://github.com/libinyl/ucore-study/blob/master/images/%E7%9B%AE%E5%BD%95%E6%A0%91%E7%9A%84%E7%A3%81%E7%9B%98%E7%BA%A7%E8%A1%A8%E7%A4%BA2.png?raw=true)


**freemap**

block 是一种资源,其状态也需要持久化维护.通常使用`bitmap`来用最少的信息表示最多的状态.每个 `bit` 都表示了其对应的 block 的占用情况.在最开始每个 block 都是未占用的,故称其为`freemap`.

**superblock**

我们把磁盘的第一块作为描述磁盘文件系统的"超级块".超级块通常会包含一个魔数,文件系统名称,以及整体的块数,未用到的块数等.

综合起来如图:

![](https://github.com/libinyl/ucore-study/blob/master/images/SFS%20%E7%A3%81%E7%9B%98%E7%BB%84%E7%BB%87.png?raw=true)

## 5 对于以上磁盘级的概念,SFS 分别有哪些数据结构一一对应?

**超级块**.

```C
struct sfs_super {
    uint32_t magic;                                 /* magic number, = SFS_MAGIC */
    uint32_t blocks;                                /* # of blocks in fs */
    uint32_t unused_blocks;                         /* # of unused blocks in fs */
    char info[SFS_MAX_INFO_LEN + 1];                /* infomation for sfs  */
};
```

**bitmap**

```C
struct bitmap {
    uint32_t nbits;
    uint32_t nwords;
    WORD_TYPE *map;
};
```

**inode:**

```C
struct sfs_disk_inode {
    uint32_t size;                                  // 此 inode 文件大小(byte)
    uint16_t type;                                  // 文件类型, 如上 SFS_TYPE_xx
    uint16_t nlinks;                                // 此 inode 的hard链接数量
    uint32_t blocks;                                // 此 inode 的 block 数
    uint32_t direct[SFS_NDIRECT];                   // 直接数据块索引,有 12 个. 直接索引的数据页大小为 12 * 4k = 48k；
    uint32_t indirect;                              // 一级间接索引块索引值. 值为 0 时表示不使用一级索引块. 使用一级间接块索引时, ucore 支持的最大文件大小为,12个直接块,加上 4KB 可以表示的 1K 整型这么多个块,即12*4K+1024*4K=48k+4m 
};
```

**block**

`blkno * SFS_BLKSIZE`

**dir block**

``` C
// 即目录项
struct sfs_disk_entry {
    uint32_t ino;                                   /* inode number */
    char name[SFS_MAX_FNAME_LEN + 1];               /* 文件名 */
};
```

## 6 对于非磁盘文件,如设备文件,SFS 是怎样表示的?

设备文件对应的结构体如下:

```
struct device {
    size_t d_blocks;        // 设备占用的数据块的个数
    size_t d_blocksize;     // 每个数据块的大小
    // 以下是函数指针
    int (*d_open)(struct device *dev, uint32_t open_flags);         // 打开设备
    int (*d_close)(struct device *dev);                             // 关闭设备
    int (*d_io)(struct device *dev, struct iobuf *iob, bool write); // 读写设备
    int (*d_ioctl)(struct device *dev, int op, void *data);         // 用ioctl方式控制设备
};
```

事实上,`vfs`中的`inode`与`SFS`中的`device`和`sfs_disk_inode`构成了接口-实现关系.(反向继承?)

```
VFS:        inode
            ↗   ↖
SFS:    device  sfs_disk_inode
```



## 7 VFS 的设计思路是什么?

### 面向进程的文件

作为(几乎)与进程直接打交道的部分,虚拟文件系统需要(在内核态)提供足够的抽象,不管`inode`什么类型,都抽象为`file`:

```C
struct file {
    enum {
        // 文件状态: 不存在/已初始化/已打开/已关闭
        FD_NONE, FD_INIT, FD_OPENED, FD_CLOSED,
    } status;
    bool readable;
    bool writable;
    int fd;                 // 文件在 filemap 中的索引值
    off_t pos;              // 当前位置
    struct inode *node;     // 该文件对应的 inode 指针
    int open_count;         // 打开此文件的次数
};
```

**进程,文件描述符,文件,inode 四者之间的关系**

![](https://github.com/libinyl/ucore-study/blob/master/images/进程与文件数据结构.png?raw=true)

> 注: 参考`files_create`函数得知.

inode 就是文件系统对不同文件系统的文件资源的统一抽象,避免了进程直接访问文件系统.

### 统一文件系统

虚拟文件系统存在的意义之一就是统一文件系统,因此自然要向下给出文件系统的接口:

```C
struct fs {
    union {
        struct sfs_fs __sfs_info;                   
    } fs_info;                                     // filesystem-specific data 
    enum {
        fs_type_sfs_info,
    } fs_type;                                     // 文件系统类型
    int (*fs_sync)(struct fs *fs);                 // Flush all dirty buffers to disk 
    struct inode *(*fs_get_root)(struct fs *fs);   // Return root inode of filesystem.
    int (*fs_unmount)(struct fs *fs);              // Attempt unmount of filesystem.
    void (*fs_cleanup)(struct fs *fs);             // Cleanup of filesystem.???
};
```

对于 ucore 有唯一的实现:

```C
struct sfs_fs {
    struct sfs_super super;                         /* 超级块          on-disk superblock */
    struct device *dev;                             /* 被挂载的设备     device mounted on */
    struct bitmap *freemap; 
    ...
```

### 统一外设

虚拟文件系统维护了设备列表`vdev_list`:

![](https://github.com/libinyl/ucore-study/blob/master/images/vdev_list.png?raw=true)

对于每种设备,都默认存在其文件系统.如果没有则为 `NULL`.**文件系统初始化的过程,也就是这个链表的初始化过程.** 简而言之,所有的所谓初始化过程都是**数据结构就位**的过程.

## 7 文件系统初始化的过程是怎样的?

整体来讲,在初始化阶段,SFS作为一种特定的文件系统被上层的虚拟文件系统**挂载**.虚拟系统向下提供了挂载方法接口`mountfunc`,在虚拟文件系统加载期间,会查找当前机器上运行的外设,一旦找到外设就会去加载其中的文件系统.SFS 作为特定文件系统,需要实现相关函数.为了了解 SFS 的具体实现,需要首先了解 **VFS**的生命周期,也就是**SFS的编程环境**,自顶向下地了解文件系统的初始化流程.

在文件系统初始化之前,系统已经将 ide 磁盘设备初始化完毕.文件系统的初始化主干函数如下:

```C
void
fs_init(void) {
    vfs_init();
    dev_init();
    sfs_init();
}
```
考察具体函数,暂时忽略并发因素,文件系统初始化过程如下:

1. `VFS`通过`vfs_init`初始化一个设备列表`vdev_list`
2. 调用ide 驱动层初始化设备`stdin`, `stdout`,`disk0`.对于设备的初始化步骤很统一,就是创建一个设备类型的 inode 并通过 devlist 维护起来,并配置好每种设备对应的操作函数.
3. `SFS`初始化,主要是把 SFS 挂载到 VFS 的过程`sfs_do_mount`,即**新建`fs`对象并初始化每个字段,最终也添加到 `dev_list` 上的过程**.

## 8 打开文件最终改变了什么状态, 过程是怎样的?

**改变的状态** 当前进程新增一个 fd;此 fd 对应的 file 状态更新.

**打开过程** 一言以蔽之,打开文件的过程就是:给当前进程分配一个新的文件,将这个文件与参数中 path 对应的 inode 关联起来.

其中比较重要的过程就是,如何 getinode by path?这个过程主要由`vfs_lookup`负责.寻找思路是,调用每个文件系统各自实现的`fs_get_root`函数获取根 `inode`,以及sfs_lookup->sfs_lookup_once.


## 9 SFS创建文件的流程是怎样的?

当 `open`一个文件并指定了`O_CREATE`时最终会使这个文件存在.

考察`sfs_create_inode`:

## 10 SFS 与 VFS 是怎样衔接起来的?


![](https://github.com/libinyl/ucore-study/blob/master/images/VFS%20%E4%B8%8E%20SFS%20%E7%9A%84%E8%A1%94%E6%8E%A5.png?raw=true)

## io_buffer 是什么?

陈老师另外的一个实现有较为详细的注释:

https://github.com/chyyuu/ucore-x64-with-golang/blob/master/ucore/src/kern-ucore/fs/iobuf.h


![](https://github.com/libinyl/ucore-study/blob/master/images/iobuf%20%E4%B8%8E%20disk0_buffer.png?raw=true)




## 附 磁盘驱动层

一个理想的 IO 设备是什么样子的,向上层提供了什么接口?

![](https://github.com/libinyl/ucore-study/blob/master/images/Canonical%20Device.png?raw=true)

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

![](https://github.com/libinyl/ucore-study/blob/master/images/文件系统架构.png?raw=true)

![](https://github.com/libinyl/ucore-study/blob/master/images/文件系统设计图.png?raw=true)

### 文件系统+磁盘区块

![](https://github.com/libinyl/ucore-study/blob/master/images/磁盘区块.png?raw=true)

```
superblock
```

### 参考资料

- *OSTEP*
- [Information Technology -
AT Attachment
with Packet Interface - 6](https://pdos.csail.mit.edu/6.828/2018/readings/hardware/ATA-d1410r3a.pdf)