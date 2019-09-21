#ifndef __KERN_FS_DEVS_DEV_H__
#define __KERN_FS_DEVS_DEV_H__

#include <defs.h>

struct inode;
struct iobuf;

/*
 * device 代表一个设备文件. Filesystem-namespace-accessible device.
 * 区别于普通文件 sfs_inode
 * 
 * 此支持对块设备（比如磁盘）、字符设备（比如键盘、串口）的表示.
 * d_io 可以用于读和写. 其中的参数iobuf 指明读写方向.
 */ 
struct device {
    size_t d_blocks;        // 设备占用的数据块的个数
    size_t d_blocksize;     // 每个数据块的大小
    // 以下是函数指针
    int (*d_open)(struct device *dev, uint32_t open_flags);         // 打开设备
    int (*d_close)(struct device *dev);                             // 关闭设备
    int (*d_io)(struct device *dev, struct iobuf *iob, bool write); // 读写设备
    int (*d_ioctl)(struct device *dev, int op, void *data);         // 用ioctl方式控制设备
};

#define dop_open(dev, open_flags)           ((dev)->d_open(dev, open_flags))
#define dop_close(dev)                      ((dev)->d_close(dev))
// 设备操作 IO 接口
#define dop_io(dev, iob, write)             ((dev)->d_io(dev, iob, write))
#define dop_ioctl(dev, op, data)            ((dev)->d_ioctl(dev, op, data))

void dev_init(void);
struct inode *dev_create_inode(void);

#endif /* !__KERN_FS_DEVS_DEV_H__ */

