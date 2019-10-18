#include <defs.h>
#include <stdio.h>
#include <console.h>
#include <unistd.h>
#include <string.h>
/* HIGH level console I/O */

#define LOG_ALL_ON 0    // 日志总开关
#define LOG_PMM_ON 1    // pmm 模块开关
#define LOG_VMM_ON 1    // vmm 模块开关

#define __MODULE_INIT_  "kern/mm/init.c"
#define __MODULE_PMM_   "kern/mm/pmm.c"
#define __MODULE_VMM_   "kern/mm/vmm.c"


// #define LOG_CHECK()\
//     ({\
//         if(!LOG_ALL_ON) {0;}\
//         if(!strcmp(__FILE__, __MODULE_PMM_) && LOG_PMM_ON){\
//             1;
//         }\
//     })

int
log_check(){
    if(!LOG_ALL_ON) return 0;
    if(!strcmp(__FILE__, __MODULE_INIT_) && LOG_PMM_ON) return 1;
    if(!strcmp(__FILE__, __MODULE_PMM_) && LOG_PMM_ON) return 1;
    if(!strcmp(__FILE__, __MODULE_VMM_) && LOG_VMM_ON) return 1;
    return 1;
}

/* *
 * cputch - writes a single character @c to stdout, and it will
 * increace the value of counter pointed by @cnt.
 * */
static void
cputch(int c, int *cnt) {
    cons_putc(c);
    (*cnt) ++;
}

/* *
 * vcprintf - format a string and writes it to stdout
 *
 * The return value is the number of characters which would be
 * written to stdout.
 *
 * Call this function if you are already dealing with a va_list.
 * Or you probably want cprintf() instead.
 * */
int
vcprintf(const char *fmt, va_list ap) {
    int cnt = 0;
    vprintfmt((void*)cputch, NO_FD, &cnt, fmt, ap);
    return cnt;
}

/* *
 * cprintf - formats a string and writes it to stdout
 *
 * The return value is the number of characters which would be
 * written to stdout.
 * */
int
cprintf(const char *fmt, ...) {
    va_list ap;
    int cnt;
    va_start(ap, fmt);
    cnt = vcprintf(fmt, ap);
    va_end(ap);
    return cnt;
}

/* cputchar - writes a single character to stdout */
void
cputchar(int c) {
    cons_putc(c);
}

/* *
 * cputs- writes the string pointed by @str to stdout and
 * appends a newline character.
 * */
int
cputs(const char *str) {
    int cnt = 0;
    char c;
    while ((c = *str ++) != '\0') {
        cputch(c, &cnt);
    }
    cputch('\n', &cnt);
    return cnt;
}

/* getchar - reads a single non-zero character from stdin */
int
getchar(void) {
    int c;
    while ((c = cons_getc()) == 0)
        /* do nothing */;
    return c;
}

int
log(const char *fmt, ...) {
    if(!log_check())
        return;
    va_list ap;
    int cnt;
    va_start(ap, fmt);
    cnt = vcprintf(fmt, ap);
    va_end(ap);
    return cnt;
}
void logline(const char *str) {
    log("\n\n--------------%s--------------\n\n",str);

}