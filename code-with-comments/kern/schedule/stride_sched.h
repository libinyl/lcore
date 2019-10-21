#ifndef __KERN_SCHEDULE_SCHED_stride_H__
#define __KERN_SCHEDULE_SCHED_stride_H__

#include <sched.h>

/**
 * 将 stride 调度器类暴露出来,作为向外提供的接口,供 sched.c 配置
 */ 
extern struct sched_class stride_sched_class;

#endif /* !__KERN_SCHEDULE_SCHED_stride_H__ */

