#include <defs.h>
#include <x86.h>
#include <stdio.h>
#include <string.h>
#include <mmu.h>
#include <memlayout.h>
#include <pmm.h>
#include <default_pmm.h>
#include <sync.h>
#include <error.h>
#include <swap.h>
#include <vmm.h>
#include <kmalloc.h>

/* *
 * 任务状态段: (Task State Segment)
 * 
 * 作用: 权限等级发生变化时,cpu 从TSS 段中加载内核栈基址和栈指针,以执行内核代码.
 * 
 * TSS 可以位于内存的任何位置.寄存器TR(Task Register) 的值作为段选择子,指向 GDT 中的 TSS描述符 的索引. 
 * 而 GDT 中的这一项中的一个字段指向内存中的 TSS.
 * 在 gdt_init 中对其进行初始化.
 *  - 在 GDT 中创建一个 TSS 描述符
 *  - 初始化内存中的 TSS
 *  - 设置 TR 寄存器的值(即段选择子)为GDT 中 TSS 描述符的索引.
 *
 * 当权限等级发生变化时,TSS 中用于指向新的 stack pointer 的字段会发生变化.
 * 在 ucore 中只用到其中的 SS0 和 ESP0.
 * 
 * CPL=0 时,
 *      - 栈段选择子(stack segment selector)的值由SS0维护;
 *      - ESP的值由 ESP0 维护.
 *
 * 当在保护模式发生中断时,x86cpu 会在 TSS 段中寻找 SS0 和 ESP0 值,并分别加载到 SS 和 ESP 寄存器.
 * 
 * */
static struct taskstate ts = {0};

// virtual address of physicall page array
struct Page *pages;
// 需要管理的物理内存页数
size_t npage = 0;

// boot-time 一级页表的虚拟地址
extern pde_t __boot_pgdir;
pde_t *boot_pgdir = &__boot_pgdir;

// boot-time 一级页表的物理地址
uintptr_t boot_cr3;

// physical memory management
const struct pmm_manager *pmm_manager;

/* *
 * The page directory entry corresponding to the virtual address range
 * [VPT, VPT + PTSIZE) points to the page directory itself. Thus, the page
 * directory is treated as a page table as well as a page directory.
 *
 * One result of treating the page directory as a page table is that all PTEs
 * can be accessed though a "virtual page table" at virtual address VPT. And the
 * PTE for number n is stored in vpt[n].
 *
 * A second consequence is that the contents of the current page directory will
 * always available at virtual address PGADDR(PDX(VPT), PDX(VPT), 0), to which
 * vpd is set bellow.
 * */
pte_t * const vpt = (pte_t *)VPT;
pde_t * const vpd = (pde_t *)PGADDR(PDX(VPT), PDX(VPT), 0);

/* *
 * Global Descriptor Table:
 *
 * To load the %ss register, the CPL must equal the DPL. Thus, we must duplicate the
 * segments for the user and the kernel. Defined as follows:
 *   - 0x0 :  unused (always faults -- for trapping NULL far pointers)
 *   - 0x8 :  kernel code segment
 *   - 0x10:  kernel data segment
 *   - 0x18:  user code segment
 *   - 0x20:  user data segment
 *   - 0x28:  defined for tss, initialized in gdt_init
 * 
 * 全局段描述符表(GDT)定义: 参考 Intel 手册 Figure 3-8, 3-10
 *  内核和用户的段描述符表是相同的,除了 DPL.
 * 
 * gdt起到保护作用.段寄存器(cs,ds))中维护着段选择子(和其他隐藏值),想要访问某个段,首先要通过 GDT 的权限验证,
 * 然后再通过其他偏移寄存器,基于 GDT 中的基址,找到最终的地址.
 * 
 * ucore 使用分页式的内存管理,GDT 把全部内存平铺,只起到权限限制作用.
 * 
 * */
static struct segdesc gdt[] = {
    // gdt[索引] = type | base | limit | dpl
    SEG_NULL,
    [SEG_KTEXT] = SEG(STA_X | STA_R, 0x0, 0xFFFFFFFF, DPL_KERNEL),
    [SEG_KDATA] = SEG(STA_W, 0x0, 0xFFFFFFFF, DPL_KERNEL),
    [SEG_UTEXT] = SEG(STA_X | STA_R, 0x0, 0xFFFFFFFF, DPL_USER),
    [SEG_UDATA] = SEG(STA_W, 0x0, 0xFFFFFFFF, DPL_USER),
    [SEG_TSS]   = SEG_NULL, //在 gdt_init 中初始化
};

static struct pseudodesc gdt_pd = {
    sizeof(gdt) - 1, (uintptr_t)gdt
};

static void check_alloc_page(void);
static void check_pgdir(void);
static void check_boot_pgdir(void);

/* *
 * lgdt - load the global descriptor table register and reset the
 * data/code segement registers for kernel.
 * */
