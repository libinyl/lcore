#include <list.h>
#include <sync.h>
#include <proc.h>
#include <sched.h>
#include <stdio.h>
#include <assert.h>
#include <kdebug.h>
#include <stride_sched.h>
#include <rr_sched.h>

static list_entry_t timer_list;

static struct sched_class *sched_class;

/**
 * 调度策略表
 */ 
static struct sched_class* sched_class_tb[] = {
    [_SCHED_RR_] = &RR_sched_class,
    [_SCHED_STRIDE_] = &stride_sched_class
};

static struct run_queue *rq;

/**
 * 添加到就绪队列
 */ 
static inline void
sched_class_enqueue(struct proc_struct *proc) {
    if (proc != idleproc) {
        sched_class->enqueue(rq, proc);
        LOG("sched_class_enqueue: 进程 %d 已入就绪队列 rq.\n", proc->pid);
    }
}

static inline void
sched_class_dequeue(struct proc_struct *proc) {
    sched_class->dequeue(rq, proc);
}

/**
 * 从就绪队列挑选下一个可运行的进程.一旦被选中,意味着会被溢出运行队列,并执行.
 */ 
static inline struct proc_struct *
sched_class_pick_next(void) {
    return sched_class->pick_next(rq);
}

static void
sched_class_proc_tick(struct proc_struct *proc) {
    if (proc != idleproc) {
        sched_class->proc_tick(rq, proc);
    }
    else {
        proc->need_resched = 1;
    }
}

static struct run_queue __rq;

/**
 * 
 * 调度器初始化,数据结构就绪
 */ 
void
sched_init(void) {
    LOG_LINE("初始化开始:进程调度器");
    LOG_TAB("初始化队列: timer_list\n");
    LOG_TAB("初始化队列: run_queue\n");


    list_init(&timer_list);

    sched_class = sched_class_tb[_SCHED_POLICY_];
    LOG_TAB("sched class: %s\n", sched_class->name);

    rq = &__rq;
    rq->max_time_slice = 5;
    LOG_TAB("\t初始化最大时间片 max_time_slice = %u\n", rq->max_time_slice);
    sched_class->init(rq);

    LOG_LINE("初始化完毕:进程调度器");

}

/**
 * 唤醒进程: 把一个进程状态更新为已就绪,添加到就绪队列
 * 
 * 顾名思义,wakeup,应当是把一个处于非 RUNNABLE(当然也不能是 ZOMBIE) 状态的进程唤醒并交给调度器加入到就绪队列.
 * 
 * 场景:
 *  - run_timer_list
 *  - schedule
 */ 
void
wakeup_proc(struct proc_struct *proc) {
    LOG("wakeup_proc start:\n");
    assert(proc->state != PROC_ZOMBIE);
    LOG_TAB("已确认此进程不是僵尸进程\n");
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        if (proc->state != PROC_RUNNABLE) {
            proc->state = PROC_RUNNABLE;
            proc->wait_state = 0;
            LOG_TAB("进程状态更新为: PROC_RUNNABLE, wait_state = 0\n");

            if (proc != current) {
                sched_class_enqueue(proc);
            }
        }
        else {
            warn("wakeup runnable process.\n");
        }
    }
    local_intr_restore(intr_flag);
    LOG("wakeup_proc end.\n");
}

/**
 * idle 运行的进程调度程序.意味着执行其他已就绪的进程.
 * 
 * 1. 恢复当前进程的期待被重新调度标志为 0
 * 2. 当前进程及 RUNNABLE 状态加入就绪队列
 * 3. 从就绪队列里挑选新的进程,并执行
 */ 

/**
 * 调度过程只涉及 rq 队列.
 * 1. 触发 trigger scheduling
 * 2. 入队 enqueue
 * 3. 选取 pick up
 * 4. 出队 dequeue
 * 5. 切换 switch
 */
void
schedule(void) {                                        // 1. 触发
    //LOG("触发进程调度\n");
    bool intr_flag;
    struct proc_struct *next;
    local_intr_save(intr_flag);
    {
        current->need_resched = 0;
        if (current->state == PROC_RUNNABLE) {
            sched_class_enqueue(current);               // 2. 当前进程状态若是 runnable 则入队,若是其他,如 wait,则不入队
        }
        if ((next = sched_class_pick_next()) != NULL) { // 3. 池内选取新进程
            sched_class_dequeue(next);                  // 4. 新进程出队
        }
        if (next == NULL) {// 池内无进程,只好运行 idleproc
            next = idleproc;
        }
        next->runs ++;
        if (next != current) {
            proc_run(next);                             // 5. 切换
        }
    }
    local_intr_restore(intr_flag);
}

void
add_timer(timer_t *timer) {
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        assert(timer->expires > 0 && timer->proc != NULL);
        assert(list_empty(&(timer->timer_link)));
        list_entry_t *le = list_next(&timer_list);
        while (le != &timer_list) {
            timer_t *next = le2timer(le, timer_link);
            if (timer->expires < next->expires) {
                next->expires -= timer->expires;
                break;
            }
            timer->expires -= next->expires;
            le = list_next(le);
        }
        list_add_before(le, &(timer->timer_link));
    }
    local_intr_restore(intr_flag);
}

/**
 * 删除 timer.
 * 
 * 如果要删除的timer 的时间片不为 0,就把它的时间片转移给它之后的 timer.
 */ 
void
del_timer(timer_t *timer) {
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        if (!list_empty(&(timer->timer_link))) {
            if (timer->expires != 0) { 
                list_entry_t *le = list_next(&(timer->timer_link));
                if (le != &timer_list) {
                    timer_t *next = le2timer(le, timer_link);
                    next->expires += timer->expires;
                }
            }
            list_del_init(&(timer->timer_link));
        }
    }
    local_intr_restore(intr_flag);
}

/**
 * 时钟中断处理
 * 
 * 更新当前系统时间点.
 *  遍历当前所有处在系统管理内的定时器，找出所有应该激活的计数器，并激活它们。
 *  该过程在且只在每次定时器中断时被调用。 在 ucore 中， 其还会调用调度器事件处理程序。
 */ 
void
run_timer_list(void) {
    LOG("run_timer_list:\n");
    bool intr_flag;

    local_intr_save(intr_flag);
    {
        list_entry_t *le = list_next(&timer_list);
        if (le != &timer_list) {
            timer_t *timer = le2timer(le, timer_link);
            assert(timer->expires != 0);// 首个 timer 的生命不应该是 0
            // 刷新首个 timer 的时间
            timer->expires --;
            // 找到所有尚未过期的 timer 并唤醒对应进程
            while (timer->expires == 0) {
                le = list_next(le);
                struct proc_struct *proc = timer->proc;
                if (proc->wait_state != 0) {
                    assert(proc->wait_state & WT_INTERRUPTED);
                }
                else {
                    warn("process %d's wait_state == 0.\n", proc->pid);
                }
                // 唤醒进程,即更新状态并从等待队列中移到就绪队列
                wakeup_proc(proc);
                // 移除唤醒进程的 timer
                del_timer(timer);
                if (le == &timer_list) {
                    break;
                }
                timer = le2timer(le, timer_link);
            }
        }
        // 调用专用于时钟中断的调度函数,示意当前进程需要减少生命值并被调度
        // 当一个进程的时间片降低至 0,则其应被标记为需被调度.
        sched_class_proc_tick(current);
    }
    local_intr_restore(intr_flag);
}
