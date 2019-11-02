/**
 * 磁盘文件系统生成程序.
 *
 */

/* prefer to compile mksfs on 64-bit linux systems.

Use a compiler-specific macro.

For example:

#if defined(__i386__)
// IA-32
#elif defined(__x86_64__)
// AMD64
#else
# error Unsupported architecture
#endif

*/

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <assert.h>

#define IS_MKSFS_LOG 1

int will_log = 1;

typedef int bool;

#define __error(msg, quit, ...)                                                         \
    do {                                                                                \
        fprintf(stderr, #msg ": function %s - line %d: ", __FUNCTION__, __LINE__);      \
        if (errno != 0) {                                                               \
            fprintf(stderr, "[error] %s: ", strerror(errno));                           \
        }                                                                               \
        fprintf(stderr, "\n\t"), fprintf(stderr, __VA_ARGS__);                          \
        errno = 0;                                                                      \
        if (quit) {                                                                     \
            exit(-1);                                                                   \
        }                                                                               \
    } while (0)

#define warn(...)           __error(warn, 0, __VA_ARGS__)
#define bug(...)            __error(bug, 1, __VA_ARGS__)

#define _NO_LOG_START       do{will_log=0;}while(0);{
#define _NO_LOG_END         do{will_log=1;}while(0);}

#define log(...)            do{ if (will_log && IS_MKSFS_LOG) fprintf(stdout, __VA_ARGS__); } while(0)
#define logtab(...)         do{ if (will_log && IS_MKSFS_LOG) fprintf(stdout, "\t"__VA_ARGS__); } while(0)
/*
static_assert(cond, msg) is defined in /usr/include/assert.h
#define static_assert(x)                                                                \
    switch (x) {case 0: case (x): ; }
*/

/* 2^31 + 2^29 - 2^25 + 2^22 - 2^19 - 2^16 + 1 */
#define GOLDEN_RATIO_PRIME_32       0x9e370001UL

#define HASH_SHIFT                              10
#define HASH_LIST_SIZE                          (1 << HASH_SHIFT)

static inline uint32_t
__hash32(uint32_t val, unsigned int bits) {
    uint32_t hash = val * GOLDEN_RATIO_PRIME_32;
    return (hash >> (32 - bits));
}

static uint32_t
hash32(uint32_t val) {
    return __hash32(val, HASH_SHIFT);
}

static uint32_t
hash64(uint64_t val) {
    return __hash32((uint32_t)val, HASH_SHIFT);
}

void *
safe_malloc(size_t size) {
    void *ret;
    if ((ret = malloc(size)) == NULL) {
        bug("malloc %lu bytes failed.\n", (long unsigned)size);
    }
    return ret;
}

char *
safe_strdup(const char *str) {
    char *ret;
    if ((ret = strdup(str)) == NULL) {
        bug("strdup failed: %s\n", str);
    }
    return ret;
}

struct stat *
safe_stat(const char *filename) {
    static struct stat __stat;
    if (stat(filename, &__stat) != 0) {
        bug("stat %s failed.\n", filename);
    }
    return &__stat;
}

struct stat *
safe_fstat(int fd) {
    static struct stat __stat;
    if (fstat(fd, &__stat) != 0) {
        bug("fstat %d failed.\n", fd);
    }
    return &__stat;
}

struct stat *
safe_lstat(const char *name) {
    static struct stat __stat;
    if (lstat(name, &__stat) != 0) {
        bug("lstat '%s' failed.\n", name);
    }
    return &__stat;
}

void
safe_fchdir(int fd) {
    if (fchdir(fd) != 0) {
        bug("fchdir failed %d.\n", fd);
    }
}

#define SFS_MAGIC                               0x2f8dbe2a
#define SFS_NDIRECT                             12                          // 一个 inode 最大 block 数 = 12
#define SFS_BLKSIZE                             4096                        // 块大小 = 4KB
#define SFS_MAX_NBLKS                           (1024UL * 512)              // 实际的文件系统块数最大值 = 4K * 512K. 原注释 4K*512K 是不是有问题? 如果按此宏计算,文件系统最大值为 1024*512*4KB = 2GB
#define SFS_MAX_INFO_LEN                        31
#define SFS_MAX_FNAME_LEN                       255
#define SFS_MAX_FILE_SIZE                       (1024UL * 1024 * 128)       // 128M

#define SFS_BLKBITS                             (SFS_BLKSIZE * CHAR_BIT)    // 每个块的bit数 = 4K * 8bit
#define SFS_TYPE_FILE                           1
#define SFS_TYPE_DIR                            2
#define SFS_TYPE_LINK                           3

#define SFS_BLKN_SUPER                          0
#define SFS_BLKN_ROOT                           1
#define SFS_BLKN_FREEMAP                        2                       // bitmap(freemap)的起始块号

struct cache_block {
    uint32_t ino;
    struct cache_block *hash_next;
    void *cache;
};

struct cache_inode {
    struct inode {
        uint32_t size;
        uint16_t type;
        uint16_t nlinks;
        uint32_t blocks;
        uint32_t direct[SFS_NDIRECT];
        uint32_t indirect;
        uint32_t db_indirect;
    } inode;
    ino_t real;
    uint32_t ino;   // 数据起始 block 号
    uint32_t nblks; // 数据占据的块数
    struct cache_block *l1, *l2;
    struct cache_inode *hash_next;
};

struct sfs_fs {
    struct {
        uint32_t magic;
        uint32_t blocks;
        uint32_t unused_blocks;
        char info[SFS_MAX_INFO_LEN + 1];
    } super;
    struct subpath {
        struct subpath *next, *prev;
        char *subname;
    } __sp_nil, *sp_root, *sp_end;
    int imgfd;                                  // 在编译机上的目标 IMG 的 fd
    uint32_t ninos, next_ino;                   // ninos: 总块数; next_ino: superblock + rootinode + bitmap 的块数,=3
    struct cache_inode *root;
    struct cache_inode *inodes[HASH_LIST_SIZE];
    struct cache_block *blocks[HASH_LIST_SIZE];
};
// entry block 的内容 32+256B
struct sfs_entry {
    uint32_t ino;   // 此 entryblock 指向的 inode 号
    char name[SFS_MAX_FNAME_LEN + 1]; // 此 entry block 指向的 inode 的名称
};

static uint32_t
sfs_alloc_ino(struct sfs_fs *sfs) {
    if (sfs->next_ino < sfs->ninos) {
        sfs->super.unused_blocks --;
        return sfs->next_ino ++;
    }
    bug("out of disk space.\n");
}

static struct cache_block *
alloc_cache_block(struct sfs_fs *sfs, uint32_t ino) {
    struct cache_block *cb = safe_malloc(sizeof(struct cache_block));
    cb->ino = (ino != 0) ? ino : sfs_alloc_ino(sfs);
    cb->cache = memset(safe_malloc(SFS_BLKSIZE), 0, SFS_BLKSIZE);
    struct cache_block **head = sfs->blocks + hash32(ino);
    cb->hash_next = *head, *head = cb;
    return cb;
}

struct cache_block *
search_cache_block(struct sfs_fs *sfs, uint32_t ino) {
    struct cache_block *cb = sfs->blocks[hash32(ino)];
    while (cb != NULL && cb->ino != ino) {
        cb = cb->hash_next;
    }
    return cb;
}
/*
 *
 * 若指定 ino=0 则新分配 ino
 * type=1:file type=2:dir
 */
static struct cache_inode *
alloc_cache_inode(struct sfs_fs *sfs, ino_t real, uint32_t ino, uint16_t type) {
    log("alloc_cache_inode:\n");
    struct cache_inode *ci = safe_malloc(sizeof(struct cache_inode));
    ci->ino = (ino != 0) ? ino : sfs_alloc_ino(sfs);
    ci->real = real, ci->nblks = 0, ci->l1 = ci->l2 = NULL;
    struct inode *inode = &(ci->inode);
    memset(inode, 0, sizeof(struct inode));
    inode->type = type;
    struct cache_inode **head = sfs->inodes + hash64(real);
    ci->hash_next = *head, *head = ci;
    return ci;
}

struct cache_inode *
search_cache_inode(struct sfs_fs *sfs, ino_t real) {
    struct cache_inode *ci = sfs->inodes[hash64(real)];
    while (ci != NULL && ci->real != real) {
        ci = ci->hash_next;
    }
    return ci;
}

/**
 * 组装文件系统信息结构
 */
struct sfs_fs *
create_sfs(int imgfd) {
    log("create_sfs:\n");
    // 1. 计算目标文件可容纳总块数 ninos,计算文件系统元数据区实际占用块数 next_ino.
    uint32_t ninos, next_ino;
    struct stat *stat = safe_fstat(imgfd);
    logtab("img 大小: %lldB = %lldM\n",stat->st_size, stat->st_size/1024/1024);
    // 总块数 = 编译机指定的目标IMG文件大小/块大小
    if ((ninos = stat->st_size / SFS_BLKSIZE) > SFS_MAX_NBLKS) {
        logtab("计算块数 ninos = %ud\n",ninos);
        ninos = SFS_MAX_NBLKS;
        warn("img file is too big (%llu bytes, only use %u blocks).\n",
             (unsigned long long)stat->st_size, ninos);
    }
    logtab("已确定块数 ninos = min{ (stat->st_size / SFS_BLKSIZE), SFS_MAX_NBLKS} = %u\n",ninos);
    // bitmap
    if ((next_ino = SFS_BLKN_FREEMAP + (ninos + SFS_BLKBITS - 1) / SFS_BLKBITS) >= ninos) {
        bug("img file is too small (%llu bytes, %u blocks, bitmap use at least %u blocks).\n",
            (unsigned long long)stat->st_size, ninos, next_ino - 2);
    }
    logtab("已确定 next_ino = %u\n", next_ino);
    logtab("开始初始化 sfs 结构.\n");
    struct sfs_fs *sfs = safe_malloc(sizeof(struct sfs_fs));
    sfs->super.magic = SFS_MAGIC;

    // /---- next_ino = 3 -------|----- unused_blocks ---\
    // super | rootinode | bitmap|------------------------
    // \---------------------- ninos---------------------/

    sfs->super.blocks = ninos, sfs->super.unused_blocks = ninos - next_ino;
    logtab("初始化 sfs->super: blocks = %d, unused_blocks = %d\n", ninos, ninos - next_ino );
    snprintf(sfs->super.info, SFS_MAX_INFO_LEN, "simple file system");

    sfs->ninos = ninos, sfs->next_ino = next_ino, sfs->imgfd = imgfd;

    sfs->sp_root = sfs->sp_end = &(sfs->__sp_nil);
    sfs->sp_end->prev = sfs->sp_end->next = NULL;

    int i;
    for (i = 0; i < HASH_LIST_SIZE; i ++) {
        sfs->inodes[i] = NULL;
        sfs->blocks[i] = NULL;
    }

    sfs->root = alloc_cache_inode(sfs, 0, SFS_BLKN_ROOT, SFS_TYPE_DIR);
    logtab("sfs 结构初始化完毕.\n");
    return sfs;
}

static void
subpath_push(struct sfs_fs *sfs, const char *subname) {
    struct subpath *subpath = safe_malloc(sizeof(struct subpath));
    subpath->subname = safe_strdup(subname);
    sfs->sp_end->next = subpath;
    subpath->prev = sfs->sp_end;
    subpath->next = NULL;
    sfs->sp_end = subpath;
}

static void
subpath_pop(struct sfs_fs *sfs) {
    assert(sfs->sp_root != sfs->sp_end);
    struct subpath *subpath = sfs->sp_end;
    sfs->sp_end = sfs->sp_end->prev, sfs->sp_end->next = NULL;
    free(subpath->subname), free(subpath);
}

static void
subpath_show(FILE *fout, struct sfs_fs *sfs, const char *name) {
    struct subpath *subpath = sfs->sp_root;
    fprintf(fout, "current is: /");
    while ((subpath = subpath->next) != NULL) {
        fprintf(fout, "%s/", subpath->subname);
    }
    if (name != NULL) {
        fprintf(fout, "%s", name);
    }
    fprintf(fout, "\n");
}

/** 对 pwrite 的封装,
 * 向文件磁盘写入一整块数据,用于新增 inode table 元素,或者新增 block
 * 1) 用于写入dir 的 block,则 data 为 entry,
 * 向 sfs->imgfd 的 ino 号块写入 data 数据的前 len 个字节.
 */
static void
write_block(struct sfs_fs *sfs, void *data, size_t len, uint32_t ino) {
    log("write_block:\n");
    _NO_LOG_START
        assert(len <= SFS_BLKSIZE && ino < sfs->ninos);
        logtab("校验: 要写入的数据长度 len = %zu \n", len);
        static char buffer[SFS_BLKSIZE];
        logtab("调整 data: 若要写入的数据小于 1 块,则复制到大小为 1 块的缓冲区中,确保最后写入 1 整块数据.");
        if (len != SFS_BLKSIZE) {
            memset(buffer, 0, sizeof(buffer));
            data = memcpy(buffer, data, len);
            logtab("从data=>buffer, 复制了%zu个字节.\n",len);
        }
        off_t offset = (off_t)ino * SFS_BLKSIZE;
        ssize_t ret;
        if ((ret = pwrite(sfs->imgfd, data, SFS_BLKSIZE, offset)) != SFS_BLKSIZE) {
            bug("write %u block failed: (%d/%d).\n", ino, (int)ret, SFS_BLKSIZE);
        }
        logtab("已向 imgfd 从 offset = %lld 的位置开始写入了data 的一块数据\n", offset);
    _NO_LOG_END
}

static void
flush_cache_block(struct sfs_fs *sfs, struct cache_block *cb) {
    write_block(sfs, cb->cache, SFS_BLKSIZE, cb->ino);
}

static void
flush_cache_inode(struct sfs_fs *sfs, struct cache_inode *ci) {
    write_block(sfs, &(ci->inode), sizeof(ci->inode), ci->ino);
}

void
close_sfs(struct sfs_fs *sfs) {
    log("close_sfs:\n");
    // 1. 写入位图 freemap
    //      buffer: 每个块的 bitmap
    static char buffer[SFS_BLKSIZE];
    uint32_t i, j, ino = SFS_BLKN_FREEMAP;
    uint32_t ninos = sfs->ninos, next_ino = sfs->next_ino;
    // freemap 区域,每个循环处理一块的 bit
    for (i = 0; i < ninos; ino ++, i += SFS_BLKBITS) {
        memset(buffer, 0, sizeof(buffer));
        // 对最后一块的处理
        if (i + SFS_BLKBITS > next_ino) {
            uint32_t start = 0, end = SFS_BLKBITS;
            if (i < next_ino) {
                start = next_ino - i;
            }
            // 至少要有一个空闲的 block
            if (i + SFS_BLKBITS > ninos) {
                end = ninos - i;
            }
            uint32_t *data = (uint32_t *)buffer;
            const uint32_t bits = sizeof(bits) * CHAR_BIT;
            for (j = start; j < end; j ++) {
                data[j / bits] |= (1 << (j % bits));
            }
        }

        write_block(sfs, buffer, sizeof(buffer), ino);
    }
    // 写入超级块
    write_block(sfs, &(sfs->super), sizeof(sfs->super), SFS_BLKN_SUPER);

    for (i = 0; i < HASH_LIST_SIZE; i ++) {
        struct cache_block *cb = sfs->blocks[i];
        while (cb != NULL) {
            flush_cache_block(sfs, cb);
            cb = cb->hash_next;
        }
        struct cache_inode *ci = sfs->inodes[i];
        while (ci != NULL) {
            flush_cache_inode(sfs, ci);
            ci = ci->hash_next;
        }
    }
}

struct sfs_fs *
open_img(const char *imgname) {
    log("open_img:\n");

    const char *expect = ".img", *ext = imgname + strlen(imgname) - strlen(expect);
    if (ext <= imgname || strcmp(ext, expect) != 0) {
        bug("invalid .img file name '%s'.\n", imgname);
    }
    int imgfd;
    if ((imgfd = open(imgname, O_WRONLY)) < 0) {
        bug("open '%s' failed.\n", imgname);
    }
    logtab("已open镜像文件得到 fd\n");
    return create_sfs(imgfd);
}

#define open_bug(sfs, name, ...)                                                        \
    do {                                                                                \
        subpath_show(stderr, sfs, name);                                                \
        bug(__VA_ARGS__);                                                               \
    } while (0)

#define show_fullpath(sfs, name) subpath_show(stderr, sfs, name)

void open_dir(struct sfs_fs *sfs, struct cache_inode *current, struct cache_inode *parent);
void open_file(struct sfs_fs *sfs, struct cache_inode *file, const char *filename, int fd);
void open_link(struct sfs_fs *sfs, struct cache_inode *file, const char *filename);

#define SFS_BLK_NENTRY                          (SFS_BLKSIZE / sizeof(uint32_t))    // 索引 block 的索引数=1024
#define SFS_L0_NBLKS                            SFS_NDIRECT
#define SFS_L1_NBLKS                            (SFS_BLK_NENTRY + SFS_L0_NBLKS)
#define SFS_L2_NBLKS                            (SFS_BLK_NENTRY * SFS_BLK_NENTRY + SFS_L1_NBLKS)
#define SFS_LN_NBLKS                            (SFS_MAX_FILE_SIZE / SFS_BLKSIZE)

static void
update_cache(struct sfs_fs *sfs, struct cache_block **cbp, uint32_t *inop) {
    uint32_t ino = *inop;
    struct cache_block *cb = *cbp;
    if (ino == 0) {
        cb = alloc_cache_block(sfs, 0);
        ino = cb->ino;
    }
    else if (cb == NULL || cb->ino != ino) {
        cb = search_cache_block(sfs, ino);
        assert(cb != NULL && cb->ino == ino);
    }
    *cbp = cb, *inop = ino;
}
// 对于操作的 inode 更新其状态.file: 就是操作的 inode. ino:新添加的block号,需要在 file 中维护
static void
append_block(struct sfs_fs *sfs, struct cache_inode *file, size_t size, uint32_t ino, const char *filename) {
    static_assert(SFS_LN_NBLKS <= SFS_L2_NBLKS, "SFS_LN_NBLKS <= SFS_L2_NBLKS");
    assert(size <= SFS_BLKSIZE);
    uint32_t nblks = file->nblks;
    struct inode *inode = &(file->inode);
    if (nblks >= SFS_LN_NBLKS) {
        open_bug(sfs, filename, "file is too big.\n");
    }
    if (nblks < SFS_L0_NBLKS) {
        inode->direct[nblks] = ino;
    }
    else if (nblks < SFS_L1_NBLKS) {
        nblks -= SFS_L0_NBLKS;
        update_cache(sfs, &(file->l1), &(inode->indirect));
        uint32_t *data = file->l1->cache;
        data[nblks] = ino;
    }
    else if (nblks < SFS_L2_NBLKS) {
        nblks -= SFS_L1_NBLKS;
        update_cache(sfs, &(file->l2), &(inode->db_indirect));
        uint32_t *data2 = file->l2->cache;
        update_cache(sfs, &(file->l1), &data2[nblks / SFS_BLK_NENTRY]);
        uint32_t *data1 = file->l1->cache;
        data1[nblks % SFS_BLK_NENTRY] = ino;
    }
    file->nblks ++;
    inode->size += size;// 对于目录 inode, 其成员的文件名占据大小.
    inode->blocks ++;
}
// 增加一个条目需要改变两个对象的状态: inode 和 block. 前一个是 current, 后一个是 file,也就是实际数据
static void
add_entry(struct sfs_fs *sfs, struct cache_inode *current, struct cache_inode *file, const char *name) {
    static struct sfs_entry __entry, *entry = &__entry;
    assert(current->inode.type == SFS_TYPE_DIR && strlen(name) <= SFS_MAX_FNAME_LEN);
    entry->ino = file->ino, strcpy(entry->name, name);
    uint32_t entry_ino = sfs_alloc_ino(sfs);
    write_block(sfs, entry, sizeof(entry->name), entry_ino);
    append_block(sfs, current, sizeof(entry->name), entry_ino, name);
    file->inode.nlinks ++;
}

static void
add_dir(struct sfs_fs *sfs, struct cache_inode *parent, const char *dirname, int curfd, int fd, ino_t real) {
    assert(search_cache_inode(sfs, real) == NULL);
    struct cache_inode *current = alloc_cache_inode(sfs, real, 0, SFS_TYPE_DIR);
    safe_fchdir(fd), subpath_push(sfs, dirname);
    open_dir(sfs, current, parent);
    safe_fchdir(curfd), subpath_pop(sfs);
    add_entry(sfs, parent, current, dirname);
}
// add 操作,添加文件,是在 current inode目录下添加文件,所以一方面要更新文件的 inode,还要更新 current inode 目录
static void
add_file(struct sfs_fs *sfs, struct cache_inode *current, const char *filename, int fd, ino_t real) {
    struct cache_inode *file;
    if ((file = search_cache_inode(sfs, real)) == NULL) {
        file = alloc_cache_inode(sfs, real, 0, SFS_TYPE_FILE);
        open_file(sfs, file, filename, fd);
    }
    add_entry(sfs, current, file, filename);
}

static void
add_link(struct sfs_fs *sfs, struct cache_inode *current, const char *filename, ino_t real) {
    struct cache_inode *file = alloc_cache_inode(sfs, real, 0, SFS_TYPE_LINK);
    open_link(sfs, file, filename);
    add_entry(sfs, current, file, filename);
}
// 每一个"open",最终都置状态为创建完毕.
void
open_dir(struct sfs_fs *sfs, struct cache_inode *current, struct cache_inode *parent) {
    DIR *dir;
    if ((dir = opendir(".")) == NULL) {
        open_bug(sfs, NULL, "opendir failed.\n");
    }
    add_entry(sfs, current, current, ".");
    add_entry(sfs, current, parent, "..");
    struct dirent *direntp;
    while ((direntp = readdir(dir)) != NULL) {
        const char *name = direntp->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue ;
        }
        if (name[0] == '.') {// 略过隐藏文件
            continue ;
        }
        if (strlen(name) > SFS_MAX_FNAME_LEN) {
            open_bug(sfs, NULL, "file name is too long: %s\n", name);
        }
        struct stat *stat = safe_lstat(name);
        if (S_ISLNK(stat->st_mode)) {
            add_link(sfs, current, name, stat->st_ino);
        }
        else { //把编译好的二进制文件写入到磁盘中
            int fd;
            if ((fd = open(name, O_RDONLY)) < 0) {
                open_bug(sfs, NULL, "open failed: %s\n", name);
            }
            if (S_ISDIR(stat->st_mode)) {
                add_dir(sfs, current, name, dirfd(dir), fd, stat->st_ino);
            }
            else if (S_ISREG(stat->st_mode)) {// regular 常规文件
                add_file(sfs, current, name, fd, stat->st_ino);
            }
            else {
                char mode = '?';
                if (S_ISFIFO(stat->st_mode)) mode = 'f';
                if (S_ISSOCK(stat->st_mode)) mode = 's';
                if (S_ISCHR(stat->st_mode)) mode = 'c';
                if (S_ISBLK(stat->st_mode)) mode = 'b';
                show_fullpath(sfs, NULL);
                warn("unsupported mode %07x (%c): file %s\n", stat->st_mode, mode, name);
            }
            close(fd);
        }
    }
    closedir(dir);
}
// open 操作,保证最终创建完毕,open_file 专门面向 文件.1)写 block 2)更新文件所在inode 状态
void
open_file(struct sfs_fs *sfs, struct cache_inode *file, const char *filename, int fd) {
    static char buffer[SFS_BLKSIZE];
    ssize_t ret, last = SFS_BLKSIZE;
    while ((ret = read(fd, buffer, sizeof(buffer))) != 0) {// 宿主机文件可能大于 1 个 buffer,要持续读写
        assert(last == SFS_BLKSIZE);
        uint32_t ino = sfs_alloc_ino(sfs);// 每循环一次就向新块写入
        write_block(sfs, buffer, ret, ino);
        append_block(sfs, file, ret, ino, filename);
        last = ret;
    }
    if (ret < 0) {
        open_bug(sfs, filename, "read file failed.\n");
    }
}