static inline void
lgdt(struct pseudodesc *pd) {
    asm volatile ("lgdt (%0)" :: "r" (pd));
    asm volatile ("movw %%ax, %%gs" :: "a" (USER_DS));
    asm volatile ("movw %%ax, %%fs" :: "a" (USER_DS));
    asm volatile ("movw %%ax, %%es" :: "a" (KERNEL_DS));
    asm volatile ("movw %%ax, %%ds" :: "a" (KERNEL_DS));
    asm volatile ("movw %%ax, %%ss" :: "a" (KERNEL_DS));
    // reload cs
    asm volatile ("ljmp %0, $1f\n 1:\n" :: "i" (KERNEL_CS));
}

/* *
 * load_esp0 - change the ESP0 in default task state segment,
 * so that we can use different kernel stack when we trap frame
 * user to kernel.
 * 
 * 更新默认 TSS 的 ESP0.
 * 
 * 用于当用户进程trap 到 kernel 时指定内核栈顶
 * 
 * 1) 当特权态3 发生中断/异常/系统调用,则 cpu从特权态 3->0,则从当前进程栈顶压栈保存现场,更新 trapframe
 * 2) 当特权态0 发生,则 cpu 特权不变,则直接从 esp0 处压栈保存现场
 * */
void
load_esp0(uintptr_t esp0) {
    ts.ts_esp0 = esp0;
}

/**
 * 
 * 初始化默认全局段描述表和 TSS 段
 */ 
static void
gdt_init(void) {
    logline("初始化开始: 全局段描述表&TSS");
    // 设置内核栈基址和默认的 SS0
    load_esp0((uintptr_t)bootstacktop); // 设置TSS内核栈指针为bootstacktop
    ts.ts_ss0 = KERNEL_DS;              // 设置TSS内核栈段选择子(即内核栈基址),默认指向 GDT 中的 SEG_KDATA一项

    /**
     * 初始化 gdt 中的 TSS 字段:
     * gdt[索引] = type | base | limit | dpl
     */ 
    gdt[SEG_TSS] = SEGTSS(STS_T32A, (uintptr_t)&ts, sizeof(ts), DPL_KERNEL);

    // 更新所有段寄存器(的段选择子)的值
    lgdt(&gdt_pd);

    //  加载 TSS 段选择子到 TR 寄存器
    ltr(GD_TSS);
    logline("初始化完毕: 全局段描述表&TSS");
}

//init_pmm_manager - 配置一个内存管理器实例
static void
init_pmm_manager(void) {
    pmm_manager = &default_pmm_manager;
    pmm_manager->init();
    LOG_TAB("物理内存管理器实例- %s 初始化完毕.\n",pmm_manager->name);
}

//init_memmap - call pmm->init_memmap to build Page struct for free memory  
static void
init_memmap(struct Page *base, size_t n) {
    pmm_manager->init_memmap(base, n);
}

// 分配 n 个 page 的连续空间,封装缺页处理
struct Page *
alloc_pages(size_t n) {
    struct Page *page=NULL;
    bool intr_flag;
    
    while (1)
    {
         local_intr_save(intr_flag);
         {
              page = pmm_manager->alloc_pages(n);
         }
         local_intr_restore(intr_flag);

         if (page != NULL || n > 1 || swap_init_ok == 0) break;
         
         extern struct mm_struct *check_mm_struct;
         //LOG("page %x, call swap_out in alloc_pages %d\n",page, n);
         swap_out(check_mm_struct, n, 0);
    }
    //LOG("n %d,get page %x, No %d in alloc_pages\n",n,page,(page-pages));
    return page;
}

//free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory 
void
free_pages(struct Page *base, size_t n) {
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        pmm_manager->free_pages(base, n);
    }
    local_intr_restore(intr_flag);
}

//nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE) 
//of current free memory
size_t
nr_free_pages(void) {
    size_t ret;
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        ret = pmm_manager->nr_free_pages();
    }
    local_intr_restore(intr_flag);
    return ret;
}

/* pmm_init - initialize the physical memory management */
/**
 * 初始化内存管理
 * 
 */
