#include <defs.h>
#include <kmalloc.h>
#include <sem.h>
#include <vfs.h>
#include <dev.h>
#include <file.h>
#include <sfs.h>
#include <inode.h>
#include <assert.h>
#include <kdebug.h>
//called when init_main proc start
void
fs_init(void) {
    LOG_LINE("初始化开始:文件系统");
    LOG("fs_init:\n");

    vfs_init();
    LOG_TAB("初始化完毕:vfs\n");
    dev_init();
    LOG_TAB("初始化完毕:dev\n");
    sfs_init();
    LOG_TAB("初始化完毕:sfs\n");

    LOG_LINE("初始化完毕:文件系统");
}

void
fs_cleanup(void) {
    vfs_cleanup();
}

void
lock_files(struct files_struct *filesp) {
    down(&(filesp->files_sem));
}

void
unlock_files(struct files_struct *filesp) {
    up(&(filesp->files_sem));
}

/*
 * 创建一个进程的文件列表.
 * 
 * 用于新进程的初始化.
 */ 
struct files_struct *
files_create(void) {
    //LOG("[files_create]\n");
    static_assert((int)FILES_STRUCT_NENTRY > 128);
    struct files_struct *filesp;
    // 1. 分配两块内存用于存储文件结构体+fd 数组.内存大小为一整页.
    if ((filesp = kmalloc(sizeof(struct files_struct) + FILES_STRUCT_BUFSIZE)) != NULL) {
        // 2. 当前工作目录是 NULL
        filesp->pwd = NULL;
        // 打开的文件列表是
        filesp->fd_array = (void *)(filesp + 1);
        filesp->files_count = 0;
        sem_init(&(filesp->files_sem), 1);
        fd_array_init(filesp->fd_array);
    }
    return filesp;
}
//Called when a proc exit
void
files_destroy(struct files_struct *filesp) {
//    LOG("[files_destroy]\n");
    assert(filesp != NULL && files_count(filesp) == 0);
    if (filesp->pwd != NULL) {
        vop_ref_dec(filesp->pwd);
    }
    int i;
    struct file *file = filesp->fd_array;
    for (i = 0; i < FILES_STRUCT_NENTRY; i ++, file ++) {
        if (file->status == FD_OPENED) {
            fd_array_close(file);
        }
        assert(file->status == FD_NONE);
    }
    kfree(filesp);
}

void
files_closeall(struct files_struct *filesp) {
//    LOG("[files_closeall]\n");
    assert(filesp != NULL && files_count(filesp) > 0);
    int i;
    struct file *file = filesp->fd_array;
    //skip the stdin & stdout
    for (i = 2, file += 2; i < FILES_STRUCT_NENTRY; i ++, file ++) {
        if (file->status == FD_OPENED) {
            fd_array_close(file);
        }
    }
}

int
dup_files(struct files_struct *to, struct files_struct *from) {
//    LOG("[dup_files]\n");
    assert(to != NULL && from != NULL);
    assert(files_count(to) == 0 && files_count(from) > 0);
    if ((to->pwd = from->pwd) != NULL) {
        vop_ref_inc(to->pwd);
    }
    int i;
    struct file *to_file = to->fd_array, *from_file = from->fd_array;
    for (i = 0; i < FILES_STRUCT_NENTRY; i ++, to_file ++, from_file ++) {
        if (from_file->status == FD_OPENED) {
            /* alloc_fd first */
            to_file->status = FD_INIT;
            fd_array_dup(to_file, from_file);
        }
    }
    return 0;
}

