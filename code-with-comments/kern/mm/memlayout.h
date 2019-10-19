#ifndef __KERN_MM_MEMLAYOUT_H__
#define __KERN_MM_MEMLAYOUT_H__

/* This file contains the definitions for memory management in our OS. */

/* global segment number 

用于 selector,也是 gdt 的索引

*/
#define SEG_KTEXT   1
#define SEG_KDATA   2
#define SEG_UTEXT   3
#define SEG_UDATA   4
#define SEG_TSS     5

/* global descrptor numbers <<3 = * 8  */
#define GD_KTEXT    ((SEG_KTEXT) << 3)      // kernel text
#define GD_KDATA    ((SEG_KDATA) << 3)      // kernel data
#define GD_UTEXT    ((SEG_UTEXT) << 3)      // user text
#define GD_UDATA    ((SEG_UDATA) << 3)      // user data
#define GD_TSS      ((SEG_TSS) << 3)        // task segment selector

#define DPL_KERNEL  (0)
#define DPL_USER    (3)

#define KERNEL_CS   ((GD_KTEXT) | DPL_KERNEL)   // 段选择子,selector
#define KERNEL_DS   ((GD_KDATA) | DPL_KERNEL)
#define USER_CS     ((GD_UTEXT) | DPL_USER)
#define USER_DS     ((GD_UDATA) | DPL_USER)

/* *
 * 虚拟地址空间分布 map:                                            权限
 *                                                              kernel/user
 *
 *     4G ------------------> +---------------------------------+
 *                            |                                 |
 *                            |         Empty Memory (*)        |
 *                            |                                 |
 *                            +---------------------------------+ 0xFB000000
 *                            |   Cur. Page Table (Kern, RW)    | RW/-- PTSIZE  = 4M, 拜 VPT 所赐,内容就是一级页表的内容
 *     VPT -----------------> +---------------------------------+ 0xFAC00000    = 4012M = 1003/1024 * 4096 自映射一级页表起始
 *                            |        Invalid Memory (*)       | --/--
 *     KERNTOP -------------> +---------------------------------+ 0xF8000000    = 3968M
 *                            |                                 |
 *                            |    Remapped Physical Memory     | RW/-- KMEMSIZE = 896MB, ucore 最大支持的物理内存大小
 *                            |                                 |               <=  maxpa = min{maxpa, KMEMSIZE},实际管理的物理内存大小
 *                            |                                 |               <= .end, 0xC015D324 实际内核文件加载到内存中的虚边界
 *     KERNBASE ------------> +---------------------------------+ 0xC0000000    <= .text 起始 = 3072M
 *                            |        Invalid Memory (*)       | --/--
 *     USERTOP -------------> +---------------------------------+ 0xB0000000
 *                            |           User stack            |
 *                            +---------------------------------+
 *                            |                                 |
 *                            :                                 :
 *                            |         ~~~~~~~~~~~~~~~~        |
 *                            :                                 :
 *                            |                                 |
 *                            ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *                            |       User Program & Heap       |
 *     UTEXT ---------------> +---------------------------------+ 0x00800000
 *                            |        Invalid Memory (*)       | --/--
 *                            |  - - - - - - - - - - - - - - -  |
 *                            |    User STAB Data (optional)    |
 *     USERBASE, USTAB------> +---------------------------------+ 0x00200000
 *                            |        Invalid Memory (*)       | --/--
 *     0 -------------------> +---------------------------------+ 0x00000000
 * (*) Note: The kernel ensures that "Invalid Memory" is *never* mapped.
 *     "Empty Memory" is normally unmapped, but user programs may map pages
 *     there if desired.
 *
 * */

/* 所有可管理的物理地址空间的映射: [0,KMEMSIZE)->[KERNBASE,KERNBASE+KMEMSIZE) */
#define KERNBASE            0xC0000000                  // 内核运行态虚拟地址 = 3072M
#define KMEMSIZE            0x38000000                  // 内核可管理物理内存空间上限 = 896MB,可调整至最小4M,即一个一级页表管理的容量
//#define KMEMSIZE            0x00400000                  // 最小 4M,即 1 个 page entry 映射的字节数
//#define KMEMSIZE            0x00800000                  // 8M
#define KERNTOP             (KERNBASE + KMEMSIZE)

