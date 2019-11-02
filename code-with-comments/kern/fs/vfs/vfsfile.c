#include <defs.h>
#include <string.h>
#include <vfs.h>
#include <inode.h>
#include <unistd.h>
#include <error.h>
#include <assert.h>


// open file in vfs, get/create inode for file with filename path.

/**
 * 功能: 返回指定路径的 inode.
 * 参数: 路径,flag.
 * 机制: 1. flag 校验 2. 
 * 
 */ 
int
vfs_open(char *path, uint32_t open_flags, struct inode **node_store) {
    // 1. 确定读写等属性
    bool can_write = 0;
    switch (open_flags & O_ACCMODE) {
    case O_RDONLY:
        break;
    case O_WRONLY:
    case O_RDWR:
        can_write = 1;
        break;
    default:
        return -E_INVAL;
    }

    if (open_flags & O_TRUNC) {
        if (!can_write) {
            return -E_INVAL;
        }
    }

    int ret; 
    struct inode *node;
    bool excl = (open_flags & O_EXCL) != 0;
    bool create = (open_flags & O_CREAT) != 0;
    ret = vfs_lookup(path, &node);
    // ret=0 说明已经找到,否则考察是否创建
    if (ret != 0) {
        if (ret == -16 && (create)) {//  todo: 改为 -E_NOENT
            char *name;
            struct inode *dir;
            // 要创建 node,首先要找到此路径所属的 dir 和名称
            if ((ret = vfs_lookup_parent(path, &dir, &name)) != 0) {
                return ret;
            }
            // 创建 inode并返回
            ret = vop_create(dir, name, excl, &node);
        } else return ret;
    } else if (excl && create) {
        return -E_EXISTS;
    }
    assert(node != NULL);
    
    if ((ret = vop_open(node, open_flags)) != 0) {
        vop_ref_dec(node);
        return ret;
    }

    vop_open_inc(node);
    if (open_flags & O_TRUNC || create) {
        if ((ret = vop_truncate(node, 0)) != 0) {
            vop_open_dec(node);
            vop_ref_dec(node);
            return ret;
        }
    }
    *node_store = node;
    return 0;
}

// close file in vfs
int
vfs_close(struct inode *node) {
    vop_open_dec(node);
    vop_ref_dec(node);
    return 0;
}

// unimplement
int
vfs_unlink(char *path) {
    return -E_UNIMP;
}

// unimplement
int
vfs_rename(char *old_path, char *new_path) {
    return -E_UNIMP;
}

// unimplement
int
vfs_link(char *old_path, char *new_path) {
    return -E_UNIMP;
}

// unimplement
int
vfs_symlink(char *old_path, char *new_path) {
    return -E_UNIMP;
}

// unimplement
int
vfs_readlink(char *path, struct iobuf *iob) {
    return -E_UNIMP;
}

// unimplement
int
vfs_mkdir(char *path){
    return -E_UNIMP;
}
