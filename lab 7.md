## lab7 实验笔记

互斥: 唯一且排他
同步: 使有序

## ucore 中哪些地方使用了信号量?

换句话说就是用信号量初始化了哪些变量?搜索后得知:

```C
disk0_device_init   ->  sem_init(&(disk0_sem), 1);
files_create        ->  sem_init(&(filesp->files_sem), 1);
sfs_do_mount        ->  sem_init(&(sfs->fs_sem), 1);
                    ->  sem_init(&(sfs->io_sem), 1);
                    ->  sem_init(&(sfs->mutex_sem), 1);
sfs_create_inode    ->  sem_init(&(sin->sem), 1);
vfs_init            ->  sem_init(&bootfs_sem, 1);
vfs_devlist_init    ->  sem_init(&vdev_list_sem, 1);
}
...省略
```






