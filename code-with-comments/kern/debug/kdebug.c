#include <defs.h>
#include <x86.h>
#include <stab.h>
#include <stdio.h>
#include <string.h>
#include <memlayout.h>
#include <sync.h>
#include <vmm.h>
#include <proc.h>
#include <kdebug.h>
#include <kmonitor.h>
#include <assert.h>

#define STACKFRAME_DEPTH 20
#define KER_TEXT_START 0xC0100000 // kernel.ld 中 text 段起始点
#define K_SIZE (1 << 10)
#define M_SIZE (K_SIZE * K_SIZE)

#define LOG_KER_SYM_INFO(prefix, sym_name)\
    LOG_TAB("%-20s: \t0x%08x = %u M + %uK\n", (prefix), (sym_name), KERNBASE/M_SIZE, ((unsigned int)sym_name - KERNBASE)/K_SIZE)

#define LOG_KER_SYM_INFO_TAB(prefix, sym_name)\
    LOG("\t");\
    LOG_KER_SYM_INFO(prefix, sym_name)


extern const struct stab __STAB_BEGIN__[];  // beginning of stabs table
extern const struct stab __STAB_END__[];    // end of stabs table
extern const char __STABSTR_BEGIN__[];      // beginning of string table
extern const char __STABSTR_END__[];        // end of string table

/* debug information about a particular instruction pointer */
struct eipdebuginfo {
    const char *eip_file;                   // source code filename for eip
    int eip_line;                           // source code line number for eip
    const char *eip_fn_name;                // name of function containing eip
    int eip_fn_namelen;                     // length of function's name
    uintptr_t eip_fn_addr;                  // start address of function
    int eip_fn_narg;                        // number of function arguments
};

/* user STABS data structure  */
struct userstabdata {
    const struct stab *stabs;
    const struct stab *stab_end;
    const char *stabstr;
    const char *stabstr_end;
};

/* *
 * stab_binsearch - according to the input, the initial value of
 * range [*@region_left, *@region_right], find a single stab entry
 * that includes the address @addr and matches the type @type,
 * and then save its boundary to the locations that pointed
 * by @region_left and @region_right.
 *
 * Some stab types are arranged in increasing order by instruction address.
 * For example, N_FUN stabs (stab entries with n_type == N_FUN), which
 * mark functions, and N_SO stabs, which mark source files.
 *
 * Given an instruction address, this function finds the single stab entry
 * of type @type that contains that address.
 *
 * The search takes place within the range [*@region_left, *@region_right].
 * Thus, to search an entire set of N stabs, you might do:
 *
 *      left = 0;
 *      right = N - 1;    (rightmost stab)
 *      stab_binsearch(stabs, &left, &right, type, addr);
 *
 * The search modifies *region_left and *region_right to bracket the @addr.
 * *@region_left points to the matching stab that contains @addr,
 * and *@region_right points just before the next stab.
 * If *@region_left > *region_right, then @addr is not contained in any
 * matching stab.
 *
 * For example, given these N_SO stabs:
 *      Index  Type   Address
 *      0      SO     f0100000
 *      13     SO     f0100040
 *      117    SO     f0100176
 *      118    SO     f0100178
 *      555    SO     f0100652
 *      556    SO     f0100654
 *      657    SO     f0100849
 * this code:
 *      left = 0, right = 657;
 *      stab_binsearch(stabs, &left, &right, N_SO, 0xf0100184);
 * will exit setting left = 118, right = 554.
 * */
