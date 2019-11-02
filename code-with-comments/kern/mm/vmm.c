#include <vmm.h>
#include <sync.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <error.h>
#include <pmm.h>
#include <x86.h>
#include <swap.h>
#include <kmalloc.h>
#include <kdebug.h>

/* 
  vmm design include two parts: mm_struct (mm) & vma_struct (vma)
  mm 是使用同一 PDT 的连续虚拟内存区域(vma)的管理器.
  链表关系: mm<->vma<->vma<->vma...
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

static void check_vmm(void);
static void check_vma_struct(void);
static void check_pgfault(void);

// mm_create -  alloc a mm_struct & initialize it.
struct mm_struct *
mm_create(void) {
    struct mm_struct *mm = kmalloc(sizeof(struct mm_struct));

    if (mm != NULL) {
        list_init(&(mm->mmap_list));
        mm->mmap_cache = NULL;
        mm->pgdir = NULL;
        mm->map_count = 0;

        if (swap_init_ok) swap_init_mm(mm);
        else mm->sm_priv = NULL;
        
        set_mm_count(mm, 0);
        sem_init(&(mm->mm_sem), 1);
    }    
    LOG("已初始化默认 mm_struct.\n");
    return mm;
}

// vma_create - 分配一个 vma_struct 并初始化. (addr range: vm_start~vm_end)
struct vma_struct *
vma_create(uintptr_t vm_start, uintptr_t vm_end, uint32_t vm_flags) {
    struct vma_struct *vma = kmalloc(sizeof(struct vma_struct));

    if (vma != NULL) {
        vma->vm_start = vm_start;
        vma->vm_end = vm_end;
        vma->vm_flags = vm_flags;
    }
    //LOG("创建了一个 vma. vm_start: %lu, vm_end: %lu, vm_flags: %u\n",vm_start, vm_end, vm_flags);
    return vma;
}


// find_vma - find a vma  (vma->vm_start <= addr <= vma_vm_end)
struct vma_struct *
find_vma(struct mm_struct *mm, uintptr_t addr) {
    struct vma_struct *vma = NULL;
    if (mm != NULL) {
        vma = mm->mmap_cache;   // 起点理论上随便(双向链表),这里可能考虑局部性所以选择了 mmap_cache
        if (!(vma != NULL && vma->vm_start <= addr && vma->vm_end > addr)) {
                bool found = 0;
                list_entry_t *list = &(mm->mmap_list), *le = list;
                while ((le = list_next(le)) != list) {
                    vma = le2vma(le, list_link);
                    if (vma->vm_start<=addr && addr < vma->vm_end) {
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    vma = NULL;
                }
        }
        if (vma != NULL) {
            mm->mmap_cache = vma;
        }
    }
    return vma;
}


// check_vma_overlap - check if vma1 overlaps vma2 ?
static inline void
check_vma_overlap(struct vma_struct *prev, struct vma_struct *next) {
    assert(prev->vm_start < prev->vm_end);
    assert(prev->vm_end <= next->vm_start);
    assert(next->vm_start < next->vm_end);
}


// insert_vma_struct -insert vma in mm's list link
void
insert_vma_struct(struct mm_struct *mm, struct vma_struct *vma) {
    assert(vma->vm_start < vma->vm_end);
    list_entry_t *list = &(mm->mmap_list);
    list_entry_t *le_prev = list, *le_next;

        list_entry_t *le = list;
        while ((le = list_next(le)) != list) {
            struct vma_struct *mmap_prev = le2vma(le, list_link);
            if (mmap_prev->vm_start > vma->vm_start) {
                break;
            }
            le_prev = le;
        }

    le_next = list_next(le_prev);

    /* check overlap */
    if (le_prev != list) {
        check_vma_overlap(le2vma(le_prev, list_link), vma);
    }
    if (le_next != list) {
        check_vma_overlap(vma, le2vma(le_next, list_link));
    }

    vma->vm_mm = mm;
    list_add_after(le_prev, &(vma->list_link));

    mm->map_count ++;
}