void
open_link(struct sfs_fs *sfs, struct cache_inode *file, const char *filename) {
    static char buffer[SFS_BLKSIZE];
    uint32_t ino = sfs_alloc_ino(sfs);
    ssize_t ret = readlink(filename, buffer, sizeof(buffer));
    if (ret < 0 || ret == SFS_BLKSIZE) {
        open_bug(sfs, filename, "read link failed, %d", (int)ret);
    }
    write_block(sfs, buffer, ret, ino);
    append_block(sfs, file, ret, ino, filename);
}

int
create_img(struct sfs_fs *sfs, const char *home) {
    int curfd, homefd;
    if ((curfd = open(".", O_RDONLY)) < 0) {
        bug("get current fd failed.\n");
    }
    if ((homefd = open(home, O_RDONLY | O_NOFOLLOW)) < 0) {
        bug("open home directory '%s' failed.\n", home);
    }
    safe_fchdir(homefd);
    open_dir(sfs, sfs->root, sfs->root);//每个 open 都是保证最终的创建完毕状态, 前后两个参数分别是 inode 和 block
    safe_fchdir(curfd);
    close(curfd), close(homefd);
    close_sfs(sfs);
    return 0;
}

static void
static_check(void) {
#if defined(__i386__)
    // IA-32, gcc with -D_FILE_OFFSET_BITS=64
	static_assert(sizeof(off_t) == 8, "sizeof off_t should be 8 in i386");
    static_assert(sizeof(ino_t) == 8,"sizeof ino_t should be 8 in i386");
    printf("in i386 system, need more testing\n");
#elif defined(__x86_64__)
// AMD64, Recommend, gcc with -D_FILE_OFFSET_BITS=64  off_t 和 ino_t 为 long long 类型
    static_assert(sizeof(off_t) == 8, "sizeof off_t should be 8 in x86_64");
    static_assert(sizeof(ino_t) == 8, "sizeof ino_t should be 8 in x86_64");
#else
# error Unsupported architecture
#endif
    static_assert(SFS_MAX_NBLKS <= 0x80000000UL, "SFS_MAX_NBLKS <= 0x80000000UL");
    static_assert(SFS_MAX_FILE_SIZE <= 0x80000000UL,"SFS_MAX_FILE_SIZE <= 0x80000000UL");
}