static void
stab_binsearch(const struct stab *stabs, int *region_left, int *region_right,
           int type, uintptr_t addr) {
    int l = *region_left, r = *region_right, any_matches = 0;

    while (l <= r) {
        int true_m = (l + r) / 2, m = true_m;

        // search for earliest stab with right type
        while (m >= l && stabs[m].n_type != type) {
            m --;
        }
        if (m < l) {    // no match in [l, m]
            l = true_m + 1;
            continue;
        }

        // actual binary search
        any_matches = 1;
        if (stabs[m].n_value < addr) {
            *region_left = m;
            l = true_m + 1;
        } else if (stabs[m].n_value > addr) {
            *region_right = m - 1;
            r = m - 1;
        } else {
            // exact match for 'addr', but continue loop to find
            // *region_right
            *region_left = m;
            l = m;
            addr ++;
        }
    }

    if (!any_matches) {
        *region_right = *region_left - 1;
    }
    else {
        // find rightmost region containing 'addr'
        l = *region_right;
        for (; l > *region_left && stabs[l].n_type != type; l --)
            /* do nothing */;
        *region_left = l;
    }
}

/* *
 * debuginfo_eip - Fill in the @info structure with information about
 * the specified instruction address, @addr.  Returns 0 if information
 * was found, and negative if not.  But even if it returns negative it
 * has stored some information into '*info'.
 * */
int
debuginfo_eip(uintptr_t addr, struct eipdebuginfo *info) {
    const struct stab *stabs, *stab_end;
    const char *stabstr, *stabstr_end;

    info->eip_file = "<unknown>";
    info->eip_line = 0;
    info->eip_fn_name = "<unknown>";
    info->eip_fn_namelen = 9;
    info->eip_fn_addr = addr;
    info->eip_fn_narg = 0;

    // find the relevant set of stabs
    if (addr >= KERNBASE) {
        stabs = __STAB_BEGIN__;
        stab_end = __STAB_END__;
        stabstr = __STABSTR_BEGIN__;
        stabstr_end = __STABSTR_END__;
    }
    else {
        // user-program linker script, tools/user.ld puts the information about the
        // program's stabs (included __STAB_BEGIN__, __STAB_END__, __STABSTR_BEGIN__,
        // and __STABSTR_END__) in a structure located at virtual address USTAB.
        const struct userstabdata *usd = (struct userstabdata *)USTAB;

        // make sure that debugger (current process) can access this memory
        struct mm_struct *mm;
        if (current == NULL || (mm = current->mm) == NULL) {
            return -1;
        }
        if (!user_mem_check(mm, (uintptr_t)usd, sizeof(struct userstabdata), 0)) {
            return -1;
        }

        stabs = usd->stabs;
        stab_end = usd->stab_end;
        stabstr = usd->stabstr;
        stabstr_end = usd->stabstr_end;

        // make sure the STABS and string table memory is valid
        if (!user_mem_check(mm, (uintptr_t)stabs, (uintptr_t)stab_end - (uintptr_t)stabs, 0)) {
            return -1;
        }
        if (!user_mem_check(mm, (uintptr_t)stabstr, stabstr_end - stabstr, 0)) {
            return -1;
        }
    }

    // String table validity checks
    if (stabstr_end <= stabstr || stabstr_end[-1] != 0) {
        return -1;
    }

    // Now we find the right stabs that define the function containing
    // 'eip'.  First, we find the basic source file containing 'eip'.
    // Then, we look in that source file for the function.  Then we look
    // for the line number.

    // Search the entire set of stabs for the source file (type N_SO).
    int lfile = 0, rfile = (stab_end - stabs) - 1;
    stab_binsearch(stabs, &lfile, &rfile, N_SO, addr);
    if (lfile == 0)
        return -1;

    // Search within that file's stabs for the function definition
    // (N_FUN).
    int lfun = lfile, rfun = rfile;
    int lline, rline;
    stab_binsearch(stabs, &lfun, &rfun, N_FUN, addr);

    if (lfun <= rfun) {
        // stabs[lfun] points to the function name
        // in the string table, but check bounds just in case.
        if (stabs[lfun].n_strx < stabstr_end - stabstr) {
            info->eip_fn_name = stabstr + stabs[lfun].n_strx;
        }
        info->eip_fn_addr = stabs[lfun].n_value;
        addr -= info->eip_fn_addr;
        // Search within the function definition for the line number.
        lline = lfun;
        rline = rfun;
    } else {
        // Couldn't find function stab!  Maybe we're in an assembly
        // file.  Search the whole file for the line number.
        info->eip_fn_addr = addr;
        lline = lfile;
        rline = rfile;
    }
    info->eip_fn_namelen = strfind(info->eip_fn_name, ':') - info->eip_fn_name;

    // Search within [lline, rline] for the line number stab.
    // If found, set info->eip_line to the right line number.
    // If not found, return -1.
    stab_binsearch(stabs, &lline, &rline, N_SLINE, addr);
    if (lline <= rline) {
        info->eip_line = stabs[rline].n_desc;
    } else {
        return -1;
    }

    // Search backwards from the line number for the relevant filename stab.
    // We can't just use the "lfile" stab because inlined functions
    // can interpolate code from a different file!
    // Such included source files use the N_SOL stab type.
    while (lline >= lfile
           && stabs[lline].n_type != N_SOL
           && (stabs[lline].n_type != N_SO || !stabs[lline].n_value)) {
        lline --;
    }
    if (lline >= lfile && stabs[lline].n_strx < stabstr_end - stabstr) {
        info->eip_file = stabstr + stabs[lline].n_strx;
    }

    // Set eip_fn_narg to the number of arguments taken by the function,
    // or 0 if there was no containing function.
    if (lfun < rfun) {
        for (lline = lfun + 1;
             lline < rfun && stabs[lline].n_type == N_PSYM;
             lline ++) {
            info->eip_fn_narg ++;
        }
    }
    return 0;
}

