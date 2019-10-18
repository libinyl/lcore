#include <defs.h>
#include <stdio.h>
#include <console.h>
#include <unistd.h>
#include <string.h>
/* HIGH level console I/O */

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

// todo: 用宏代替 logcheck 函数
int
log_check(const char *filename){
    if(!LOG_MODULE_ALL_ON) return 0;
    if(!strcmp(filename, __MODULE_INIT_) && LOG_INIT_ON) return 1;
    /* pmm begin */
    if(!strcmp(filename, __MODULE_PMM_) && LOG_PMM_ON) return 1;
    if(!strcmp(filename, __MODULE_PMM_DEFAULT_) && LOG_PMM_ON) return 1;
    /* pmm end */
    if(!strcmp(filename, __MODULE_VMM_) && LOG_VMM_ON) return 1;
    if(!strcmp(filename, __MODULE_DEBUG_) && LOG_DEBUG_ON) return 1;
    if(!strcmp(filename, __MODULE_COS_) && LOG_COS_ON) return 1;
    return 0;
}

int
log(const char *fmt, ...) {
    va_list ap;
    int cnt;
    va_start(ap, fmt);
    cnt = vcprintf(fmt, ap);
    va_end(ap);
    return cnt;
}

void logline(const char *str) {
    if (LOG_LINE_ON)
        cprintf("\n\n--------------%s--------------\n\n",str);
}