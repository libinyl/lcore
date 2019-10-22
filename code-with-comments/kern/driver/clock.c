#include <x86.h>
#include <trap.h>
#include <stdio.h>
#include <picirq.h>
#include <kdebug.h>
/* *
 * 时间相关硬件的函数封装 - 8253 时钟控制器,
 * 在 IRQ-0 生成中断.
 * 
 * 注: 8253内部有三个计数器，分别成为计数器0、计数器1和计数器2.
 * 我们只用到 timer1.
 * 
 * 参考 linux 的源码 include/linux/jiffies.h
 * http://blog.chinaunix.net/uid-23951161-id-206711.html
 * */

#define IO_TIMER1           0x040               // 8253 Timer #1

/* *
 * Frequency of all three count-down timers; (TIMER_FREQ/freq)
 * is the appropriate count to generate a frequency of freq Hz.
 * */


/**
 * TIMER_FREQ 是8253 每秒的脉冲个数.
 * 要想1 秒内发出中断n 次,就需要 TIMER_FREQ
 */

#define TIMER_FREQ      1193182                 // 输入频率固定,每秒的时钟脉冲个数
#define TIMER_DIV(x)    ((TIMER_FREQ + (x) / 2) / (x))  // 分频,

#define TIMER_MODE      (IO_TIMER1 + 3)         // timer mode port
#define TIMER_SEL0      0x00                    // select counter 0
#define TIMER_RATEGEN   0x04                    // mode 2, rate generator
#define TIMER_16BIT     0x30                    // r/w counter 16 bits, LSB first

volatile size_t ticks;

long SYSTEM_READ_TIMER( void ){
    return ticks;
}

/* *
 * clock_init - initialize 8253 clock to interrupt 100 times per second,
 * and then enable IRQ_TIMER.
 * 设置时钟每秒中断 100 次,即时钟频率为 100Hz,即两次中断间隔 0.01=10 毫秒  并使能时钟中断
 * */
void
clock_init(void) {
    LOG_LINE("初始化开始:时钟控制器");

    // set 8253 timer-chip
    outb(TIMER_MODE, TIMER_SEL0 | TIMER_RATEGEN | TIMER_16BIT);
    // 装入计数器初值
    int times_per_second = 100;// ms
    outb(IO_TIMER1, TIMER_DIV(times_per_second) % 256);
    outb(IO_TIMER1, TIMER_DIV(times_per_second) / 256);

    // initialize time counter 'ticks' to zero
    ticks = 0;
    pic_enable(IRQ_TIMER);
    LOG("clock_init:\n");
    LOG_TAB("每秒脉冲次数:%d\n", times_per_second);
    LOG_LINE("初始化完毕:时钟控制器");
}

