#include <defs.h>
#include <mmu.h>
#include <sem.h>
#include <ide.h>
#include <inode.h>
#include <kmalloc.h>
#include <dev.h>
#include <vfs.h>
#include <iobuf.h>
#include <error.h>
#include <assert.h>
#include <kdebug.h>

#define DISK0_BLKSIZE                   PGSIZE                      // 每个块的byte数
#define DISK0_BUFSIZE                   (4 * DISK0_BLKSIZE)         // 磁盘模块缓冲区byte 数
#define DISK0_BLK_NSECT                 (DISK0_BLKSIZE / SECTSIZE)  // 每个块占的扇区数(=8)

static char *disk0_buffer;      // 磁盘IO的缓存,4 个 block 大小 = DISK0_BUFSIZE
static semaphore_t disk0_sem;

static void
lock_disk0(void) {
    down(&(disk0_sem));
}

static void
unlock_disk0(void) {
    up(&(disk0_sem));
}

static int
disk0_open(struct device *dev, uint32_t open_flags) {
    return 0;
}

static int
disk0_close(struct device *dev) {
    return 0;
}

/**
 * (未加锁地)从默认磁盘的第 blkno 号块开始加载 nblks 个块到 缓存 disk0_buffer 中.
 * 
 * 把 以扇区为单位的 IO 封装为->以块为单位的 IO
 */ 
static void
disk0_read_blks_nolock(uint32_t blkno, uint32_t nblks) {
    int ret;
    uint32_t sectno = blkno * DISK0_BLK_NSECT, nsecs = nblks * DISK0_BLK_NSECT;
    if ((ret = ide_read_secs(DISK0_DEV_NO, sectno, disk0_buffer, nsecs)) != 0) {
        panic("disk0: read blkno = %d (sectno = %d), nblks = %d (nsecs = %d): 0x%08x.\n",
                blkno, sectno, nblks, nsecs, ret);
    }
}

/**
 * 向默认磁盘的第blkno块写入 buffer 中 nblks 块的数据
 */ 
static void
disk0_write_blks_nolock(uint32_t blkno, uint32_t nblks) {
    int ret;
    uint32_t sectno = blkno * DISK0_BLK_NSECT, nsecs = nblks * DISK0_BLK_NSECT;
    if ((ret = ide_write_secs(DISK0_DEV_NO, sectno, disk0_buffer, nsecs)) != 0) {
        panic("disk0: write blkno = %d (sectno = %d), nblks = %d (nsecs = %d): 0x%08x.\n",
                blkno, sectno, nblks, nsecs, ret);
    }
}

/**
 * 磁盘 IO,面向 device <--> iob 的 IO
 *
 * 角色: 磁盘设备 dev , 内存缓冲区 iob
 * 
 * 
 */ 
static int
disk0_io(struct device *dev, struct iobuf *iob, bool write) {
    off_t offset = iob->io_offset;          // 模拟磁盘的目标范围起始地址
    size_t resid = iob->io_resid;           // 模拟磁盘的目标范围偏移量
    uint32_t blkno = offset / DISK0_BLKSIZE;
    uint32_t nblks = resid / DISK0_BLKSIZE;

    /* don't allow I/O that isn't block-aligned */
    if ((offset % DISK0_BLKSIZE) != 0 || (resid % DISK0_BLKSIZE) != 0) {
        return -E_INVAL;
    }

    /* don't allow I/O past the end of disk0 */
    if (blkno + nblks > dev->d_blocks) {
        return -E_INVAL;
    }

    /* read/write nothing ? */
    if (nblks == 0) {
        return 0;
    }

    lock_disk0();
    while (resid != 0) {
        size_t copied, alen = DISK0_BUFSIZE;
        // 写入磁盘
        if (write) {
            //1. iob -> disk0_buffer
            iobuf_move(iob, disk0_buffer, alen, 0, &copied);
            assert(copied != 0 && copied <= resid && copied % DISK0_BLKSIZE == 0);
            nblks = copied / DISK0_BLKSIZE;
            //2. disk0_buffer->刷入磁盘
            disk0_write_blks_nolock(blkno, nblks);
        }
        // 读取磁盘
        else {
            if (alen > resid) {
                alen = resid;
            }
            nblks = alen / DISK0_BLKSIZE;
            // 把操作量每次按disk0_buffer分割,每次只从磁盘读取一个缓冲区disk0_buffer的量
            disk0_read_blks_nolock(blkno, nblks);
            // 从磁盘级模块缓冲复制到上层缓冲
            iobuf_move(iob, disk0_buffer, alen, 1, &copied);
            assert(copied == alen && copied % DISK0_BLKSIZE == 0);
        }
        resid -= copied, blkno += nblks;
    }
    unlock_disk0();
    return 0;
}

static int
disk0_ioctl(struct device *dev, int op, void *data) {
    return -E_UNIMP;
}

/**
 * 磁盘模块初始化
 * 
 * 1. dev 函数指针 就位
 * 2. 初始化 disk0_buffer 结构,
 */ 
static void
disk0_device_init(struct device *dev) {
    LOG("disk0_device_init:\n");
    static_assert(DISK0_BLKSIZE % SECTSIZE == 0);
    if (!ide_device_valid(DISK0_DEV_NO)) {
        panic("disk0 device isn't available.\n");
    }
    dev->d_blocks = ide_device_size(DISK0_DEV_NO) / DISK0_BLK_NSECT;
    dev->d_blocksize = DISK0_BLKSIZE;
    dev->d_open = disk0_open;
    dev->d_close = disk0_close;
    dev->d_io = disk0_io;
    dev->d_ioctl = disk0_ioctl;
    sem_init(&(disk0_sem), 1);
    LOG_TAB("初始化数据结构: dev\n");
    static_assert(DISK0_BUFSIZE % DISK0_BLKSIZE == 0);
    // 初始化磁盘 buffer
    if ((disk0_buffer = kmalloc(DISK0_BUFSIZE)) == NULL) {
        panic("disk0 alloc buffer failed.\n");
    }
    LOG_TAB("初始化数据结构: disk0_buffer = 4 PGSIZE\n");
    LOG_TAB("disk0 数据结构已就位.\n");
}

/**
 * 功能:磁盘初始化.
 * 机制: 创建 vfs-inode
 * 
 */ 
void
dev_init_disk0(void) {
    LOG("dev_init_disk0:\n");
    struct inode *node;
    if ((node = dev_create_inode()) == NULL) {
        panic("disk0: dev_create_node.\n");
    }
    LOG_TAB("已创建 inode: disk0, 类型为 device.\n");
    disk0_device_init(vop_info(node, device));
    LOG_TAB("已初始化: disk0 的 device 部分.\n");
    int ret;
    if ((ret = vfs_add_dev("disk0", node, 1)) != 0) {
        panic("disk0: vfs_add_dev: %e.\n", ret);
    }
    LOG("已维护:disk0 至 dev_list.\n");
}

