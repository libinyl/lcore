#include <defs.h>
#include <stdio.h>
#include <string.h>
#include <kmalloc.h>
#include <list.h>
#include <fs.h>
#include <vfs.h>
#include <dev.h>
#include <sfs.h>
#include <inode.h>
#include <iobuf.h>
#include <bitmap.h>
#include <error.h>
#include <assert.h>
#include <kdebug.h>


// 来自 OS161.
/*
 * sfs_sync - sync sfs's superblock and freemap in memroy into disk
 */

/*
 * Get the sfs_fs from the generic abstract fs.
 *
 * Note that the abstract struct fs, which is all the VFS
 * layer knows about, is actually a member of struct sfs_fs.
 * The pointer in the struct fs points back to the top of the
 * struct sfs_fs - essentially the same object. This can be a
 * little confusing at first.
 *
 * The following diagram may help:
 *
 *     struct sfs_fs        <-----------------\
 *           :                                |
 *           :   sfs_absfs (struct fs)        |   <------\
 *           :      :                         |          |
 *           :      :  various members        |          |
 *           :      :                         |          |
 *           :      :  fs_info(__sfs_info) ---/          |
 *           :      :                             ...|...
 *           :                                   .  VFS  .
 *           :                                   . layer . 
 *           :   other members                    .......
 *           :                                    
 *           :
 *
 * This construct is repeated with inodes and devices and other
 * similar things all over the place in ucore, so taking the
 * time to straighten it out in your mind is worthwhile.
 */
static int
sfs_sync(struct fs *fs) {
    struct sfs_fs *sfs = fsop_info(fs, sfs);
    lock_sfs_fs(sfs);
    {
        list_entry_t *list = &(sfs->inode_list), *le = list;
        while ((le = list_next(le)) != list) {
            struct sfs_inode *sin = le2sin(le, inode_link);
            vop_fsync(info2node(sin, sfs_inode));
        }
    }
    unlock_sfs_fs(sfs);

    int ret;
    if (sfs->super_dirty) {
        sfs->super_dirty = 0;
        if ((ret = sfs_sync_super(sfs)) != 0) {
            sfs->super_dirty = 1;
            return ret;
        }
        if ((ret = sfs_sync_freemap(sfs)) != 0) {
            sfs->super_dirty = 1;
            return ret;
        }
    }
    return 0;
}

/*
 * sfs_get_root - get the root directory inode  from disk (SFS_BLKN_ROOT,1)
 */
static struct inode *
sfs_get_root(struct fs *fs) {
    struct inode *node;
    int ret;
    if ((ret = sfs_load_inode(fsop_info(fs, sfs), &node, SFS_BLKN_ROOT)) != 0) {
        panic("load sfs root failed: %e", ret);
    }
    return node;
}

/*
 * sfs_unmount - unmount sfs, and free the memorys contain sfs->freemap/sfs_buffer/hash_liskt and sfs itself.
 */
static int
sfs_unmount(struct fs *fs) {
    struct sfs_fs *sfs = fsop_info(fs, sfs);
    if (!list_empty(&(sfs->inode_list))) {
        return -E_BUSY;
    }
    assert(!sfs->super_dirty);
    bitmap_destroy(sfs->freemap);
    kfree(sfs->sfs_buffer);
    kfree(sfs->hash_list);
    kfree(sfs);
    return 0;
}

/*
 * sfs_cleanup - when sfs failed, then should call this function to sync sfs by calling sfs_sync
 *
 * NOTICE: nouse now.
 */
static void
sfs_cleanup(struct fs *fs) {
    struct sfs_fs *sfs = fsop_info(fs, sfs);
    uint32_t blocks = sfs->super.blocks, unused_blocks = sfs->super.unused_blocks;
    LOG("sfs: cleanup: '%s' (%d/%d/%d)\n", sfs->super.info,
            blocks - unused_blocks, unused_blocks, blocks);
    int i, ret;
    for (i = 0; i < 32; i ++) {
        if ((ret = fsop_sync(fs)) == 0) {
            break;
        }
    }
    if (ret != 0) {
        warn("sfs: sync error: '%s': %e.\n", sfs->super.info, ret);
    }
}

/*
 * sfs_init_read - used in sfs_do_mount to read disk block(blkno, 1) directly.
 * 加载超级块,在函数 sfs_do_mount 中使用.
 * 把给定的dev设备,从第 blkno 号 block 加载一个 block 到目标缓冲区 blk_buffer
 *
 * @dev:        the block device
 * @blkno:      the NO. of disk block
 * @blk_buffer: the buffer used for read
 *
 *      (1) init iobuf
 *      (2) read dev into iobuf
 */
static int
sfs_init_read(struct device *dev, uint32_t blkno, void *blk_buffer) {
    LOG("sfs_init_read:\n");
    struct iobuf __iob, *iob = iobuf_init(&__iob, blk_buffer, SFS_BLKSIZE, blkno * SFS_BLKSIZE);
    return dop_io(dev, iob, 0);
}

/*
 * sfs_init_freemap - used in sfs_do_mount to read freemap data info in disk block(blkno, nblks) directly.
 *
 * @dev:        the block device
 * @bitmap:     the bitmap in memroy
 * @blkno:      the NO. of disk block
 * @nblks:      Rd number of disk block
 * @blk_buffer: the buffer used for read
 *
 *      (1) get data addr in bitmap
 *      (2) read dev into iobuf
 */
static int
sfs_init_freemap(struct device *dev, struct bitmap *freemap, uint32_t blkno, uint32_t nblks, void *blk_buffer) {
    size_t len;
    void *data = bitmap_getdata(freemap, &len);
    assert(data != NULL && len == nblks * SFS_BLKSIZE);
    while (nblks != 0) {
        int ret;
        if ((ret = sfs_init_read(dev, blkno, data)) != 0) {
            return ret;
        }
        blkno ++, nblks --, data += SFS_BLKSIZE;
    }
    return 0;
}

