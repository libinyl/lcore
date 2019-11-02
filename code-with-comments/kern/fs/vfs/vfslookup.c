#include <defs.h>
#include <string.h>
#include <vfs.h>
#include <inode.h>
#include <error.h>
#include <assert.h>

/*
 * get_device- Common code to pull the device name, if any, off the front of a
 *             path and choose the inode to begin the name lookup relative to.
 * 
 * 解析路径,获取对应的 inode.
 * 
 */

static int
get_device(char *path, char **subpath, struct inode **node_store) {
    // 1. 获取冒号或斜杠的位置
    int i, slash = -1, colon = -1;
    for (i = 0; path[i] != '\0'; i ++) {
        if (path[i] == ':') { colon = i; break; }
        if (path[i] == '/') { slash = i; break; }
    }
    if (colon < 0 && slash != 0) {
        /*
         * 
         * 没有冒号,且第一个字符不是/ , 例如abc/c
         *  - 说明没有指定设备;这是一个相对路径或者就是个文件名.
         *  - 接下来从当前文件夹开始,把整个字符串作为子路径.
         *  - 返回pwd 的 inode
         * */
        *subpath = path;
        // 由于是相对路径,所以返回当前工作目录的 inode
        return vfs_get_curdir(node_store);
    }
    
    if (colon > 0) {
        /** 
         * 有冒号且不位于第一个位置,说明指定了设备.例如 C:/abc/d
         *  - 用冒号位置截断字符串
         */ 
        /* device:path - get root of device's filesystem */
        // C\0/abc/d
        path[colon] = '\0';

        /* device:/path - skip slash, treat as device:path */
        while (path[++ colon] == '/');
        // 返回纯子路径 abc/d
        *subpath = path + colon;
        return vfs_get_root(path, node_store);
    }
    /**
     * 
     * 其他情况:
     *      有冒号且位于第一个位置,说明是当前文件系统的根相对路径,如            :abc
     *      没有冒号,且斜杠位于第 1 个位置,说明是boot 文件系统的根相对路径,如   /abc
     * 
     */ 
    int ret;
    if (*path == '/') {
        if ((ret = vfs_get_bootfs(node_store)) != 0) {
            return ret;
        }
    }
    else {
        assert(*path == ':');
        struct inode *node;
        if ((ret = vfs_get_curdir(&node)) != 0) {
            return ret;
        }
        /* The current directory may not be a device, so it must have a fs. */
        assert(node->in_fs != NULL);
        *node_store = fsop_get_root(node->in_fs);
        vop_ref_dec(node);
    }

    /* ///... or :/... */
    while (*(++ path) == '/');
    *subpath = path;
    return 0;
}

/*
 * vfs_lookup - get the inode according to the path filename
 * 
 * path --> inode
 */
int
vfs_lookup(char *path, struct inode **node_store) {
    int ret;
    struct inode *node;
    // path --> path1,最终文件所在的目录 inode
    if ((ret = get_device(path, &path, &node)) != 0) {
        return ret;
    }
    // inode,path1 --> node_store
    if (*path != '\0') {
        ret = vop_lookup(node, path, node_store);
        vop_ref_dec(node);
        return ret;
    }
    *node_store = node;
    return 0;
}

/*
 * vfs_lookup_parent - Name-to-vnode translation.
 *  (In BSD, both of these are subsumed by namei().)
 * 
 * 
 */
int
vfs_lookup_parent(char *path, struct inode **node_store, char **endp){
    int ret;
    struct inode *node;
    if ((ret = get_device(path, &path, &node)) != 0) {
        return ret;
    }
    *endp = path;
    *node_store = node;
    return 0;
}
