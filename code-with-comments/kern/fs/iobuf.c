#include <defs.h>
#include <string.h>
#include <iobuf.h>
#include <error.h>
#include <assert.h>
#include <kdebug.h>



/* 
 * iobuf_init - init io buffer struct.
 *                set up io_base to point to the buffer you want to transfer to, and set io_len to the length of buffer;
 *                initialize io_offset as desired;
 *                initialize io_resid to the total amount of data that can be transferred through this io.
 */
struct iobuf *
iobuf_init(struct iobuf *iob, void *base, size_t len, off_t offset) {
    LOG("iobuf_init:\n");
    iob->io_base = base;
    iob->io_offset = offset;
    iob->io_len = iob->io_resid = len;
    return iob;
}

/* iobuf_move - move data  (iob->io_base ---> data OR  data --> iob->io.base) in memory
 * @copiedp:  the size of data memcopied
 *
 * iobuf_move may be called repeatedly on the same io to transfer
 * additional data until the available buffer space the io refers to
 * is exhausted.
 * 
 * iobuffer 与 内存 data 之间的数据转移(复制)
 * 
 * 若m2b=0,则从内存 buffer到设备 data
 * 若 m2b=1,则从设备data到内存 buffer
 */
//iobuf_move(iob, disk0_buffer, alen, 1, &copied);
int
iobuf_move(struct iobuf *iob, void *data, size_t len, bool m2b, size_t *copiedp) {
    size_t alen;
    if ((alen = iob->io_resid) > len) {
        alen = len;
    }
    if (alen > 0) {
        void *src = iob->io_base, *dst = data;
        if (m2b) {//read.
            void *tmp = src;
            src = dst, dst = tmp;
        }
        memmove(dst, src, alen);
        iobuf_skip(iob, alen), len -= alen;
    }
    if (copiedp != NULL) {
        *copiedp = alen;
    }
    return (len == 0) ? 0 : -E_NO_MEM;
}

/*
 * iobuf_move_zeros - set io buffer zero
 * @copiedp:  the size of data memcopied
 */
int
iobuf_move_zeros(struct iobuf *iob, size_t len, size_t *copiedp) {
    size_t alen;
    if ((alen = iob->io_resid) > len) {
        alen = len;
    }
    if (alen > 0) {
        memset(iob->io_base, 0, alen);
        iobuf_skip(iob, alen), len -= alen;
    }
    if (copiedp != NULL) {
        *copiedp = alen;
    }
    return (len == 0) ? 0 : -E_NO_MEM;
}

/*
 * iobuf_skip - change the current position of io buffer
 * iobuf向高地址方向移动 n 个字节.
 */
void
iobuf_skip(struct iobuf *iob, size_t n) {
    assert(iob->io_resid >= n);
    iob->io_base += n, iob->io_offset += n, iob->io_resid -= n;
}

