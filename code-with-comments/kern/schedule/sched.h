#ifndef __KERN_SCHEDULE_SCHED_H__
#define __KERN_SCHEDULE_SCHED_H__

#include <defs.h>
#include <list.h>
#include <skew_heap.h>

//todo: 更优雅的策略配置方式?
#define _SCHED_POLICY_ _SCHED_RR_

#define _SCHED_RR_ 0
#define _SCHED_STRIDE_ 1

struct proc_struct;

/**
 * 定时器--基于时间的调度机制
 */ 
typedef struct {
    unsigned int expires;       // 此 itmer 的生命值(时间片的数量)
    struct proc_struct *proc;
    list_entry_t timer_link;    // timer 所在的队列
} timer_t;

#define le2timer(le, member)            \
to_struct((le), timer_t, member)

static inline timer_t *
timer_init(timer_t *timer, struct proc_struct *proc, int expires) {
    timer->expires = expires;
    timer->proc = proc;
    list_init(&(timer->timer_link));
    return timer;
}

struct run_queue;

// 调度器类的设计借鉴了 linux 的设计思想,扩展性很强.这些类封装了调度策略.
// 参考: https://www.cnblogs.com/vamei/archive/2018/07/25/9364382.html
struct sched_class {
    // the name of sched_class
    const char *name;
    // Init the run queue
    void (*init)(struct run_queue *rq);
    // put the proc into runqueue, and this function must be called with rq_lock
    void (*enqueue)(struct run_queue *rq, struct proc_struct *proc);
    // get the proc out runqueue, and this function must be called with rq_lock
    void (*dequeue)(struct run_queue *rq, struct proc_struct *proc);
    // choose the next runnable task
    struct proc_struct *(*pick_next)(struct run_queue *rq);
    // dealer of the time-tick
    void (*proc_tick)(struct run_queue *rq, struct proc_struct *proc);
    
    /* for SMP support in the future
     *  load_balance
     *     void (*load_balance)(struct rq* rq);
     *  get some proc from this rq, used in load_balance,
     *  return value is the num of gotten proc
     *  int (*get_proc)(struct rq* rq, struct proc* procs_moved[]);
     */
};

/**
 * 就绪队列, 在用户看来都是正在运行,不断地切换上下文
 */ 
struct run_queue {
    list_entry_t run_list;
    unsigned int proc_num;
    int max_time_slice;
    // For LAB6 ONLY
    skew_heap_entry_t *lab6_run_pool;
};

void sched_init(void);
void wakeup_proc(struct proc_struct *proc);
void schedule(void);
void add_timer(timer_t *timer);
void del_timer(timer_t *timer);
void run_timer_list(void);

#endif /* !__KERN_SCHEDULE_SCHED_H__ */

