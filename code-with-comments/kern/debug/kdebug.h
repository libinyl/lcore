#ifndef __KERN_DEBUG_KDEBUG_H__
#define __KERN_DEBUG_KDEBUG_H__

#include <defs.h>
#include <trap.h>


/**
 * 简单的日志控制
 * 
 * todo: 每增加 1 个模块,需要修改三处代码: 开关本身;模块名称;log_check. 待优化
 */ 

#define LOG_ALL_ON 1           // 日志总控开关,应该保持开启

#define LOG_LINE_ON 1           // 行打印开关
#define LOG_MODULE_ALL_ON 1     // 模块日志总开关

#define LOG_INIT_ON  1         // init 模块开关
#define LOG_DEBUG_ON 1          // debug 模块开关
#define LOG_PMM_ON 1            // 物理内存管理模块开关
#define LOG_VMM_ON 1            // vmm 模块开关
#define LOG_COS_ON 1            // console 模块开关
#define LOG_TRAP_ON 1            // 中断模块开关
#define LOG_SCHED_ON 1            // 调度模块开关
#define LOG_PROC_ON 1            // 进程模块开关
#define LOG_IDE_ON 1             // 磁盘模块开关


#define __MODULE_INIT_  "kern/init/init.c"
#define __MODULE_DEBUG_  "kern/debug/kdebug.c"
#define __MODULE_PMM_   "kern/mm/pmm.c"
#define __MODULE_PMM_DEFAULT_   "kern/mm/default_pmm.c"
#define __MODULE_VMM_   "kern/mm/vmm.c"
#define __MODULE_COS_   "kern/driver/console.c"
#define __MODULE_TRAP_   "kern/trap/trap.c"
#define __MODULE_SCHED_   "kern/schedule/sched.c"
#define __MODULE_PROC_   "kern/process/proc.c"
#define __MODULE_IDE_   "kern/process/proc.c"

#define LOG_TAB(_LOG_STR, ...)\
    LOG("\t"_LOG_STR, ##__VA_ARGS__)

#define LOG(_LOG_STR, ...)\
        do{\
            if(log_check(__FILE__)){\
                log(_LOG_STR, ##__VA_ARGS__);\
            }\
        }while(0)

void print_history(void);
void print_kerninfo(void);
void print_stackframe(void);
void print_debuginfo(uintptr_t eip);

#endif /* !__KERN_DEBUG_KDEBUG_H__ */

