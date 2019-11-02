#ifndef __KERN_FS_SFS_SFS_H__
#define __KERN_FS_SFS_SFS_H__

#include <defs.h>
#include <mmu.h>
#include <list.h>
#include <sem.h>
#include <unistd.h>


/**
 * ucore 中的普通文件.
 * 
 * 区别于设备文件device.
 * 
 */ 

/*
 * Simple FS (SFS) definitions visible to ucore. This covers the on-disk format
 * and is used by tools that work on SFS volumes, such as mksfs.
 */

#define SFS_MAGIC                                   0x2f8dbe2a              /* magic number for sfs */
#define SFS_BLKSIZE                                 PGSIZE                  /* size of block */
#define SFS_NDIRECT                                 12                      /* # of direct blocks in inode */
#define SFS_MAX_INFO_LEN                            31                      /* max length of infomation */
#define SFS_MAX_FNAME_LEN                           FS_MAX_FNAME_LEN        /* max length of filename */
#define SFS_MAX_FILE_SIZE                           (1024UL * 1024 * 128)   /* max file size (128M) */
#define SFS_BLKN_SUPER                              0                       /* block the superblock lives in */
#define SFS_BLKN_ROOT                               1                       /* location of the root dir inode */
#define SFS_BLKN_FREEMAP                            2                       /* 1st block of the freemap */

/* # of bits in a block */
#define SFS_BLKBITS                                 (SFS_BLKSIZE * CHAR_BIT)

/* # of entries in a block */
#define SFS_BLK_NENTRY                              (SFS_BLKSIZE / sizeof(uint32_t))

/* 文件类型 */
#define SFS_TYPE_INVAL                              0       /* Should not appear on disk */
#define SFS_TYPE_FILE                               1       // 文件
#define SFS_TYPE_DIR                                2       // 目录
#define SFS_TYPE_LINK                               3       // 链接

/*
 * On-disk superblock
 * 超级块,作用范围是整个 OS 空间
 */
struct sfs_super {
    uint32_t magic;                                 /* magic number, = SFS_MAGIC */
    uint32_t blocks;                                /* # of blocks in fs */
    uint32_t unused_blocks;                         /* # of unused blocks in fs */
    char info[SFS_MAX_INFO_LEN + 1];                /* infomation for sfs  */
};

/* inode (on disk) */
// 磁盘级的 inode, 是根目录
// 在 ucore 的实现中,inode 号就是 block 号
struct sfs_disk_inode {
    uint32_t size;                                  // 此 inode 文件大小(byte)
    uint16_t type;                                  // 文件类型, 如上 SFS_TYPE_xx
    uint16_t nlinks;                                // 此 inode 的hard链接数量
    uint32_t blocks;                                // 此 inode 的 block 数
    uint32_t direct[SFS_NDIRECT];                   // 直接数据块索引,有 12 个. 直接索引的数据页大小为 12 * 4k = 48k；
    uint32_t indirect;                              // 一级间接索引块索引值. 值为 0 时表示不使用一级索引块. 使用一级间接块索引时, ucore 支持的最大文件大小为,12个直接块,加上 4KB 可以表示的 1K 整型这么多个块,即12*4K+1024*4K=48k+4m 
//    uint32_t db_indirect;                           /* double indirect blocks */
//   unused
};

/* file entry (on disk) */
// 目录项. 一系列目录项组成路径.
struct sfs_disk_entry {
    uint32_t ino;                                   /* inode number */
    char name[SFS_MAX_FNAME_LEN + 1];               /* 文件名 */
};

#define sfs_dentry_size                             \
    sizeof(((struct sfs_disk_entry *)0)->name)

// SFS 的 inode, 加载到内存中的inode
struct sfs_inode {
    struct sfs_disk_inode *din;                     /* on-disk inode */
    uint32_t ino;                                   /* inode number */
    bool dirty;                                     /* true if inode modified */
    int reclaim_count;                              /* kill inode if it hits zero */
    semaphore_t sem;                                /* semaphore for din */
    list_entry_t inode_link;                        /* entry for linked-list in sfs_fs */
    list_entry_t hash_link;                         /* entry for hash linked-list in sfs_fs */
};

#define le2sin(le, member)                          \
    to_struct((le), struct sfs_inode, member)

/* filesystem for sfs */
// SFS 文件系统结构体,描述 SFS 在硬盘上的整体分布
struct sfs_fs {
    struct sfs_super super;                         /* 超级块          on-disk superblock */
    struct device *dev;                             /* 被挂载的设备     device mounted on */
    struct bitmap *freemap;                         /* freemap, 空闲的 inode结构    blocks in use are mared 0 */
    bool super_dirty;                               /* true if super/freemap modified */
    void *sfs_buffer;                               /* buffer for non-block aligned io */
    semaphore_t fs_sem;                             /* semaphore for fs */
    semaphore_t io_sem;                             /* semaphore for io */
    semaphore_t mutex_sem;                          /* semaphore for link/unlink and rename */
    list_entry_t inode_list;                        /* 硬盘上所有的 inode   inode linked-list */
    list_entry_t *hash_list;                        /* inode hash linked-list */
};

/* hash for sfs */
#define SFS_HLIST_SHIFT                             10
#define SFS_HLIST_SIZE                              (1 << SFS_HLIST_SHIFT)
#define sin_hashfn(x)                               (hash32(x, SFS_HLIST_SHIFT))

/* size of freemap (in bits) */
// 根据 super 结构获取 freemap 的 bit 数 = 向上取整(块数 * 每块位数)
#define sfs_freemap_bits(super)                     ROUNDUP((super)->blocks, SFS_BLKBITS)

/* size of freemap (in blocks) */
#define sfs_freemap_blocks(super)                   ROUNDUP_DIV((super)->blocks, SFS_BLKBITS)

struct fs;
struct inode;

void sfs_init(void);
int sfs_mount(const char *devname);

void lock_sfs_fs(struct sfs_fs *sfs);
void lock_sfs_io(struct sfs_fs *sfs);
void unlock_sfs_fs(struct sfs_fs *sfs);
void unlock_sfs_io(struct sfs_fs *sfs);

int sfs_rblock(struct sfs_fs *sfs, void *buf, uint32_t blkno, uint32_t nblks);
int sfs_wblock(struct sfs_fs *sfs, void *buf, uint32_t blkno, uint32_t nblks);
int sfs_rbuf(struct sfs_fs *sfs, void *buf, size_t len, uint32_t blkno, off_t offset);
int sfs_wbuf(struct sfs_fs *sfs, void *buf, size_t len, uint32_t blkno, off_t offset);
int sfs_sync_super(struct sfs_fs *sfs);
int sfs_sync_freemap(struct sfs_fs *sfs);
int sfs_clear_block(struct sfs_fs *sfs, uint32_t blkno, uint32_t nblks);

int sfs_load_inode(struct sfs_fs *sfs, struct inode **node_store, uint32_t ino);

#endif /* !__KERN_FS_SFS_SFS_H__ */

