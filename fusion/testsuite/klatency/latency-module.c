#include <rtai/task.h>
#include <rtai/timer.h>
#include <rtai/pipe.h>
#include "latency.h"

MODULE_LICENSE("GPL");

#define ONE_BILLION  1000000000
#define TEN_MILLION    10000000

#define TASK_PERIOD_NS 100000	/* ns */

#define SAMPLE_COUNT (ONE_BILLION / TASK_PERIOD_NS)

int task_period_ns = TASK_PERIOD_NS;
module_param(task_period_ns,int,0444);
MODULE_PARM_DESC(task_period_ns, "period in ns (default: 100000)");

int sample_count;
		  
RT_TASK latency_task;

RT_PIPE pipe;

long minjitter = TEN_MILLION,
     maxjitter = -TEN_MILLION,
     avgjitter,
     overrun;

void latency (void *cookie)

{
    struct rtai_latency_stat *s;
    RTIME expected, period;
    RT_PIPE_MSG *msg;
    int  err, count;

    period = rt_timer_ns2ticks(task_period_ns);
    expected = rt_timer_tsc();
    err = rt_task_set_periodic(NULL,TM_NOW,task_period_ns);

    if (err)
	{
	xnarch_logerr("latency: failed to set periodic, code %d\n",err);
	return;
	}

    sample_count = (ONE_BILLION / task_period_ns);

    for (;;)
	{
	long minj = TEN_MILLION, maxj = -TEN_MILLION, dt, sumj;

	for (count = sumj = 0; count < sample_count; count++)
	    {
	    expected += period;
	    err = rt_task_wait_period();

	    if (err)
	        overrun++;

	    dt = (long)(rt_timer_tsc() - expected);
	    if (dt > maxj)	maxj = dt;
	    if (dt < minj)	minj = dt;
	    sumj += dt;
	    }

	minjitter = minj;
	maxjitter = maxj;
	avgjitter = sumj / sample_count;

	msg = rt_pipe_alloc(sizeof(struct rtai_latency_stat));

	if (!msg)
	    {
	    xnarch_logerr("latency: cannot allocate pipe message\n");
	    continue;
	    }

	s = (struct rtai_latency_stat *)P_MSGPTR(msg);
	s->minjitter = rt_timer_ticks2ns(minjitter);
	s->maxjitter = rt_timer_ticks2ns(maxjitter);
	s->avgjitter = rt_timer_ticks2ns(avgjitter);
	s->overrun = overrun;

	/* Do not care if the user-space side of the pipe is not yet
	   open; just enter the next sampling loop then retry. But in
	   the latter case, we need to free the unsent message by
	   ourselves. */

	if (rt_pipe_write(&pipe,msg,sizeof(*s),0) != sizeof(*s))
	    rt_pipe_free(msg);
	}
}

int __latency_init (void)

{
    int err;

    err = rt_timer_start(TM_ONESHOT);

    if (err)
	{
	xnarch_logerr("latency: cannot start timer, code %d\n",err);
	return 1;
	}

    err = rt_task_create(&latency_task,"ksampling",0,99,0);

    if (err)
	{
	xnarch_logerr("latency: failed to create latency task, code %d\n",err);
	return 2;
	}

    err = rt_pipe_open(&pipe,0);

    if (err)
	{
	xnarch_logerr("latency: failed to open real-time pipe, code %d\n",err);
	return 3;
	}

    err = rt_task_start(&latency_task,&latency,NULL);

    if (err)
	{
	xnarch_logerr("latency: failed to start latency task, code %d\n",err);
	return 4;
	}

    return 0;
}

void __latency_exit (void)

{
    int err;

    rt_task_delete(&latency_task);

    err = rt_pipe_close(&pipe);

    if(err)
        xnarch_logerr("Warning: could not close pipe: err=%d.\n",err);

    rt_timer_stop();
}

module_init(__latency_init);
module_exit(__latency_exit);