static void
page_init(void) {
    logline("初始化开始:内存分页记账");
    LOG("目标: 根据探测得到的物理空间分布,初始化 pages 表格.\n\n");
    LOG_TAB("1. 确定 pages 基址. 通过 ends 向上取整得到, 位于 end 之上, 这意味着从此就已经突破了内核文件本身的内存空间,开始动态分配内存\n");
    LOG_TAB("2. 确定 page 数 npages,即 可管理内存的页数.\n");
    LOG_TAB("\t2.1 确定实际管理的物理内存大小maxpa.即向上取探测结果中的最大可用地址,但不得大于管理上限 KMEMSIZE. maxpa = min{maxpa, KMEMSIZE}.\n");
    LOG_TAB("\t2.2 npage = maxpa/PAGESIZE.\n");
    LOG_TAB("\t3. 确定可管理内存中每个空闲 page 的属性,便于日后的换入换出的调度; 加入到 freelist 中.\n\n");

    struct e820map *memmap = (struct e820map *)(0x8000 + KERNBASE);
    uint64_t maxpa = 0;     // 可管理物理空间上限,最大不超过 KMEMSIZE

    LOG("1) e820map信息报告:\n\n");
    LOG("   共探测到%d块内存区域:\n\n",memmap->nr_map);

    int i;
    for (i = 0; i < memmap->nr_map; i ++) {
        uint64_t range_begin = memmap->map[i].addr, range_end = range_begin + memmap->map[i].size;
        LOG_TAB("区间[%d]:[%08llx, %08llx], 大小: 0x%08llx Byte, 类型: %d, ",
                i, range_begin, range_end - 1, memmap->map[i].size, memmap->map[i].type);

        if (memmap->map[i].type == E820_ARM) {  // E820_ARM,ARM=address range memory,可用内存,值=1
            LOG_TAB("系可用内存.\n");
            if (maxpa < range_end && range_begin < KMEMSIZE) {
                maxpa = range_end;
                LOG_TAB("\t调整已知物理空间最大值 maxpa 至 0x%08llx = %lld K = %lld M\n", maxpa, maxpa /1024, maxpa /1024/1024);
            }
        }else{
            LOG_TAB("系不可用内存.\n");
        }
    }
    if (maxpa > KMEMSIZE) { // 可管理物理空间上限不超过 KMEMSIZE=0x38000000
        maxpa = KMEMSIZE;   
    }

    extern char end[];  //bootloader加载ucore的结束地址

    npage = maxpa / PGSIZE;
    pages = (struct Page *)ROUNDUP((void *)end, PGSIZE);

    for (i = 0; i < npage; i ++) {
        SetPageReserved(pages + i); // 设置 pages 表格中 page 的属性为不可交换
    }
    LOG("\n2) 物理内存维护表格 pages 初始化:\n     \n");

    LOG_TAB("实际管理物理内存大小 maxpa = 0x%08llx = %dM\n",maxpa,maxpa/1024/1024);
    LOG_TAB("需要管理的内存页数 npage = maxpa/PGSIZE = %d\n", npage);
    LOG_TAB("内核文件地址边界 end: 0x%08llx\n",end);
    LOG_TAB("表格起始地址 pages = ROUNDUP(end) = 0x%08lx = %d M\n", (uintptr_t)pages, (uintptr_t)pages/1024/1024);
    LOG_TAB("pages 表格自身内核虚拟地址区间 [pages,pages*n): [0x%08lx, 0x%08lx)B,已被设置为不可交换.\n",pages,((uintptr_t)pages + sizeof(struct Page) * npage), (uintptr_t)pages/1024/1024, ((uintptr_t)pages + sizeof(struct Page) * npage)/1024/1024);

    uintptr_t freemem = PADDR((uintptr_t)pages + sizeof(struct Page) * npage);

    LOG_TAB("pages 表格结束于物理地址 freemem :0x%08lxB ≈ %dM. 也是后序可用内存的起始地址. \n\n",freemem, freemem/1024/1024);
    LOG_TAB("考察管理区间, 将空闲区域标记为可用.\n");


    for (i = 0; i < memmap->nr_map; i ++) {
        uint64_t begin = memmap->map[i].addr, end = begin + memmap->map[i].size;
        LOG_TAB("考察区间: [%08llx,%08llx):\t",begin,end);
        if (memmap->map[i].type == E820_ARM) {
            if (begin < freemem) {
                begin = freemem;
            }
            if (end > KMEMSIZE) {
                end = KMEMSIZE;
            }
            if (begin < end) {
                begin = ROUNDUP(begin, PGSIZE);
                end = ROUNDDOWN(end, PGSIZE);
                if (begin < end) {
                    LOG_TAB("此区间可用, 大小为 0x%08llx B = %lld KB = %lld MB = %lld page.\n", (end - begin), (end - begin)/1024, (end - begin)/1024/1024, (end - begin)/PGSIZE);
                    init_memmap(pa2page(begin), (end - begin) / PGSIZE);
                }
            }else{
                LOG_TAB("此区间不可用, 原因: 边界非法.\n");
            }
        }else{
            LOG_TAB("此区间不可用, 原因: BIOS 认定非可用内存.\n");
        }
    }
    logline("初始化完毕: 内存分页记账");
}

//boot_map_segment - setup&enable the paging mechanism
// parameters
//  la:   linear address of this memory need to map (after x86 segment map)
//  size: memory size
//  pa:   physical address of this memory
//  perm: permission of this memory  
/**
 * 对区域进行映射,专用于内核.
 *      把虚拟地址[la, la + size)映射至物理地址[pa, pa + size),映射关系保存在 pgdir 指向的一级页表上.
 *      la 和 pa 将向下对 PGSIZE 取整.
 * 映射过程: 
 *      对于给定的虚拟地址,每隔一个 PGSIZE值,就根据指定的一级页表地址pgdir,找到对应的二级页表项,写入相应的的物理地址和属性.
 * 
 * 计算: 对于内核初始值 KMEMSIZE=896MB,需要多少个/多大空间的二级页表来保存映射关系?
 *      1) 对于一级页表, 参考 entry.S, 一个 PAGESIZE 大小的一级页表,在 KERNELBASE之上还可以维护 1G 的内存>896MB,所以仍然使用已经定义的一级页表__boot_pgdir即可.
 *      2) 对于二级页表, 则需要896M/4K=224K个 entry,每个二级页表含 1K个 entry,所以共需要 224 个二级页表.
 *      3) ucore 中比较方便地设定为每个页表的大小是 1024 个,正好占用一个 page.所以需要224*4K=896KB 的空间容纳这些页表.
 */ 