void
print_history(void) {
    LOG_LINE("历史过程");
    LOG("CPU上电,BIOS 自检\n");
    LOG("BIOS 从 #0 sector 把 bootloader 加载到 0x7c00 并执行\n\n");
    LOG("bootloader:\n");
    LOG_TAB("A20地址线打开\n");
    LOG_TAB("探测物理内存分布\n");
    LOG_TAB("初始化 boot-time GDT\n");
    LOG_TAB("使能 32 bit 保护模式\n");
    LOG_TAB("设置 C 语言环境, 栈相关指针\n");
    LOG_TAB("bootmain 将 KERNEL 的 elf header 从第二块扇区加载到内存 64KB 处\n");
    LOG_TAB("解析 elf header 信息, 将 kernel 完全加载\n");
    LOG_TAB("控制权转移到 kernel entry\n\n");
    LOG("kernel entry:\n");
    LOG_TAB("设置内核环境的页表, 使能分页\n");
    LOG_TAB("设置内核栈基址和栈指针\n");
    LOG_TAB("控制权交给 kern_init\n");
}

/**
 * 打印内核信息.
 * - entry  内核入口
 * - etext  内核代码段
 * - edata  内核数据段起始地址(物理)
 * - end    可用内存起始地址(物理)
 * - 内核占用的内存量
 */ 
void
print_kerninfo(void) {

    extern char etext[], edata[], end[], kern_entry[], kern_init[], bootstack[], bootstacktop[], __boot_pt1[],__boot_pgdir[];

    LOG_LINE("内核地址空间布局框架");
    LOG_KER_SYM_INFO("end", end);
    LOG_KER_SYM_INFO("edata", edata);
    LOG_KER_SYM_INFO("[0, 4M) pt end", __boot_pt1 + 1024 * sizeof(uint32_t));
    LOG_KER_SYM_INFO("[0, 4M) pt begin", __boot_pt1);
    LOG_KER_SYM_INFO("global pd end", __boot_pt1);
    LOG_KER_SYM_INFO("global pd begin", __boot_pgdir);    
    LOG_KER_SYM_INFO("bootstacktop", bootstacktop);    
    LOG_KER_SYM_INFO("bootstack", bootstack);    
    LOG_KER_SYM_INFO("etext", etext);
    LOG_KER_SYM_INFO("kern_init", kern_init);
    LOG_KER_SYM_INFO("kern_entry", kern_entry);
    LOG_KER_SYM_INFO("text start", KER_TEXT_START);

    LOG_TAB("%s\t:\t%uMB\n","内核文件预计占用最大内存",4);
    LOG_TAB("%s\t\t:\t%d KB\n", "内核文件实际占用内存", (end - kern_init + 1023)/1024);
    LOG_TAB("%s\t:\t0x%08lx Byte = %d MB\n", "内核可管理物理内存大小上限", KMEMSIZE, KMEMSIZE/M_SIZE);
    LOG_TAB("%s\t\t:\t[0x%08lx , 0x%08lx)\n", "内核虚拟地址区间(B)", KERNBASE, KERNBASE + KMEMSIZE);
    LOG_TAB("%s\t\t:\t[%u M, %u M)\n", "内核虚拟地址区间(M)", KERNBASE/M_SIZE, (KERNBASE + KMEMSIZE)/M_SIZE);
    LOG_TAB("内存分页大小\t\t\t:\t%d B\n\n", PGSIZE);
}

