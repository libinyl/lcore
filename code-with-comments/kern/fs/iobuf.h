#ifndef __KERN_FS_IOBUF_H__
#define __KERN_FS_IOBUF_H__

#include <defs.h>

// 参考陈老师另一个实现的注释:  https://github.com/chyyuu/ucore-x64-with-golang/blob/master/ucore/src/kern-ucore/fs/iobuf.h


/**
 * 此 iobuf是模仿自 BSD 的 uio 简化而来,参考 https://www.freebsd.org/cgi/man.cgi?query=uio&sektion=9&manpath=freebsd-release-ports
 * 
 * 在 ucore 中,用于用户地址与内核地址之间的数据复制.
 * 此结构用一些字段"定义"了内存中的一段目标地址.
 * 可以理解为一个"消息",描述了要传输数据使用的信息
 * 
 * 
 * 使用模式: 
 * 
 * 1. 设定目标设备的地址 io_offset
 * 2. 设定内存地址 io_base,内存 buffer 长度 io_len
 * 3. 设定需要传输的数据总量,一般与 io_len 相等
 * 
 * 
 * 
 * 
 */

/*
 * iobuf is a buffer Rd/Wr status record
 * 
 * io buffer : 读写状态记录器
 * 
 * io_base  : 内存目标地址
 * io_len   : buffer 长度
 * io_resid : 还剩多少没有传输完毕
 * io_offset: 在目标对象(设备)中的偏移量
 */
struct iobuf {
    void *io_base;     // the base addr of buffer (used for Rd/Wr)
    off_t io_offset;   // current Rd/Wr position in buffer, will have been incremented by the amount transferred
    size_t io_len;     // the length of buffer  (used for Rd/Wr)
    size_t io_resid;   // current resident length need to Rd/Wr, will have been decremented by the amount transferred.
};

// "use" 是指已经拷贝了的数量.
#define iobuf_used(iob)                         ((size_t)((iob)->io_len - (iob)->io_resid))

struct iobuf *iobuf_init(struct iobuf *iob, void *base, size_t len, off_t offset);
int iobuf_move(struct iobuf *iob, void *data, size_t len, bool m2b, size_t *copiedp);
int iobuf_move_zeros(struct iobuf *iob, size_t len, size_t *copiedp);
void iobuf_skip(struct iobuf *iob, size_t n);

#endif /* !__KERN_FS_IOBUF_H__ */

