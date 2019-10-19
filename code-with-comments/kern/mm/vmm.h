#ifndef __KERN_MM_VMM_H__
#define __KERN_MM_VMM_H__

#include <defs.h>
#include <list.h>
#include <memlayout.h>
#include <sync.h>
#include <proc.h>
#include <sem.h>


/*

vmm : virtual memory manager, 虚拟内存管理
mm  : memory manager, 使用同一 PDT 的内存管理集合
vma : virtual memory area, 连续虚拟内存空间, 通过线性链表和红黑树组织起来.

---------------
  mm related functions:
   golbal functions
     struct mm_struct * mm_create(void)
     void mm_destroy(struct mm_struct *mm)
     int do_pgfault(struct mm_struct *mm, uint32_t error_code, uintptr_t addr)
--------------
  vma related functions:
   global functions
     struct vma_struct * vma_create (uintptr_t vm_start, uintptr_t vm_end,...)
     void insert_vma_struct(struct mm_struct *mm, struct vma_struct *vma)
     struct vma_struct * find_vma(struct mm_struct *mm, uintptr_t addr)
   local functions
     inline void check_vma_overlap(struct vma_struct *prev, struct vma_struct *next)
---------------
   check correctness functions
     void check_vmm(void);
     void check_vma_struct(void);
     void check_pgfault(void);
*/

//pre define
struct mm_struct;

/**
 * 虚拟连续内存空间 virtual continuous memory area(vma)
 * 
 * 维护地址空间的属性,主要用于处理 page fault.
 * 
 * 地址区间有哪些属性? 读/写/执行
 */ 
struct vma_struct {
    struct mm_struct *vm_mm; // 当前 vma 所在的 mm     | the set of vma using the same PDT 
    uintptr_t vm_start;      // start addr of vma    
    uintptr_t vm_end;        // end addr of vma
    uint32_t vm_flags;       // flags of vma
    list_entry_t list_link;  // vma 链表,按基址排序      | linear list link which sorted by start addr of vma
};

#define le2vma(le, member)                  \
    to_struct((le), struct vma_struct, member)

// vma 属性
#define VM_READ                 0x00000001
#define VM_WRITE                0x00000002
#define VM_EXEC                 0x00000004
#define VM_STACK                0x00000008

/**
 * 面向处理器的虚拟内存状态维护器.
 * 1) 维护了 vma 链表,标识可访问内存区域和权限.
 * 2) 维护了可交换页链表, 用于换出调度.
 * 
 * mm_struct 是内存访问的核心结构, 被进程控制块维护.
 * 
 * 当 cpu 访问某个进程的某个地址va时,
 *  0. 进程创建时 cpu 已获取此 mm 维护的页表基址
 *  1. 获取此进程的 mm
 *  2. 通过 mm 获取此地址的 vma.如果没有找到, 或者权限不正确,就会触发 segment fault;如果找到且权限正确则继续.
 *  3. 根据此一级页表基址和 va,得到一级页表条目
 *  4.1 若条目存在位是 0,则触发 page fault;
 *  4.2 若条目存在位是 1,但二级页表存在位是 0,也触发 page fault.
 *  4.3 若一直有存在位,则正常访问.
 *  5 处理 page fault 是另一码事了
 *      
 */ 
struct mm_struct {
    list_entry_t mmap_list;        // vma 链表,按基址排序                       | linear list link which sorted by start addr of vma 
    struct vma_struct *mmap_cache; // 当前正在使用的 vma                        | current accessed vma, used for speed purpose
    pde_t *pgdir;                  // the PDT of these vma
    int map_count;                 // vma 个数                                 | the count of these vma
    void *sm_priv;                 // 当前进程的置换链表头                        | the private data for swap manager 
    int mm_count;                  // 共享同一 mm 的进程数量
    semaphore_t mm_sem;            // 互斥量,用于在 dup_mmap 函数中复制 mm 
    int locked_by;                 // the lock owner process's pid
};

struct vma_struct *find_vma(struct mm_struct *mm, uintptr_t addr);
struct vma_struct *vma_create(uintptr_t vm_start, uintptr_t vm_end, uint32_t vm_flags);
void insert_vma_struct(struct mm_struct *mm, struct vma_struct *vma);

struct mm_struct *mm_create(void);
void mm_destroy(struct mm_struct *mm);

void vmm_init(void);
int mm_map(struct mm_struct *mm, uintptr_t addr, size_t len, uint32_t vm_flags,
           struct vma_struct **vma_store);
int do_pgfault(struct mm_struct *mm, uint32_t error_code, uintptr_t addr);

int mm_unmap(struct mm_struct *mm, uintptr_t addr, size_t len);
int dup_mmap(struct mm_struct *to, struct mm_struct *from);
void exit_mmap(struct mm_struct *mm);
uintptr_t get_unmapped_area(struct mm_struct *mm, size_t len);
int mm_brk(struct mm_struct *mm, uintptr_t addr, size_t len);

extern volatile unsigned int pgfault_num;
extern struct mm_struct *check_mm_struct;

bool user_mem_check(struct mm_struct *mm, uintptr_t start, size_t len, bool write);
bool copy_from_user(struct mm_struct *mm, void *dst, const void *src, size_t len, bool writable);
bool copy_to_user(struct mm_struct *mm, void *dst, const void *src, size_t len);
bool copy_string(struct mm_struct *mm, char *dst, const char *src, size_t maxn);

static inline int
mm_count(struct mm_struct *mm) {
    return mm->mm_count;
}

static inline void
set_mm_count(struct mm_struct *mm, int val) {
    mm->mm_count = val;
}

static inline int
mm_count_inc(struct mm_struct *mm) {
    mm->mm_count += 1;
    return mm->mm_count;
}

static inline int
mm_count_dec(struct mm_struct *mm) {
    mm->mm_count -= 1;
    return mm->mm_count;
}

static inline void
lock_mm(struct mm_struct *mm) {
    if (mm != NULL) {
        down(&(mm->mm_sem));
        if (current != NULL) {
            mm->locked_by = current->pid;
        }
    }
}

static inline void
unlock_mm(struct mm_struct *mm) {
    if (mm != NULL) {
        up(&(mm->mm_sem));
        mm->locked_by = 0;
    }
}

#endif /* !__KERN_MM_VMM_H__ */

