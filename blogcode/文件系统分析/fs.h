#ifndef _ABSTRACTFS_H_
#define _ABSTRACTFS_H_

#include "ucoredef.h"

UCORE_NAMESPACE_START

class inode;

class fs
{
public:
	virtual int fs_sync() = 0;
	virtual inode& fs_get_root() = 0;
	virtual int fs_unmount() = 0;
	virtual void fs_cleanup() = 0;
};

class sfs_fs {
	//struct sfs_super super;
	//struct device* dev;
	//struct bitmap* freemap;
};
class Sfs :public fs {
private:
	sfs_fs _sfs_fs;
public:
	Sfs(sfs_fs& sfs_fs) :
		_sfs_fs(sfs_fs) {};
	// 省略sfs_fs的包装

	// 接口实现，
	int fs_sync() {};
	inode& fs_get_root() {};
	int fs_unmount() {};
	void fs_cleanup() {};

};
UCORE_NAMESPACE_END

#endif // !_ABSTRACTFS_H_