static void
boot_map_segment(pde_t *pgdir, uintptr_t la, size_t size, uintptr_t pa, uint32_t perm) {
    logline("开始: 内核区域映射");

    //LOG_TAB("一级页表地址:0x%08lx\n",pgdir);
    LOG_TAB("映射区间[0x%08lx,0x%08lx + 0x%08lx ) => [0x%08lx, 0x%08lx + 0x%08lx )\n", la, la, size, pa, pa, size);
    LOG_TAB("区间长度 = %u M\n", size/1024/1024);

    assert(PGOFF(la) == PGOFF(pa));// 要映射的地址在 page 内的偏移量应当相同
    size_t n = ROUNDUP(size + PGOFF(la), PGSIZE) / PGSIZE;
    la = ROUNDDOWN(la, PGSIZE);
    pa = ROUNDDOWN(pa, PGSIZE);
    LOG_TAB("校准后映射区间: [0x%08lx, 0x%08lx), 页数:%u\n", la, pa, n);
    LOG_TAB("校准后映射区间: [0x%08lx,0x%08lx + 0x%08lx ) => [0x%08lx, 0x%08lx + 0x%08lx )\n", la, la, n * PGSIZE, pa, pa, n * PGSIZE);

    for (; n > 0; n --, la += PGSIZE, pa += PGSIZE) {
        pte_t *ptep = get_pte(pgdir, la, 1);
        assert(ptep != NULL);
        *ptep = pa | PTE_P | perm;  // 写 la 对应的二级页表; 要保证权限的正确性.
    }
    LOG_TAB("映射完毕, 直接按照可管理内存上限映射. 虚存对一级页表比例: [KERNBASE, KERNBASE + KMEMSIZE) <=> [768, 896) <=> [3/4, 7/8)\n");
    
    logline("完毕: 内核区域映射");
}

//boot_alloc_page - allocate one page using pmm->alloc_pages(1) 
// return value: the kernel virtual address of this allocated page
//note: this function is used to get the memory for PDT(Page Directory Table)&PT(Page Table)
static void *
boot_alloc_page(void) {
    struct Page *p = alloc_page();
    if (p == NULL) {
        panic("boot_alloc_page failed.\n");
    }
    return page2kva(p);
}

//pmm_init - setup a pmm to manage physical memory, build PDT&PT to setup paging mechanism 
//         - check the correctness of pmm & paging mechanism, print PDT&PT
void
pmm_init(void) {
    // print_bootPD(); //1024 个一级页表项,只有
    logline("初始化开始:内存管理模块");
    LOG("目标: 建立完整的虚拟内存机制.\n");


    // 之前已经开启了paging,用的是 bootloader 的页表基址.现在单独维护一个变量boot_cr3 即内核一级页表基址.
    boot_cr3 = PADDR(boot_pgdir);
    LOG_TAB("已维护内核页表物理地址;当前页表只临时维护了 KERNBASE 起的 4M 映射,页表内容:\n");
    print_all_pt(boot_pgdir);

    // 初始化物理内存分配器,之后即可使用其 alloc/free 的功能
    init_pmm_manager();

    // 探测物理内存分布,初始化 pages, 然后调用 pmm->init_memmap 来初始化 freelist
    page_init();

    // 测试pmm 的alloc/free
    check_alloc_page();

    check_pgdir();

    // 编译时校验: KERNBASE和KERNTOP都是PTSIZE的整数,即可以用两级页表管理(4M 的倍数)
    static_assert(KERNBASE % PTSIZE == 0);
    static_assert( KERNTOP % PTSIZE == 0);

    // 定义一块映射,使得可以更方便地访问一级页表的内容.在 print_pgdir 中用到.
    // 定义一个高于 KERNBASE + KMEMSIZE 的地址 VPT, 设置[VPT, VPT + 4MB) => [PADDR(boot_pgdir), PADDR(boot_pgdir) + 4MB )的映射.
    // 这样一来, 只要访问到 VPT 起始的 4MB 的虚拟地址范围内,都会映射到 boot_pgdir 对应的起始的 4MB ,即一级页表本身! 太稳了.
    LOG("\n开始建立一级页表自映射: [VPT, VPT + 4MB) => [PADDR(boot_pgdir), PADDR(boot_pgdir) + 4MB).\n");
    boot_pgdir[PDX(VPT)] = PADDR(boot_pgdir) | PTE_P | PTE_W;
    LOG("\n自映射完毕.\n");
    //print_pgdir();
    print_all_pt(boot_pgdir);


    // 把所有物理内存区域映射到虚拟空间.即 [0, KMEMSIZE)->[KERNBASE, KERNBASE+KERNBASE);
    // 在此过程中会建立二级页表, 写对应的一级页表.
    boot_map_segment(boot_pgdir, KERNBASE, KMEMSIZE, 0, PTE_W);
    print_all_pt(boot_pgdir);

    // 到目前为止还是用的 bootloader 的GDT.
    // 现在更新为内核的 GDT,把内存平铺, virtual_addr 0 ~ 4G = linear_addr 0 ~ 4G.
    // 然后设置内存中的TSS即 ts, ss:esp, 设置 gdt 中的 TSS指向&ts, 最后设置 TR 寄存器的值为 gdt 中 TSS 项索引.
    gdt_init();

    // 基本的虚拟地址空间分布已经建立.检查其正确性.
    check_boot_pgdir();

    print_pgdir();
    
    kmalloc_init();
    logline("初始化完毕:内存管理模块");
}