// mm_destroy - free mm and mm internal fields
void
mm_destroy(struct mm_struct *mm) {
    assert(mm_count(mm) == 0);

    list_entry_t *list = &(mm->mmap_list), *le;
    while ((le = list_next(list)) != list) {
        list_del(le);
        kfree(le2vma(le, list_link));  //kfree vma        
    }
    kfree(mm); //kfree mm
    mm=NULL;
}
/**
 * 
 * 创建新的 vma 链接到给定的 mm 上.新的 vma 的参数由函数参数指定.
 * 
 */ 
int
mm_map(struct mm_struct *mm, uintptr_t addr, size_t len, uint32_t vm_flags,
       struct vma_struct **vma_store) {
    // 新的 vma 的 start 由 addr 向下取PGSIZE 整数倍,
    //            end   由(addr+len)向上取整 PGSIZE 整数倍.
    uintptr_t start = ROUNDDOWN(addr, PGSIZE), end = ROUNDUP(addr + len, PGSIZE);
    if (!USER_ACCESS(start, end)) {
        return -E_INVAL;
    }

    assert(mm != NULL);

    int ret = -E_INVAL;

    struct vma_struct *vma;
    if ((vma = find_vma(mm, start)) != NULL && end > vma->vm_start) {
        goto out;
    }
    ret = -E_NO_MEM;

    if ((vma = vma_create(start, end, vm_flags)) == NULL) {
        goto out;
    }
    insert_vma_struct(mm, vma);
    if (vma_store != NULL) {
        *vma_store = vma;
    }
    ret = 0;

out:
    return ret;
}

int
dup_mmap(struct mm_struct *to, struct mm_struct *from) {
    LOG_TAB("\tdup_mmap:\n");
    assert(to != NULL && from != NULL);
    list_entry_t *list = &(from->mmap_list), *le = list;
    while ((le = list_prev(le)) != list) {
        struct vma_struct *vma, *nvma;
        vma = le2vma(le, list_link);
        nvma = vma_create(vma->vm_start, vma->vm_end, vma->vm_flags);
        if (nvma == NULL) {
            return -E_NO_MEM;
        }

        insert_vma_struct(to, nvma);

        bool share = 0;
        if (copy_range(to->pgdir, from->pgdir, vma->vm_start, vma->vm_end, share) != 0) {
            return -E_NO_MEM;
        }
    }
    return 0;
}

void
exit_mmap(struct mm_struct *mm) {
    assert(mm != NULL && mm_count(mm) == 0);
    pde_t *pgdir = mm->pgdir;
    list_entry_t *list = &(mm->mmap_list), *le = list;
    while ((le = list_next(le)) != list) {
        struct vma_struct *vma = le2vma(le, list_link);
        unmap_range(pgdir, vma->vm_start, vma->vm_end);
    }
    while ((le = list_next(le)) != list) {
        struct vma_struct *vma = le2vma(le, list_link);
        exit_range(pgdir, vma->vm_start, vma->vm_end);
    }
}

bool
copy_from_user(struct mm_struct *mm, void *dst, const void *src, size_t len, bool writable) {
    if (!user_mem_check(mm, (uintptr_t)src, len, writable)) {
        return 0;
    }
    memcpy(dst, src, len);
    return 1;
}

/**
 * 
 * 把数据从内核区复制到用户区 由于调用此函数的数据在栈上,所以需要复制一份返回给用户
 * 考虑目标用户区地址合法性
 */ 
bool
copy_to_user(struct mm_struct *mm, void *dst, const void *src, size_t len) {
    if (!user_mem_check(mm, (uintptr_t)dst, len, 1)) {
        return 0;
    }
    memcpy(dst, src, len);
    return 1;
}

// vmm_init - 初始化虚拟内存管理模块
//          - now just call check_vmm to check correctness of vmm
void
vmm_init(void) {
    LOG_LINE("测试开始:虚拟内存管理模块(vmm)");
    check_vmm();
    LOG_LINE("测试结束:虚拟内存管理模块(vmm)");

}

