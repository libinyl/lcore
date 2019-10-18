#ifndef __LIBS_STDIO_H__
#define __LIBS_STDIO_H__

#include <defs.h>
#include <stdarg.h>

/**
 * 简单的日志控制
 * 
 * todo: 每增加 1 个模块,需要修改三处代码: 开关本身;模块名称;log_check. 待优化
 */ 

#define LOG_ALL_ON 1           // 日志总控开关,应该保持开启

#define LOG_LINE_ON 1           // 行打印开关
#define LOG_MODULE_ALL_ON 1     // 模块日志总开关

#define LOG_INIT_ON  0         // init 模块开关
#define LOG_DEBUG_ON 1          // debug 模块开关,应保持开启
#define LOG_PMM_ON 1            // 物理内存管理模块开关
#define LOG_VMM_ON 0            // vmm 模块开关
#define LOG_COS_ON 0            // console 模块开关


#define __MODULE_INIT_  "kern/init/init.c"
#define __MODULE_DEBUG_  "kern/debug/kdebug.c"
#define __MODULE_PMM_   "kern/mm/pmm.c"
#define __MODULE_VMM_   "kern/mm/vmm.c"
#define __MODULE_COS_   "kern/driver/console.c"

#define LOG_TAB(_LOG_STR, ...)\
    LOG("\t"_LOG_STR, ##__VA_ARGS__)

#define LOG(_LOG_STR, ...)\
        {\
            if(log_check(__FILE__)){\
                log(_LOG_STR, ##__VA_ARGS__);\
            }\
        }
    
/* kern/libs/stdio.c */
int cprintf(const char *fmt, ...);
int vcprintf(const char *fmt, va_list ap);
void cputchar(int c);
int cputs(const char *str);
int getchar(void);
int log(const char *fmt, ...);
int log_check(const char *filename);
void logline(const char *str);

/* kern/libs/readline.c */
char *readline(const char *prompt);

/* libs/printfmt.c */
void printfmt(void (*putch)(int, void *, int), int fd, void *putdat, const char *fmt, ...);
void vprintfmt(void (*putch)(int, void *, int), int fd, void *putdat, const char *fmt, va_list ap);    
int snprintf(char *str, size_t size, const char *fmt, ...);
int vsnprintf(char *str, size_t size, const char *fmt, va_list ap);

#endif /* !__LIBS_STDIO_H__ */