//get_pte - get pte and return the kernel virtual address of this pte for la
//        - if the PT contians this pte didn't exist, alloc a page for PT
// parameter:
//  pgdir:  the kernel virtual base address of PDT
//  la:     the linear address need to map
//  create: a logical value to decide if alloc a page for PT
// return vaule: the kernel virtual address of this pte
/**
 * 对于给定的线性地址(liner addr),指定 page directory,返回对应的 pte.
 * 
 * 注意:只负责找到,不负责内容的读写!
 * 
 */ 
pte_t *
get_pte(pde_t *pgdir, uintptr_t la, bool create) {
    /* 
     * DEFINEs:
     *   PTE_P           0x001                   // page table/directory entry flags bit : Present
     *   PTE_W           0x002                   // page table/directory entry flags bit : Writeable
     *   PTE_U           0x004                   // page table/directory entry flags bit : User can access
     */
#if 0
    /* 原注释可参考官方 repo */
#endif
    // 1. 定位到一级页表项
    //      la 的一级页表索引=PDX(la)
    //      la 的一级页表项地址=&pgdir[PDX(la)]
    pde_t *pdep = &pgdir[PDX(la)];
    // 2. 保证二级页表的存在性(如果指定 create)
    //      一级页表项的 PTE_P 位标记了二级页表是否存在.实际上这正是按需分配内存的体现.
    //      如果不存在,就可以专门申请一个page,来存储二级页表, 并更新一级页表项的状态.
    if (!(*pdep & PTE_P)) {
        struct Page *page;
        if (!create || (page = alloc_page()) == NULL) {
            return NULL;
        }
        set_page_ref(page, 1);
        uintptr_t pa = page2pa(page);
        memset(KADDR(pa), 0, PGSIZE);
        *pdep = pa | PTE_U | PTE_W | PTE_P; // 更新一级页表项状态.默认状态是用户/可写/存在. 可以在上层更改状态,如设置为内核项.
    }
    // 3. 定位二级页表
    //      目前状态: 找到了此虚拟地址的一级页表项,且保证二级页表项是存在的.
    //      *pdep: 一级页表项内容
    //      PDE_ADDR(*pdep): 一级页表项内容中的物理地址部分(高 20位)
    //      KADDR(PDE_ADDR(*pdep)): 一级页表项内容中的物理地址对应的内核虚拟地址,即二级页表基址
    //      KADDR(PDE_ADDR(*pdep))[PTX(la)] 基于二级页表基址,用PTX(la)作为索引值,定位到二级页表项
    //      最后&取二级页表项地址.
    return &((pte_t *)KADDR(PDE_ADDR(*pdep)))[PTX(la)];
}

//get_page - get related Page struct for linear address la using PDT pgdir
struct Page *
get_page(pde_t *pgdir, uintptr_t la, pte_t **ptep_store) {
    pte_t *ptep = get_pte(pgdir, la, 0);
    if (ptep_store != NULL) {
        *ptep_store = ptep;
    }
    if (ptep != NULL && *ptep & PTE_P) {
        return pte2page(*ptep);
    }
    return NULL;
}

//page_remove_pte - free an Page sturct which is related linear address la
//                - and clean(invalidate) pte which is related linear address la
//note: PT is changed, so the TLB need to be invalidate 
/**
 * 移除二级页表项 ptep 对应的 page, 置零此二级页表项.
 * 手动作废 pgdir 和 la 对应的 tlb
 */ 