#define VPT                 0xFAC00000                  // = 4012M = 1003/1024 * 4096 自映射起始点

#define KSTACKPAGE          2                           // # of pages in kernel stack
#define KSTACKSIZE          (KSTACKPAGE * PGSIZE)       // sizeof kernel stack

#define USERTOP             0xB0000000
#define USTACKTOP           USERTOP
#define USTACKPAGE          256                         // # of pages in user stack
#define USTACKSIZE          (USTACKPAGE * PGSIZE)       // sizeof user stack

#define USERBASE            0x00200000
#define UTEXT               0x00800000                  // where user programs generally begin
#define USTAB               USERBASE                    // the location of the user STABS data structure

/**
 * 判断所给的地址区间是否属于用户区,即位于 USERBASE 和 USERTOP 之内.
 */ 
#define USER_ACCESS(start, end)                     \
(USERBASE <= (start) && (start) < (end) && (end) <= USERTOP)

/**
 * 判断所给的地址区间是否属于内核区,即位于 KERNBASE 和 KERNTOP 之内.
 */ 
#define KERN_ACCESS(start, end)                     \
(KERNBASE <= (start) && (start) < (end) && (end) <= KERNTOP)

#ifndef __ASSEMBLER__

#include <defs.h>
#include <atomic.h>
#include <list.h>

typedef uintptr_t pte_t;
typedef uintptr_t pde_t;
typedef pte_t swap_entry_t; //the pte can also be a swap entry

// some constants for bios interrupt 15h AX = 0xE820
#define E820MAX             20      // number of entries in E820MAP
#define E820_ARM            1       // address range memory
#define E820_ARR            2       // address range reserved

/**
 * 当请求BIOS中断号15H，并且置操作码AX=E820H的时候，
 * BIOS就会向调用者报告可用的物理地址区间等信息，e820由此得名。
 * e820map: 物理内存探测器
 */ 
struct e820map {
    int nr_map;
    struct {
        uint64_t addr;
        uint64_t size;
        uint32_t type;
    } __attribute__((packed)) map[E820MAX];
};

/**
 * 内存页描述符结构,用于描述物理地址.
 * kern/mm/pmm.h 中有很多函数实现 page 与 pa 或 va 的互转.
 */ 
struct Page {
    int ref;                        // 页引用计数
    uint32_t flags;                 // array of flags that describe the status of the page frame
    unsigned int property;          // used in buddy system, stores the order (the X in 2^X) of the continuous memory block
    int zone_num;                   // used in buddy system, the No. of zone which the page belongs to
    list_entry_t page_link;         // free list link
    list_entry_t pra_page_link;     // used for pra (page replace algorithm)
    uintptr_t pra_vaddr;            // used for pra (page replace algorithm)
};

/* Flags describing the status of a page frame */
#define PG_reserved                 0       // the page descriptor is reserved for kernel or unusable
#define PG_property                 1       // the member 'property' is valid

#define SetPageReserved(page)       set_bit(PG_reserved, &((page)->flags)) // 标记为从不换出
#define ClearPageReserved(page)     clear_bit(PG_reserved, &((page)->flags))
#define PageReserved(page)          test_bit(PG_reserved, &((page)->flags))
#define SetPageProperty(page)       set_bit(PG_property, &((page)->flags))
#define ClearPageProperty(page)     clear_bit(PG_property, &((page)->flags))
#define PageProperty(page)          test_bit(PG_property, &((page)->flags))

// convert list entry to page
#define le2page(le, member)                 \
    to_struct((le), struct Page, member)

/* free_area_t - maintains a doubly linked list to record free (unused) pages */
typedef struct {
    list_entry_t free_list;         // the list header
    unsigned int nr_free;           // # of free pages in this free list
} free_area_t;

/* for slab style kmalloc */
//#define PG_slab                     2       // page frame is included in a slab
//#define SetPageSlab(page)           set_bit(PG_slab, &((page)->flags))
//#define ClearPageSlab(page)         clear_bit(PG_slab, &((page)->flags))
//#define PageSlab(page)              test_bit(PG_slab, &((page)->flags))

#endif /* !__ASSEMBLER__ */

#endif /* !__KERN_MM_MEMLAYOUT_H__ */