// check_vmm - check correctness of vmm
static void
check_vmm(void) {
    check_vma_struct();
    check_pgfault();

    LOG("check_vmm() succeeded.\n");
}

// https://chyyuu.gitbooks.io/ucore_os_docs/content/lab3/lab3_5_2_page_swapping_principles.html
static void
check_vma_struct(void) {
    size_t nr_free_pages_store = nr_free_pages();
    LOG("   开始测试 vma结构.\n");
    LOG("   测试点: 是否正确把 vma插入到 mm,是否有重叠,是否能从 mm 找到某个地址所在的 vma.\n");

    LOG("   当前空闲 page 数:%d\n",nr_free_pages_store);

    struct mm_struct *mm = mm_create();
    assert(mm != NULL);

    int step1 = 10, step2 = step1 * 10;
    LOG("   从 5 到 50, 以及从 55 到 500,每隔 5 个字节创建一个 vma, 长度是 2;全部插入到 mm 链表中.\n");
    int i;
    for (i = step1; i >= 1; i --) {
        struct vma_struct *vma = vma_create(i * 5, i * 5 + 2, 0);
        assert(vma != NULL);
        insert_vma_struct(mm, vma);
    }

    for (i = step1 + 1; i <= step2; i ++) {
        struct vma_struct *vma = vma_create(i * 5, i * 5 + 2, 0);
        assert(vma != NULL);
        insert_vma_struct(mm, vma);
    }
    LOG("   插入结束,mm 所维护的 vma 数量为%d\n",mm->map_count);

    list_entry_t *le = list_next(&(mm->mmap_list));

    for (i = 1; i <= step2; i ++) {
        assert(le != &(mm->mmap_list));
        struct vma_struct *mmap = le2vma(le, list_link);
        assert(mmap->vm_start == i * 5 && mmap->vm_end == i * 5 + 2);
        le = list_next(le);
    }

    for (i = 5; i <= 5 * step2; i +=5) {
        struct vma_struct *vma1 = find_vma(mm, i);
        assert(vma1 != NULL);
        struct vma_struct *vma2 = find_vma(mm, i+1);
        assert(vma2 != NULL);
        struct vma_struct *vma3 = find_vma(mm, i+2);
        assert(vma3 == NULL);
        struct vma_struct *vma4 = find_vma(mm, i+3);
        assert(vma4 == NULL);
        struct vma_struct *vma5 = find_vma(mm, i+4);
        assert(vma5 == NULL);

        assert(vma1->vm_start == i  && vma1->vm_end == i  + 2);
        assert(vma2->vm_start == i  && vma2->vm_end == i  + 2);
    }

    for (i =4; i>=0; i--) {
        struct vma_struct *vma_below_5= find_vma(mm,i);
        if (vma_below_5 != NULL ) {
           LOG("vma_below_5: i %x, start %x, end %x\n",i, vma_below_5->vm_start, vma_below_5->vm_end); 
        }
        assert(vma_below_5 == NULL);
    }

    mm_destroy(mm);

  //  assert(nr_free_pages_store == nr_free_pages());

    LOG("check_vma_struct() succeeded!\n");
}

struct mm_struct *check_mm_struct;  // 当前ucore 认为的合法虚拟内存空间集合

