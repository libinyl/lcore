#ifndef __KERN_DEBUG_KDEBUG_H__
#define __KERN_DEBUG_KDEBUG_H__

#include <defs.h>
#include <trap.h>



/**
 * 日志控制优化:
 * 0) 行日志开关
 * 1) 模块级日志开关: 视 kern 下一个文件夹为一个模块, 对应宏置零即可关闭
 * 2) 区域日志开关: 如果想屏蔽对某个区域的日志, 只需用 _NO_LOG_START 和_NO_LOG_END包裹区域即可.
 *    这两个宏已配置为必须成对出现,否则无法编译通过.
 */ 
#define LOG_LINE_ON 0           // 行打印开关,即"---xxx---"形式的开关

#define IS_LOG_GLOBAL_ON 0  // 日志总控(不含LOG_LINE)
#define IS_LOG_INIT_ON 1
#define IS_LOG_MEMORY_ON 1
#define IS_LOG_TRAP_ON 0
#define IS_LOG_SYNC_ON 0
#define IS_LOG_PROCESS_ON 1
#define IS_LOG_FS_ON 0
#define IS_LOG_DRIVER_ON 0
#define IS_LOG_SYSCALL_ON 0
#define IS_LOG_SCHEDULE_ON 0
#define IS_LOG_DEBUG_ON 1

extern int _will_log;
extern int log(const char *fmt, ...);
extern int log_check(const char *filename);
extern int cprintf(const char *fmt, ...);

#define _NO_LOG_START\
    {\
        do{_will_log = 0;}while(0);

#define _NO_LOG_END\
        do{_will_log = 1;}while(0);\
    }

#define LOG(_LOG_STR, ...)\
        do{\
            if(_will_log && log_check(__FILE__)){\
                log(_LOG_STR, ##__VA_ARGS__);\
            }\
        }while(0)

#define LOG_TAB(_LOG_STR, ...)\
    LOG("\t"_LOG_STR, ##__VA_ARGS__)

// 行打印, 行如"-------xxx---------"
#define LOG_LINE(_LOG_STR)\
    do{\
        if (LOG_LINE_ON)\
        cprintf("\n\n--------------"_LOG_STR"--------------\n\n");\
    }while(0)

void print_history(void);
void print_kerninfo(void);
void print_stackframe(void);
void print_debuginfo(uintptr_t eip);

#endif /* !__KERN_DEBUG_KDEBUG_H__ */