static inline void
page_remove_pte(pde_t *pgdir, uintptr_t la, pte_t *ptep) {
    /* LAB2 EXERCISE 3: YOUR CODE
     *
     * Please check if ptep is valid, and tlb must be manually updated if mapping is updated
     *
     * Maybe you want help comment, BELOW comments can help you finish the code
     *
     * Some Useful MACROs and DEFINEs, you can use them in below implementation.
     * MACROs or Functions:
     *   struct Page *page pte2page(*ptep): get the according page from the value of a ptep
     *   free_page : free a page
     *   page_ref_dec(page) : decrease page->ref. NOTICE: ff page->ref == 0 , then this page should be free.
     *   tlb_invalidate(pde_t *pgdir, uintptr_t la) : Invalidate a TLB entry, but only if the page tables being
     *                        edited are the ones currently in use by the processor.
     * DEFINEs:
     *   PTE_P           0x001                   // page table/directory entry flags bit : Present
     */
#if 0
    if (0) {                      //(1) check if page directory is present
        struct Page *page = NULL; //(2) find corresponding page to pte
                                  //(3) decrease page reference
                                  //(4) and free this page when page reference reachs 0
                                  //(5) clear second page table entry
                                  //(6) flush tlb
    }
#endif
    if (*ptep & PTE_P) {
        struct Page *page = pte2page(*ptep);
        if (page_ref_dec(page) == 0) {
            free_page(page);
        }
        *ptep = 0;
        tlb_invalidate(pgdir, la);
    }
}

void
unmap_range(pde_t *pgdir, uintptr_t start, uintptr_t end) {
    assert(start % PGSIZE == 0 && end % PGSIZE == 0);
    assert(USER_ACCESS(start, end));

    do {
        pte_t *ptep = get_pte(pgdir, start, 0);
        if (ptep == NULL) {
            start = ROUNDDOWN(start + PTSIZE, PTSIZE);
            continue ;
        }
        if (*ptep != 0) {
            page_remove_pte(pgdir, start, ptep);
        }
        start += PGSIZE;
    } while (start != 0 && start < end);
}

void
exit_range(pde_t *pgdir, uintptr_t start, uintptr_t end) {
    assert(start % PGSIZE == 0 && end % PGSIZE == 0);
    assert(USER_ACCESS(start, end));

    start = ROUNDDOWN(start, PTSIZE);
    do {
        int pde_idx = PDX(start);
        if (pgdir[pde_idx] & PTE_P) {
            free_page(pde2page(pgdir[pde_idx]));
            pgdir[pde_idx] = 0;
        }
        start += PTSIZE;
    } while (start != 0 && start < end);
}
/* copy_range - copy content of memory (start, end) of one process A to another process B
 * @to:    the addr of process B's Page Directory
 * @from:  the addr of process A's Page Directory
 * @share: flags to indicate to dup OR share. We just use dup method, so it didn't be used.
 *
 * CALL GRAPH: copy_mm-->dup_mmap-->copy_range
 */
int
copy_range(pde_t *to, pde_t *from, uintptr_t start, uintptr_t end, bool share) {
    assert(start % PGSIZE == 0 && end % PGSIZE == 0);
    assert(USER_ACCESS(start, end));
    // copy content by page unit.
    do {
        //call get_pte to find process A's pte according to the addr start
        pte_t *ptep = get_pte(from, start, 0), *nptep;
        if (ptep == NULL) {
            start = ROUNDDOWN(start + PTSIZE, PTSIZE);
            continue ;
        }
        //call get_pte to find process B's pte according to the addr start. If pte is NULL, just alloc a PT
        if (*ptep & PTE_P) {
            if ((nptep = get_pte(to, start, 1)) == NULL) {
                return -E_NO_MEM;
            }
        uint32_t perm = (*ptep & PTE_USER);
        //get page from ptep
        struct Page *page = pte2page(*ptep);
        // alloc a page for process B
        struct Page *npage=alloc_page();
        assert(page!=NULL);
        assert(npage!=NULL);
        int ret=0;
        /* LAB5:EXERCISE2 YOUR CODE
         * replicate content of page to npage, build the map of phy addr of nage with the linear addr start
         *
         * Some Useful MACROs and DEFINEs, you can use them in below implementation.
         * MACROs or Functions:
         *    page2kva(struct Page *page): return the kernel vritual addr of memory which page managed (SEE pmm.h)
         *    page_insert: build the map of phy addr of an Page with the linear addr la
         *    memcpy: typical memory copy function
         *
         * (1) find src_kvaddr: the kernel virtual address of page
         * (2) find dst_kvaddr: the kernel virtual address of npage
         * (3) memory copy from src_kvaddr to dst_kvaddr, size is PGSIZE
         * (4) build the map of phy addr of  nage with the linear addr start
         */
        void * kva_src = page2kva(page);
        void * kva_dst = page2kva(npage);
    
        memcpy(kva_dst, kva_src, PGSIZE);

        ret = page_insert(to, npage, start, perm);
        assert(ret == 0);
        }
        start += PGSIZE;
    } while (start != 0 && start < end);
    return 0;
}

// 移除 pgdir 中 la 对应的二级页表项
void
page_remove(pde_t *pgdir, uintptr_t la) {
    pte_t *ptep = get_pte(pgdir, la, 0);
    if (ptep != NULL) {
        page_remove_pte(pgdir, la, ptep);
    }
}

/**
 * 建立 pte=>page 的映射.
 * 从虚拟地址到 la 到 page 在一级页表pgdir下的映射.
 * 1. 获取la 对应的 pte
 * 2. 如果 pte 已存在,就移除掉
 * 3. 将 pte 指向的 page 指定为参数中的 page,以及一些权限.
 * 4. 更新 TLB
 */ 