// check_pgfault - pgfault handler 测试函数
static void
check_pgfault(void) {
    LOG_LINE("开始测试: page fault");
    LOG("   当前页表状态:\n");
    print_pgdir();

    size_t nr_free_pages_store = nr_free_pages();
    LOG("初始可用页数: %u.\n",nr_free_pages_store);
    check_mm_struct = mm_create();
    assert(check_mm_struct != NULL);

    struct mm_struct *mm = check_mm_struct;
    pde_t *pgdir = mm->pgdir = boot_pgdir;
    LOG("此mm_struct的一级页表地址是: 0x%08lx.\n",pgdir);
    assert(pgdir[0] == 0);


    LOG("   创建一个页目录项对应大小的 vma(1024*4KB=4MB), 物理地址区间是[0,4M), flag=write,并插入到 mm 中.\n",PTSIZE, PTSIZE/1024/1024);
    struct vma_struct *vma = vma_create(0, PTSIZE, VM_WRITE);
    assert(vma != NULL);

    insert_vma_struct(mm, vma);

    uintptr_t addr = 0x100; // = 256B
    assert(find_vma(mm, addr) == vma);

    LOG("   开始对此区域进行写入.\n");
    LOG("预计缺页情况:");
    int i, sum = 0;
    for (i = 0; i < 100; i ++) {
        *(char *)(addr + i) = i;
        sum += i;
    }
    for (i = 0; i < 100; i ++) {
        sum -= *(char *)(addr + i);
    }
    assert(sum == 0);
    LOG("   写入完毕,即将移除页表项\n");
    page_remove(pgdir, ROUNDDOWN(addr, PGSIZE));
    LOG("   已移除addr所属内存页的页表项\n");

    free_page(pde2page(pgdir[0]));
    pgdir[0] = 0;

    mm->pgdir = NULL;
    mm_destroy(mm);
    check_mm_struct = NULL;

    assert(nr_free_pages_store == nr_free_pages());

    LOG_LINE("测试通过: page fault");
}
//page fault number
volatile unsigned int pgfault_num=0;

/**
 * do_pgfault - page fault 中断处理函数,用于处理缺页异常.
 * 
 * see also: pgfault_handler
 * 
 * @mm         : 使用同一 PDT 的 vma 集合的控制块
 * @error_code : 由 x86 硬件设置的page fault 错误码,在trapframe->tf_err 中记录
 * @addr       : 出发内存访问异常的地址.(也就是CR 寄存器的值)
 *
 * CALL GRAPH: trap--> trap_dispatch-->pgfault_handler-->do_pgfault
 * 
 * 处理器提供了两类信息用于缺页异常的诊断和恢复:
 * 
 *   (1) CR2 的值. 
 *       处理器把触发缺页异常的地址加载到 CR2 寄存器里. do_pgfault可以使用此地址定位相应的 page directory
 *       和 page table entry.
 *   (2) 内核栈上的错误码.
 *       缺页异常的错误码的格式比较特殊,跟其他错误的格式有所不同.
 *       错误码包含三类信息:
 *         --  P flag   (bit 0) 表示异常原因是 not-present page(0) 还是由于权限问题/使用了保留位
 *                              indicates whether the exception was due to a not-present page (0)
 *                              or to either an access rights violation or the use of a reserved bit (1).
 *         --  W/R flag (bit 1) 表示触发异常的动作是读(0)还是写(1).
 *         --  U/S flag (bit 2) 表示当异常发生时,处理器正处于用户态(1)还是内核态(0).
 */
