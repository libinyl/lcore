## bug 说明：

准确的说应该是 mksfs ——SFS 文件系统制作工具 "mksfs" 的 bug.

mksfs 工具用于将宿主机指定目录下所有文件，按照 SFS 文件系统的格式写入到指定的虚拟硬盘中。

此 bug 将导致用 mksfs 工具制作文件系统时，如果宿主机提供的文件的名字超过 252 个字符，则操作系统运行时只能读到前 252 个字符；而系统声明最多支持文件名字符数为 255。

复现方法很方便，不一定真要写入一个有 255 个字符名字的文件，只需把 mksfs 中声明支持文件名最大字符数的宏 `SFS_MAX_FNAME_LEN` 改为较小值，比如改为 15. 正常编译并启动系统，shell 键入 `ls`, 会发现总长度为 15 个字符的文件 `faultreadkernel` 被截断成 `faultreadker`, 共 15 - 3 = 12 个字符。

## 原因：

mksfs 工具制作 SFS 文件系统时，`add_entry` 操作中，`write_block` 的参数不正确，写入数据大小不应是 `sizeof(entry->name)`，而应该是 `sizeof(struct sfs_entry)`。

`entry` 作为目录类型 `inode` 的数据内容，只包含两个元素：子节点的 `ino` 及其 `name`, `ino`的类型是 `uint32`, `name` 的类型是 `char[SFS_MAX_FNAME_LEN + 1]`。 

bug 代码相当于只写入了 `ino` 和 `name` 的一部分内容，且未包含最后的 `'\0'` 。由于 `sizeof(entry->name) = 256`（包括最后的 `'\0'`）， 而 `ino` 是 `uint32` 类型，占 4 个字节，所以 name 的最后 3 个字符将丢失，剩余不含 `'\0'` 的部分共 252 个字符。

这个 bug 可能还有一个风险：文件系统在读取此 `entry` 的 `name` 时，寻找的是 `null-terminated string`，而由于上述截断的发生，可能写入磁盘的不完整文件名之后紧邻的不是 `'\0'`, 这就麻烦大了，会有兼容性问题。不过暂时没去查 C 语言是否规定全局 `struct` 中的 `char array` 会不会被初始化为 `\0`，如果不会的话，那就确有问题了。

## 附 问题代码：

```C
static void
add_entry(struct sfs_fs *sfs, struct cache_inode *current, struct cache_inode *file, const char *name) {
    static struct sfs_entry __entry, *entry = &__entry;
    assert(current->inode.type == SFS_TYPE_DIR && strlen(name) <= SFS_MAX_FNAME_LEN);
    entry->ino = file->ino, strcpy(entry->name, name);
    uint32_t entry_ino = sfs_alloc_ino(sfs);
    write_block(sfs, entry, sizeof(entry->name), entry_ino); // bug!
    append_block(sfs, current, sizeof(entry->name), entry_ino, name);
    file->inode.nlinks ++;
}
```