/* *
 * print_debuginfo - read and print the stat information for the address @eip,
 * and info.eip_fn_addr should be the first address of the related function.
 * */
void
print_debuginfo(uintptr_t eip) {
    struct eipdebuginfo info;
    if (debuginfo_eip(eip, &info) != 0) {
        cprintf("    <unknow>: -- 0x%08x --\n", eip);
    }
    else {
        char fnname[256];
        int j;
        for (j = 0; j < info.eip_fn_namelen; j ++) {
            fnname[j] = info.eip_fn_name[j];
        }
        fnname[j] = '\0';
        cprintf("    %s:%d: %s+%d\n", info.eip_file, info.eip_line,
                fnname, eip - info.eip_fn_addr);
    }
}

static __noinline uint32_t
read_eip(void) {
    uint32_t eip;
    asm volatile("movl 4(%%ebp), %0" : "=r" (eip));
    return eip;
}

/* *
 * print_stackframe - print a list of the saved eip values from the nested 'call'
 * instructions that led to the current point of execution
 *
 * The x86 stack pointer, namely esp, points to the lowest location on the stack
 * that is currently in use. Everything below that location in stack is free. Pushing
 * a value onto the stack will invole decreasing the stack pointer and then writing
 * the value to the place that stack pointer pointes to. And popping a value do the
 * opposite.
 *
 * The ebp (base pointer) register, in contrast, is associated with the stack
 * primarily by software convention. On entry to a C function, the function's
 * prologue code normally saves the previous function's base pointer by pushing
 * it onto the stack, and then copies the current esp value into ebp for the duration
 * of the function. If all the functions in a program obey this convention,
 * then at any given point during the program's execution, it is possible to trace
 * back through the stack by following the chain of saved ebp pointers and determining
 * exactly what nested sequence of function calls caused this particular point in the
 * program to be reached. This capability can be particularly useful, for example,
 * when a particular function causes an assert failure or panic because bad arguments
 * were passed to it, but you aren't sure who passed the bad arguments. A stack
 * backtrace lets you find the offending function.
 *
 * The inline function read_ebp() can tell us the value of current ebp. And the
 * non-inline function read_eip() is useful, it can read the value of current eip,
 * since while calling this function, read_eip() can read the caller's eip from
 * stack easily.
 *
 * In print_debuginfo(), the function debuginfo_eip() can get enough information about
 * calling-chain. Finally print_stackframe() will trace and print them for debugging.
 *
 * Note that, the length of ebp-chain is limited. In boot/bootasm.S, before jumping
 * to the kernel entry, the value of ebp has been set to zero, that's the boundary.
 * */
