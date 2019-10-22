#include <swap.h>
#include <swapfs.h>
#include <mmu.h>
#include <fs.h>
#include <ide.h>
#include <pmm.h>
#include <assert.h>
#include <stdio.h>
#include <kdebug.h>

void
swapfs_init(void) {
    static_assert((PGSIZE % SECTSIZE) == 0);
    if (!ide_device_valid(SWAP_DEV_NO)) {
        panic("swap fs isn't available.\n");
    }
    max_swap_offset = ide_device_size(SWAP_DEV_NO) / (PGSIZE / SECTSIZE);
    LOG("swapfs_init: 交换分区条件已满足: 分页大小是扇区大小的整数倍.\n");
    LOG_TAB("已将 %d 号ide磁盘设为交换分区.\n",SWAP_DEV_NO);
    LOG_TAB("此磁盘大小为 %u byte\n",ide_device_size(SWAP_DEV_NO));
    LOG("swapfs_init end.\n");
}

int
swapfs_read(swap_entry_t entry, struct Page *page) {
    return ide_read_secs(SWAP_DEV_NO, swap_offset(entry) * PAGE_NSECT, page2kva(page), PAGE_NSECT);
}

int
swapfs_write(swap_entry_t entry, struct Page *page) {
    return ide_write_secs(SWAP_DEV_NO, swap_offset(entry) * PAGE_NSECT, page2kva(page), PAGE_NSECT);
}