/*
 * sfs_do_mount - mount sfs file system.
 * 挂载 sfs 文件系统
 * 什么是挂载? 挂载就是,对于一个新的 device,创建一个新的 fs 结构,并添加到 boot fs中.
 * 从硬盘加载 superblock 和 freemap.
 * 
 * @dev:        the block device contains sfs file system
 * @fs_store:   the fs struct in memroy
 * 
 * 最终把 fs 初始化完毕并返回
 */
static int
sfs_do_mount(struct device *dev, struct fs **fs_store) {
    LOG("sfs_do_mount: 开始加载 sfs\n");
    static_assert(SFS_BLKSIZE >= sizeof(struct sfs_super));
    static_assert(SFS_BLKSIZE >= sizeof(struct sfs_disk_inode));
    static_assert(SFS_BLKSIZE >= sizeof(struct sfs_disk_entry));
    LOG_TAB("已校验: super, inode, entry < 1block.\n");

    if (dev->d_blocksize != SFS_BLKSIZE) {
        return -E_NA_DEV;
    }
    LOG_TAB("初始化开始: fs 结构\n");
    /* allocate fs structure */
    // 注意编程顺序:1. 申请内存 2.初始化内部结构 3. 初始化外部结构 
    struct fs *fs;
    if ((fs = alloc_fs(sfs)) == NULL) {
        return -E_NO_MEM;
    }
    struct sfs_fs *sfs = fsop_info(fs, sfs);
    LOG_TAB("初始化子类型: sfs.\n");

    sfs->dev = dev;

    int ret = -E_NO_MEM;

    void *sfs_buffer;
    if ((sfs->sfs_buffer = sfs_buffer = kmalloc(SFS_BLKSIZE)) == NULL) {
        goto failed_cleanup_fs;
    }
    LOG_TAB("已分配 sfs 缓冲区: sfs->sfs_buffer \n");

    /* load and check superblock */
    /* 加载&校验超级块(占 1 块) */
    if ((ret = sfs_init_read(dev, SFS_BLKN_SUPER, sfs_buffer)) != 0) {
        goto failed_cleanup_sfs_buffer;
    }
    LOG_TAB("已加载: 超级块.\n");

    ret = -E_INVAL;

    struct sfs_super *super = sfs_buffer;
    if (super->magic != SFS_MAGIC) {
        LOG("sfs: wrong magic in superblock. (%08x should be %08x).\n",
                super->magic, SFS_MAGIC);
        goto failed_cleanup_sfs_buffer;
    }
    if (super->blocks > dev->d_blocks) {
        LOG("sfs: fs has %u blocks, device has %u blocks.\n",
                super->blocks, dev->d_blocks);
        goto failed_cleanup_sfs_buffer;
    }
    super->info[SFS_MAX_INFO_LEN] = '\0';
    sfs->super = *super;

    ret = -E_NO_MEM;

    uint32_t i;

    /* alloc and initialize hash list */
    list_entry_t *hash_list;
    if ((sfs->hash_list = hash_list = kmalloc(sizeof(list_entry_t) * SFS_HLIST_SIZE)) == NULL) {
        goto failed_cleanup_sfs_buffer;
    }
    for (i = 0; i < SFS_HLIST_SIZE; i ++) {
        list_init(hash_list + i);
    }

    /* 加载并校验 bitmap */
    struct bitmap *freemap;
    uint32_t freemap_size_nbits = sfs_freemap_bits(super);
    if ((sfs->freemap = freemap = bitmap_create(freemap_size_nbits)) == NULL) {
        goto failed_cleanup_hash_list;
    }
    uint32_t freemap_size_nblks = sfs_freemap_blocks(super);
    if ((ret = sfs_init_freemap(dev, freemap, SFS_BLKN_FREEMAP, freemap_size_nblks, sfs_buffer)) != 0) {
        goto failed_cleanup_freemap;
    }

    uint32_t blocks = sfs->super.blocks, unused_blocks = 0;
    for (i = 0; i < freemap_size_nbits; i ++) {
        if (bitmap_test(freemap, i)) {
            unused_blocks ++;
        }
    }
    assert(unused_blocks == sfs->super.unused_blocks);

    /* and other fields */
    sfs->super_dirty = 0;
    sem_init(&(sfs->fs_sem), 1);
    sem_init(&(sfs->io_sem), 1);
    sem_init(&(sfs->mutex_sem), 1);
    list_init(&(sfs->inode_list));
    LOG("sfs: mount: '%s' (%d/%d/%d)\n", sfs->super.info,
            blocks - unused_blocks, unused_blocks, blocks);

    /* link addr of sync/get_root/unmount/cleanup funciton  fs's function pointers*/
    // 初始化 fs 函数指针部分, 指向 sfs 的函数
    fs->fs_sync = sfs_sync;
    fs->fs_get_root = sfs_get_root;
    fs->fs_unmount = sfs_unmount;
    fs->fs_cleanup = sfs_cleanup;
    *fs_store = fs;
    LOG_TAB("sfs_do_mount: 挂载 sfs 完毕.\n");
    return 0;

failed_cleanup_freemap:
    bitmap_destroy(freemap);
failed_cleanup_hash_list:
    kfree(hash_list);
failed_cleanup_sfs_buffer:
    kfree(sfs_buffer);
failed_cleanup_fs:
    kfree(fs);
    return ret;
}

int
sfs_mount(const char *devname) {
    return vfs_mount(devname, sfs_do_mount);
}

