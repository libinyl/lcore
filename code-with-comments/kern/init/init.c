#include <defs.h>
#include <stdio.h>
#include <string.h>
#include <console.h>
#include <kdebug.h>
#include <kmonitor.h>
#include <picirq.h>
#include <trap.h>
#include <clock.h>
#include <intr.h>
#include <pmm.h>
#include <vmm.h>
#include <ide.h>
#include <swap.h>
#include <proc.h>
#include <fs.h>

int kern_init(void) __attribute__((noreturn));


/**
 * 内核总控函数,依次初始化:
 * 
 * 1. 控制台
 * 1. 物理内存
 * 2. 虚拟内存
 * 3. 调度器
 * 4. 进程表
 * 5. 磁盘驱动
 * 6. 置换管理
 * 7. 文件系统
 */ 
int
kern_init(void) {
    extern char edata[], end[];
    memset(edata, 0, end - edata);  // 清空 bss 段

    cons_init();                // init the console

    print_history();
    print_kerninfo();

    //grade_backtrace();

    pmm_init();                 // init physical memory management

    pic_init();                 // init interrupt controller
    idt_init();                 // init interrupt descriptor table

    vmm_init();                 // init virtual memory management
    sched_init();               // init scheduler
    proc_init();                // init process table
    
    ide_init();                 // init ide devices
    swap_init();                // init swap
    fs_init();                  // init fs
    
    clock_init();               // init clock interrupt
    intr_enable();              // enable irq interrupt
    
    cpu_idle();                 // run idle process
}

void __attribute__((noinline))
grade_backtrace2(int arg0, int arg1, int arg2, int arg3) {
    mon_backtrace(0, NULL, NULL);
}

void __attribute__((noinline))
grade_backtrace1(int arg0, int arg1) {
    grade_backtrace2(arg0, (int)&arg0, arg1, (int)&arg1);
}

void __attribute__((noinline))
grade_backtrace0(int arg0, int arg1, int arg2) {
    grade_backtrace1(arg0, arg2);
}

void
grade_backtrace(void) {
    grade_backtrace0(0, (int)kern_init, 0xffff0000);
}

static void
lab1_print_cur_status(void) {
    static int round = 0;
    uint16_t reg1, reg2, reg3, reg4;
    asm volatile (
            "mov %%cs, %0;"
            "mov %%ds, %1;"
            "mov %%es, %2;"
            "mov %%ss, %3;"
            : "=m"(reg1), "=m"(reg2), "=m"(reg3), "=m"(reg4));
    LOG("%d: @ring %d\n", round, reg1 & 3);
    LOG("%d:  cs = %x\n", round, reg1);
    LOG("%d:  ds = %x\n", round, reg2);
    LOG("%d:  es = %x\n", round, reg3);
    LOG("%d:  ss = %x\n", round, reg4);
    round ++;
}