int
do_pgfault(struct mm_struct *mm, uint32_t error_code, uintptr_t addr) {
    LOG("do_pgfault begin:\n");
    LOG("page fault信息:\n");
    LOG_TAB("mm: 0x%08lx\n", mm);
    LOG_TAB("地址: 0x%08lx\n", addr);
    LOG("此异常错误码: 0x%08lx\n",error_code);

    int ret = -E_INVAL;
    // 找到此地址所在的 vma
    
    struct vma_struct *vma = find_vma(mm, addr);
    LOG("已通过find_vma获取此地址在 mm_struct 中对应的 vma.\n");

    pgfault_num++;
    // 若未找到 vma,或找到的 vma 的起始地址不正常(大于 addr),则不合法
    if (vma == NULL || vma->vm_start > addr) {
        LOG("not valid addr %x, and  can not find it in vma\n", addr);
        goto failed;
    }
    // 考察错误码,即 page fault 分类
    // 错误码格式参考手册 6.15 节 p6-40 
    switch (error_code & 3) {
    default:
            /* error code flag : default is 3 ( W/R=1, P=1): write, present */
    case 2: 
        /* 错误码: 写入缺页地址 : (W/R=1, P=0) */
        if (!(vma->vm_flags & VM_WRITE)) {// 属性校验结果异常
            LOG("do_pgfault failed: error code flag = write AND not present, but the addr's vma cannot write\n");
            goto failed;
        }
        break;
    case 1: /* 错误码: 读取存在的地址 : (W/R=0, P=1): read, present */
        // 读取已存在页不应该出现错误码
        LOG("do_pgfault failed: error code flag = read AND present\n");
        goto failed;
    case 0: /* 错误码:读取缺页地址 (W/R=0, P=0): read, not present */
        if (!(vma->vm_flags & (VM_READ | VM_EXEC))) {// 属性校验结果异常
            LOG("do_pgfault failed: error code flag = read AND not present, but the addr's vma cannot read or exec\n");
            goto failed;
        }
    }
    /* IF (write an existed addr ) OR
     *    (write an non_existed addr && addr is writable) OR
     *    (read  an non_existed addr && addr is readable)
     * THEN
     *    continue process
     * 
     */
    uint32_t perm = PTE_U;
    if (vma->vm_flags & VM_WRITE) {
        perm |= PTE_W;
    }
    addr = ROUNDDOWN(addr, PGSIZE);

    ret = -E_NO_MEM;

    pte_t *ptep=NULL;
    /*
    * MACROs or Functions:
    *   get_pte : get an pte and return the kernel virtual address of this pte for la
    *             if the PT contians this pte didn't exist, alloc a page for PT (notice the 3th parameter '1')
    *   pgdir_alloc_page : call alloc_page & page_insert functions to allocate a page size memory & setup
    *             an addr map pa<--->la with linear address la and the PDT pgdir
    * DEFINES:
    *   VM_WRITE  : If vma->vm_flags & VM_WRITE == 1/0, then the vma is writable/non writable
    *   PTE_W           0x002                   // page table/directory entry flags bit : Writeable
    *   PTE_U           0x004                   // page table/directory entry flags bit : User can access
    * VARIABLES:
    *   mm->pgdir : the PDT of these vma
    *
    */
#if 0
    /*LAB3 EXERCISE 1: YOUR CODE*/
    ptep = ???              //(1) try to find a pte, if pte's PT(Page Table) isn't existed, then create a PT.
    if (*ptep == 0) {
                            //(2) if the phy addr isn't exist, then alloc a page & map the phy addr with logical addr

    }
    else {
    /*LAB3 EXERCISE 2: YOUR CODE
    * Now we think this pte is a  swap entry, we should load data from disk to a page with phy addr,
    * and map the phy addr with logical addr, trigger swap manager to record the access situation of this page.
    *
    *  Some Useful MACROs and DEFINEs, you can use them in below implementation.
    *  MACROs or Functions:
    *    swap_in(mm, addr, &page) : alloc a memory page, then according to the swap entry in PTE for addr,
    *                               find the addr of disk page, read the content of disk page into this memroy page
    *    page_insert ： build the map of phy addr of an Page with the linear addr la
    *    swap_map_swappable ： set the page swappable
    */
    /*
     * LAB5 CHALLENGE ( the implmentation Copy on Write)
		There are 2 situlations when code comes here.
		  1) *ptep & PTE_P == 1, it means one process try to write a readonly page. 
		     If the vma includes this addr is writable, then we can set the page writable by rewrite the *ptep.
		     This method could be used to implement the Copy on Write (COW) thchnology(a fast fork process method).
		  2) *ptep & PTE_P == 0 & but *ptep!=0, it means this pte is a  swap entry.
		     We should add the LAB3's results here.
     */
        if(swap_init_ok) {
            struct Page *page=NULL;
                                    //(1）According to the mm AND addr, try to load the content of right disk page
                                    //    into the memory which page managed.
                                    //(2) According to the mm, addr AND page, setup the map of phy addr <---> logical addr
                                    //(3) make the page swappable.
                                    //(4) [NOTICE]: you myabe need to update your lab3's implementation for LAB5's normal execution.
        }
        else {
            LOG("no swap_init_ok but ptep is %x, failed\n",*ptep);
            goto failed;
        }
   }
#endif
    // 1. 加载地址 addr 在所属 mm 中对应的的二级页表项,不存在则创建
    LOG("开始恢复缺页异常: 建立对应虚拟地址的页表即可.\n");
    if ((ptep = get_pte(mm->pgdir, addr, 1)) == NULL) {
        LOG("get_pte in do_pgfault failed\n");
        goto failed;
    }
    LOG("已得到此地址的页表项\n");
    if (*ptep == 0) { // 1. 若页表项中物理地址的值为空,则分配一个物理页并将 addr 映射过去
        if (pgdir_alloc_page(mm->pgdir, addr, perm) == NULL) {
            LOG("pgdir_alloc_page in do_pgfault failed\n");
            goto failed;
        }
    }
    else {
        struct Page *page=NULL;
        LOG("do pgfault: ptep %x, pte %x\n",ptep, *ptep);
        if (*ptep & PTE_P) {
            //if process write to this existed readonly page (PTE_P means existed), then should be here now.
            //we can implement the delayed memory space copy for fork child process (AKA copy on write, COW).
            //we didn't implement now, we will do it in future.
            panic("error write a non-writable pte");
            //page = pte2page(*ptep);
        } else{
           // if this pte is a swap entry, then load data from disk to a page with phy addr
           // and call page_insert to map the phy addr with logical addr
           if(swap_init_ok) {               
               if ((ret = swap_in(mm, addr, &page)) != 0) {
                   LOG("swap_in in do_pgfault failed\n");
                   goto failed;
               }    

           }  
           else {
            LOG("no swap_init_ok but ptep is %x, failed\n",*ptep);
            goto failed;
           }
       } 
       // 安装页表项
       page_insert(mm->pgdir, page, addr, perm);
       swap_map_swappable(mm, addr, page, 1);
       page->pra_vaddr = addr;
   }
   ret = 0;
failed:
    return ret;
}