int
page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm) {
    pte_t *ptep = get_pte(pgdir, la, 1);
    if (ptep == NULL) {
        return -E_NO_MEM;
    }
    page_ref_inc(page);
    if (*ptep & PTE_P) {
        struct Page *p = pte2page(*ptep);
        if (p == page) {
            page_ref_dec(page);
        }
        else {
            page_remove_pte(pgdir, la, ptep);
        }
    }
    *ptep = page2pa(page) | PTE_P | perm;
    tlb_invalidate(pgdir, la);
    return 0;
}

// invalidate a TLB entry, but only if the page tables being
// edited are the ones currently in use by the processor.
void
tlb_invalidate(pde_t *pgdir, uintptr_t la) {
    if (rcr3() == PADDR(pgdir)) {
        invlpg((void *)la);
    }
}

// pgdir_alloc_page - call alloc_page & page_insert functions to 
//                  - allocate a page size memory & setup an addr map
//                  - pa<->la with linear address la and the PDT pgdir
/**
 * 
 * 新建一个在 pgdir 下, 从未映射过的虚拟地址 la 到物理空间的的映射
 * 1. la 是从未映射过的,所以肯定要分配一个物理空间 page 与其对应.
 * 2. 建立页表
 * 3. 将新 page 设置为可交换的.
 * 
 * la: liner address,线性地址
 * perm: permission,权限
 */ 
struct Page *
pgdir_alloc_page(pde_t *pgdir, uintptr_t la, uint32_t perm) {
    struct Page *page = alloc_page();
    if (page != NULL) {
        if (page_insert(pgdir, page, la, perm) != 0) {
            free_page(page);
            return NULL;
        }
        if (swap_init_ok){
            if(check_mm_struct!=NULL) {
                swap_map_swappable(check_mm_struct, la, page, 0);
                page->pra_vaddr=la;
                assert(page_ref(page) == 1);
                //LOG("get No. %d  page: pra_vaddr %x, pra_link.prev %x, pra_link_next %x in pgdir_alloc_page\n", (page-pages), page->pra_vaddr,page->pra_page_link.prev, page->pra_page_link.next);
            } 
            else  {  //now current is existed, should fix it in the future
                //swap_map_swappable(current->mm, la, page, 0);
                //page->pra_vaddr=la;
                //assert(page_ref(page) == 1);
                //panic("pgdir_alloc_page: no pages. now current is existed, should fix it in the future\n");
            }
        }

    }

    return page;
}

static void
check_alloc_page(void) {
    pmm_manager->check();
    LOG_TAB("%-20s%s\n","check_alloc_page()", ": succeed!");
}

static void
check_pgdir(void) {
    assert(npage <= KMEMSIZE / PGSIZE);
    assert(boot_pgdir != NULL && (uint32_t)PGOFF(boot_pgdir) == 0);
    assert(get_page(boot_pgdir, 0x0, NULL) == NULL);

    struct Page *p1, *p2;
    p1 = alloc_page();
    assert(page_insert(boot_pgdir, p1, 0x0, 0) == 0);

    pte_t *ptep;
    assert((ptep = get_pte(boot_pgdir, 0x0, 0)) != NULL);
    assert(pte2page(*ptep) == p1);
    assert(page_ref(p1) == 1);

    ptep = &((pte_t *)KADDR(PDE_ADDR(boot_pgdir[0])))[1];
    assert(get_pte(boot_pgdir, PGSIZE, 0) == ptep);

    p2 = alloc_page();
    assert(page_insert(boot_pgdir, p2, PGSIZE, PTE_U | PTE_W) == 0);
    assert((ptep = get_pte(boot_pgdir, PGSIZE, 0)) != NULL);
    assert(*ptep & PTE_U);
    assert(*ptep & PTE_W);
    assert(boot_pgdir[0] & PTE_U);
    assert(page_ref(p2) == 1);

    assert(page_insert(boot_pgdir, p1, PGSIZE, 0) == 0);
    assert(page_ref(p1) == 2);
    assert(page_ref(p2) == 0);
    assert((ptep = get_pte(boot_pgdir, PGSIZE, 0)) != NULL);
    assert(pte2page(*ptep) == p1);
    assert((*ptep & PTE_U) == 0);

    page_remove(boot_pgdir, 0x0);
    assert(page_ref(p1) == 1);
    assert(page_ref(p2) == 0);

    page_remove(boot_pgdir, PGSIZE);
    assert(page_ref(p1) == 0);
    assert(page_ref(p2) == 0);

    assert(page_ref(pde2page(boot_pgdir[0])) == 1);
    free_page(pde2page(boot_pgdir[0]));
    boot_pgdir[0] = 0;

    LOG_TAB("%-20s%s\n","check_pgdir()", ": succeed!");

}