void
print_stackframe(void) {
     /* LAB1 YOUR CODE : STEP 1 */
     /* (1) call read_ebp() to get the value of ebp. the type is (uint32_t);
      * (2) call read_eip() to get the value of eip. the type is (uint32_t);
      * (3) from 0 .. STACKFRAME_DEPTH
      *    (3.1) printf value of ebp, eip
      *    (3.2) (uint32_t)calling arguments [0..4] = the contents in address (uint32_t)ebp +2 [0..4]
      *    (3.3) cprintf("\n");
      *    (3.4) call print_debuginfo(eip-1) to print the C calling function name and line number, etc.
      *    (3.5) popup a calling stackframe
      *           NOTICE: the calling funciton's return addr eip  = ss:[ebp+4]
      *                   the calling funciton's ebp = ss:[ebp]
      */
    uint32_t ebp = read_ebp(), eip = read_eip();

    int i, j;
    for (i = 0; ebp != 0 && i < STACKFRAME_DEPTH; i ++) {
        cprintf("ebp:0x%08x eip:0x%08x args:", ebp, eip);
        uint32_t *args = (uint32_t *)ebp + 2;
        for (j = 0; j < 4; j ++) {
            cprintf("0x%08x ", args[j]);
        }
        cprintf("\n");
        print_debuginfo(eip - 1);
        eip = ((uint32_t *)ebp)[1];
        ebp = ((uint32_t *)ebp)[0];
    }
}

/********************* 日志控制 *********************/

int
log(const char *fmt, ...) {
    va_list ap;
    int cnt;
    va_start(ap, fmt);
    cnt = vcprintf(fmt, ap);
    va_end(ap);
    return cnt;
}

#define MODULE_INIT 0
#define MODULE_MEMORY 1
#define MODULE_TRAP 2
#define MODULE_SYNC 3
#define MODULE_PROCESS 4
#define MODULE_FS 5
#define MODULE_DRIVER 6
#define MODULE_SYSCALL 7
#define MODULE_SCHEDULE 8
#define MODULE_DEBUG 9


int _will_log = 1;
struct log_ctl_entry{
    const char *mod_name;
    int is_log_on;
};

static struct log_ctl_entry log_ctl_tb[] = {

    #define LOG_CTL_ENTRY(name, log_on)\
    (struct log_ctl_entry){\
        .mod_name = name,\
        .is_log_on = log_on,\
    }
    [MODULE_INIT]       = LOG_CTL_ENTRY("kern/init",IS_LOG_INIT_ON),
    [MODULE_MEMORY]     = LOG_CTL_ENTRY("kern/mm",IS_LOG_MEMORY_ON),
    [MODULE_TRAP]       = LOG_CTL_ENTRY("kern/trap",IS_LOG_TRAP_ON),
    [MODULE_SYNC]       = LOG_CTL_ENTRY("kern/sync",IS_LOG_SYNC_ON),
    [MODULE_PROCESS]    = LOG_CTL_ENTRY("kern/process",IS_LOG_PROCESS_ON),
    [MODULE_FS]         = LOG_CTL_ENTRY("kern/fs",IS_LOG_FS_ON),
    [MODULE_DRIVER]     = LOG_CTL_ENTRY("kern/driver",IS_LOG_DRIVER_ON),
    [MODULE_SYSCALL]    = LOG_CTL_ENTRY("kern/syscall",IS_LOG_SYSCALL_ON),
    [MODULE_SCHEDULE]   = LOG_CTL_ENTRY("kern/schedule",IS_LOG_SCHEDULE_ON),
    [MODULE_DEBUG]      = LOG_CTL_ENTRY("kern/debug",IS_LOG_DEBUG_ON),
};

static struct log_ctl_entry*
get_ctl_entry(const char *mod_name){
    if((!mod_name) || (!IS_LOG_GLOBAL_ENABLE)) return NULL;
    struct log_ctl_entry *e = NULL;
    for(int i = 0; i < sizeof(log_ctl_tb)/sizeof(struct log_ctl_entry); ++i ){
        if(!strncmp(log_ctl_tb[i].mod_name, mod_name, strlen(log_ctl_tb[i].mod_name))){
            return &log_ctl_tb[i];// lookup the table above
        }
    }
    warn("get_ctl_entry: did not find a entry match [%s].\n", mod_name);
    return NULL;
}

/**
 * return 1, if check passed; otherwise 0.
 */ 
int
log_check(const char *filename){
    struct log_ctl_entry* e = get_ctl_entry(filename);
    if(!e) return 0;
    return e->is_log_on;
}