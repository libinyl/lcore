#include <defs.h>
#include <x86.h>
#include <elf.h>

/* *********************************************************************
 * This a dirt simple boot loader, whose sole job is to boot
 * an ELF kernel image from the first IDE hard disk.
 *
 * DISK LAYOUT
 *  * This program(bootasm.S and bootmain.c) is the bootloader.
 *    It should be stored in the first sector of the disk.
 *
 *  * The 2nd sector onward holds the kernel image.
 *
 *  * The kernel image must be in ELF format.
 *
 * BOOT UP STEPS
 *  * when the CPU boots it loads the BIOS into memory and executes it
 *
 *  * the BIOS intializes devices, sets of the interrupt routines, and
 *    reads the first sector of the boot device(e.g., hard-drive)
 *    into memory and jumps to it.
 *
 *  * Assuming this boot loader is stored in the first sector of the
 *    hard-drive, this code takes over...
 *
 *  * control starts in bootasm.S -- which sets up protected mode,
 *    and a stack so C code then run, then calls bootmain()
 *
 *  * bootmain() in this file takes over, reads in the kernel and jumps to it.
 * */

#define SECTSIZE        512
#define ELFHDR          ((struct elfhdr *)0x10000)      // scratch space = 64KB

/* waitdisk - wait for disk ready */
static void
waitdisk(void) {
    while ((inb(0x1F7) & 0xC0) != 0x40)
        /* do nothing */;
}

/* readsect - read a single sector at @secno into @dst */
static void
readsect(void *dst, uint32_t secno) {
    // wait for disk to be ready
    waitdisk();

    outb(0x1F2, 1);                         // count = 1
    outb(0x1F3, secno & 0xFF);
    outb(0x1F4, (secno >> 8) & 0xFF);
    outb(0x1F5, (secno >> 16) & 0xFF);
    outb(0x1F6, ((secno >> 24) & 0xF) | 0xE0);
    outb(0x1F7, 0x20);                      // cmd 0x20 - read sectors

    // wait for disk to be ready
    waitdisk();

    // read a sector
    insl(0x1F0, dst, SECTSIZE / 4);
}

/* *
 * readseg - read @count bytes at @offset from kernel into virtual address @va,
 * might copy more than asked.
 * 从内核的offset处读取 count 个字节到虚拟地址 va. 扇区号=(offset / SECTSIZE) + 1. kenerl 位于 1 号.
 * 封装了对于 va 的处理
 * */
static void
readseg(uintptr_t va, uint32_t count, uint32_t offset) {
    uintptr_t end_va = va + count;

    // round down to sector boundary
    // 令 va 等于小于 va 但最邻近 va 的 SECTSIZE整数倍.
    va -= offset % SECTSIZE;

    // translate from bytes to sectors; kernel starts at sector 1
    uint32_t secno = (offset / SECTSIZE) + 1;

    // If this is too slow, we could read lots of sectors at a time.
    // We'd write more to memory than asked, but it doesn't matter --
    // we load in increasing order.
    for (; va < end_va; va += SECTSIZE, secno ++) {
        // secno->va
        readsect((void *)va, secno);
    }
}

void
bootmain(void) {
    // 读取 1 号磁盘(即 kernel 位于的磁盘)上4KB=1PAGE 的内容到 elf header 处就位
    readseg((uintptr_t)ELFHDR, SECTSIZE * 8, 0);

    // is this a valid ELF?
    if (ELFHDR->e_magic != ELF_MAGIC) {
        goto bad;
    }

    struct proghdr *ph, *eph;

    // 把每个 program 加载到其期待被加载到的虚拟地址位置.
    ph = (struct proghdr *)((uintptr_t)ELFHDR + ELFHDR->e_phoff);
    eph = ph + ELFHDR->e_phnum;
    for (; ph < eph; ph ++) {
        // 参考 lab2 附录C
        // ph->p_va = 0xC0100000 = 3073M,用0xFFFFFF 取低 24 位,得到0x00100000,即实际内核代码在内存的位置从 1M 位置开始
        readseg(ph->p_va & 0xFFFFFF, ph->p_memsz, ph->p_offset);
    }

    // 调用 elf header 指定的 entry point,即 entry.S 中的 kern_entry
    // note: does not return
    // 注意,这里也用 0xFFFFFF 对代码地址进行了阶段,强行虚拟地址...
    // 这样,对于内核的很高的虚拟地址的访问,都转化了在物理内存中较低的 1M 以内的访问.
    ((void (*)(void))(ELFHDR->e_entry & 0xFFFFFF))();

    /**
     * 注: 内核如何指定入口?
     *      在链接文件中, kernel.ld  - ENTRY(kern_entry)
     * 
     * 这个入口的地址是虚拟地址还是物理地址?
     *      当然是虚拟地址.因为 kernel.ld 定义了 text代码段,入口必然在这其中.
     */ 

bad:
    outw(0x8A00, 0x8A00);
    outw(0x8A00, 0x8E00);

    /* do nothing */
    while (1);
}

/**
 * kernel 实际信息:
 * 
 * Section Headers:
  [Nr] Name              Type            Addr     Off    Size   ES Flg Lk Inf Al
  [ 0]                   NULL            00000000 000000 000000 00      0   0  0
  [ 1] .text             PROGBITS        c0100000 001000 0147b8 00  AX  0   0  1
  [ 2] .rodata           PROGBITS        c01147c0 0157c0 005860 00   A  0   0 32
  [ 3] .stab             PROGBITS        c011a020 01b020 02d199 0c   A  4   0  4
  [ 4] .stabstr          STRTAB          c01471b9 0481b9 00cb09 00   A  0   0  1
  [ 5] .data             PROGBITS        c0154000 055000 002ed0 00  WA  0   0 4096
  [ 6] .data.pgdir       PROGBITS        c0157000 058000 002000 00  WA  0   0 4096
  [ 7] .bss              NOBITS          c0159000 05a000 004324 00  WA  0   0 32
  [ 8] .comment          PROGBITS        00000000 05a000 000011 01  MS  0   0  1
  [ 9] .symtab           SYMTAB          00000000 05a014 003e30 10     10 417  4
  [10] .strtab           STRTAB          00000000 05de44 0027bd 00      0   0  1
  [11] .shstrtab         STRTAB          00000000 060601 000058 00      0   0  1


 * Program Headers:
 * Type           Offset   VirtAddr   PhysAddr   FileSiz MemSiz  Flg Align
 * LOAD           0x001000 0xc0100000 0xc0100000 0x53cc2 0x53cc2 R E 0x1000
 * LOAD           0x055000 0xc0154000 0xc0154000 0x05000 0x09324 RW  0x1000
 * 
 * 可见 Program Headers 中两个部分分别是 [.text .data)和 [.data, .bss].注意 ld 还定义了 .end
 * 
 * 
 * 
 */ 