
#ifndef _RTAI_TYPES_H_
#define _RTAI_TYPES_H_

#define PRIO_Q    0 
#define FIFO_Q    4 

#define BIN_SEM   1 
#define CNT_SEM   2 
#define RES_SEM   3

struct pt_regs;
struct rt_task_struct;

typedef long long RTIME;

typedef int (*RT_TRAP_HANDLER)(int, int, struct pt_regs *,void *);

typedef struct rt_task_struct RT_TASK;

struct rt_times {
	int linux_tick;
	int periodic_tick;
	RTIME tick_time;
	RTIME linux_time;
	RTIME intr_time;
};

#endif