static void
check_boot_pgdir(void) {
    pte_t *ptep;
    int i;
    for (i = 0; i < npage; i += PGSIZE) {
        assert((ptep = get_pte(boot_pgdir, (uintptr_t)KADDR(i), 0)) != NULL);
        assert(PTE_ADDR(*ptep) == i);
    }

    assert(PDE_ADDR(boot_pgdir[PDX(VPT)]) == PADDR(boot_pgdir));

    assert(boot_pgdir[0] == 0);

    struct Page *p;
    p = alloc_page();
    assert(page_insert(boot_pgdir, p, 0x100, PTE_W) == 0);
    assert(page_ref(p) == 1);
    assert(page_insert(boot_pgdir, p, 0x100 + PGSIZE, PTE_W) == 0);
    assert(page_ref(p) == 2);

    const char *str = "ucore: Hello world!!";
    strcpy((void *)0x100, str);
    assert(strcmp((void *)0x100, (void *)(0x100 + PGSIZE)) == 0);

    *(char *)(page2kva(p) + 0x100) = '\0';
    assert(strlen((const char *)0x100) == 0);

    free_page(p);
    free_page(pde2page(boot_pgdir[0]));
    boot_pgdir[0] = 0;

    LOG("check_boot_pgdir() succeeded!\n");
}

//perm2str - use string 'u,r,w,-' to present the permission
static const char *
perm2str(int perm) {
    static char str[4];
    str[0] = (perm & PTE_U) ? 'u' : '-';
    str[1] = 'r';
    str[2] = (perm & PTE_W) ? 'w' : '-';
    str[3] = '\0';
    return str;
}

//get_pgtable_items - In [left, right] range of PDT or PT, find a continuous linear addr space
//                  - (left_store*X_SIZE~right_store*X_SIZE) for PDT or PT
//                  - X_SIZE=PTSIZE=4M, if PDT; X_SIZE=PGSIZE=4K, if PT
// paramemters:
//  left:        no use ???
//  right:       the high side of table's range
//  start:       the low side of table's range
//  table:       the beginning addr of table
//  left_store:  the pointer of the high side of table's next range
//  right_store: the pointer of the low side of table's next range
// return value: 0 - not a invalid item range, perm - a valid item range with perm permission 
/**
 * 获取页表项.
 * 
 */ 
static int
get_pgtable_items(size_t left, size_t right, size_t start, uintptr_t *table, size_t *left_store, size_t *right_store) {
    if (start >= right) {
        return 0;
    }
    while (start < right && !(table[start] & PTE_P)) {
        start ++;
    }
    if (start < right) {
        if (left_store != NULL) {
            *left_store = start;
        }
        int perm = (table[start ++] & PTE_USER);
        while (start < right && (table[start] & PTE_USER) == perm) {
            start ++;
        }
        if (right_store != NULL) {
            *right_store = start;
        }
        return perm;
    }
    return 0;
}

//print_pgdir - print the PDT&PT
void
print_pgdir(void) {
    LOG("\n");
    LOG_TAB("--- 页表信息 begin ---\n\n");
    size_t left, right = 0, perm;
    while ((perm = get_pgtable_items(0, NPDEENTRY, right, vpd, &left, &right)) != 0) {
        LOG_TAB("PDE(%03x) %08x-%08x %08x %s\n", right - left,
                left * PTSIZE, right * PTSIZE, (right - left) * PTSIZE, perm2str(perm));
        size_t l, r = left * NPTEENTRY;
        while ((perm = get_pgtable_items(left * NPTEENTRY, right * NPTEENTRY, r, vpt, &l, &r)) != 0) {
            LOG_TAB("  |-- PTE(%05x) %08x-%08x %08x %s\n", r - l,
                    l * PGSIZE, r * PGSIZE, (r - l) * PGSIZE, perm2str(perm));
        }
    }
    LOG("\n");
    LOG_TAB("--- 页表信息 end ---\n\n");
}

void
print_all_pt(pde_t *pt_base) {
    LOG("\n一级页表内容:\n\n");
    LOG_TAB("索引\t二级页表物理基址\t存在位\t读写性\t特权级\n");
    for(int i = 1023; i >= 0; -- i){
        pde_t *pdep = pt_base + i;
        if(*pdep != 0){
            LOG_TAB("%u", i);
            LOG_TAB("0x%08lx", (*pdep) & ~0xFFF );
            LOG_TAB("\t%u",(*pdep) & 1);
            LOG_TAB("%s",(*pdep) & 1<<1 == 0 ? "r":"rw");
            LOG_TAB("%s\n",(*pdep) & 1<<2 == 0 ? "s":"u");
        }
    }
    LOG("\n");
}


/**
 * 关于内存映射的一些计算:
 * 
 * Q 在进行地址映射时,ucore 的处理方式是,不考虑实际物理内存大小,而是直接按照设定的物理内存上限 KMEMSIZE 进行映射.
 *      这种做法基于虚拟内存的思想,把完整的物理地址一股脑全部映射到虚拟地址空间指定的这一区域, 这个区域构成了内核在每个进程独立的运行区间.
 * 
 */