/**
 * 按对于给定的mm,地址区间和读写属性,检查地址是否合法.
 * 防止用户进程向内核传递指向内核空间的指针。
 * 
 * 合法的定义:调用此函数有两种情况:
 *      1) 内核进程调用,此时 mm 为 NULL,则检查地址是否处于内核空间;
 *      2) 内核态的用户进程调用,此时 mm 不为 NULL,则检查是否处于用户区间,且是否符合 mm 属性.读0 写1
 *                      
 */ 
bool
user_mem_check(struct mm_struct *mm, uintptr_t addr, size_t len, bool write) {
    if (mm != NULL) {
    // 用户进程
        // 1. 值区间检查
        if (!USER_ACCESS(addr, addr + len)) {
            return 0;
        }
        // 2. 属性检查
        struct vma_struct *vma;
        uintptr_t start = addr, end = addr + len;
        while (start < end) {
            if ((vma = find_vma(mm, start)) == NULL || start < vma->vm_start) {
                return 0;
            }
            if (!(vma->vm_flags & ((write) ? VM_WRITE : VM_READ))) {
                return 0;
            }
            if (write && (vma->vm_flags & VM_STACK)) {
                if (start < vma->vm_start + PGSIZE) { //check stack start & size
                    return 0;
                }
            }
            start = vma->vm_end;
        }
        return 1;
    }
    // 内核进程
    return KERN_ACCESS(addr, addr + len);
}
/**
 * 带合法性检测的字符串复制函数
 * 
 * 合法性定义: 从src到 src+maxn 必须可读
 */ 
bool
copy_string(struct mm_struct *mm, char *dst, const char *src, size_t maxn) {
    size_t alen, part = ROUNDDOWN((uintptr_t)src + PGSIZE, PGSIZE) - (uintptr_t)src;
    while (1) {
        if (part > maxn) {
            part = maxn;
        }
        // src 必须可读
        if (!user_mem_check(mm, (uintptr_t)src, part, 0)) {
            return 0;
        }
        if ((alen = strnlen(src, part)) < part) {
            memcpy(dst, src, alen + 1);
            return 1;
        }
        if (part == maxn) {
            return 0;
        }
        memcpy(dst, src, part);
        dst += part, src += part, maxn -= part;
        part = PGSIZE;
    }
}