/**
 *  按 SFS 格式化磁盘
 *  并把 disk0 下编译好的程序写入到虚拟磁盘 sfs.img 中
 *
 * 编译:
 *
 *
 *
 * 执行:
 * mksfs bin/sfs.img disk0
 *
 * 远程调试:
 * gdbserver :1233 bin/mksfs bin/sfs.img disk0
 *
 * 调试:
 * 清空 sfs.img: dd if=/dev/zero of=bin/sfs.img bs=1Mwrite_block count=128
 * 带参数调试:    gdb --args bin/mksfs bin/sfs.img disk0
 */
int
main(int argc, char **argv) {
    log("\n\n----------文件系统生成程序----------\n\n");
    log("SFS 文件系统设计规格:\n");
    log("\n/------ next_ino = 3 ------|----- unused_blocks ---\\\nsuper | rootinode | bitmap |------------------------\n\\----------------------- ninos---------------------/\n\n");
    logtab("每块大小:%d B\n",SFS_BLKSIZE);          // 4KB
    logtab("super 起始块: %d\n",SFS_BLKN_SUPER);    // 0
    logtab("root 起始块: %d\n",SFS_BLKN_ROOT);      // 1
    logtab("freemap 起始块: %d\n",SFS_BLKN_FREEMAP); // 2
    //命令: mksfs bin/sfs.img disk0, 把 sfs.img 这个 128M 的空文件
    log("参数: %s, %s\n", argv[1],argv[2]);

    static_check();
    if (argc != 3) {
        bug("usage: <input *.img> <input dirname>\n");
    }
    const char *imgname = argv[1], *home = argv[2];
    if (create_img(open_img(imgname), home) != 0) {
        bug("create img failed.\n");
    }
    printf("\n----------%s (%s) 成功.----------\n\n", imgname, home);
    return 0;
}

