#include <rtai/task.h>
#include <rtai/timer.h>
#include <rtai/pipe.h>
#ifdef CONFIG_RTAI_OPT_TIMESTAMPS
#include <nucleus/pod.h>
#endif /* CONFIG_RTAI_OPT_TIMESTAMPS */
#include "latency.h"

MODULE_LICENSE("GPL");

#define TASK_PERIOD_NS XNARCH_CALIBRATION_PERIOD

#define SAMPLE_COUNT (1000000000 / TASK_PERIOD_NS)

RT_TASK latency_task;

RT_PIPE pipe;

int minjitter = 10000000,
    maxjitter = -10000000,
    avgjitter,
    overrun;

void latency (void *cookie)

{
    RTIME itime, expected, period;
    struct rtai_latency_stat *s;
    int minj, maxj = -10000000;
    int dt, err, count, sumj;
#ifdef CONFIG_RTAI_OPT_TIMESTAMPS
    int tsflag = 0;
    xntimes_t ts;
#endif /* CONFIG_RTAI_OPT_TIMESTAMPS */
    RT_PIPE_MSG *msg;

    err = rt_timer_start(RT_TIMER_ONESHOT);

    if (err)
	{
	xnarch_logerr("latency: cannot start timer, code %d\n",err);
	return;
	}

    period = rt_timer_ns2ticks(TASK_PERIOD_NS);
    itime = rt_timer_read() + TASK_PERIOD_NS * 5;
    expected = rt_timer_ns2ticks(itime);
    err = rt_task_set_periodic(NULL,itime,TASK_PERIOD_NS);

    if (err)
	{
	xnarch_logerr("latency: failed to set periodic, code %d\n",err);
	return;
	}

    for (;;)
	{
	minj = 10000000;

	for (count = sumj = 0; count < SAMPLE_COUNT; count++)
	    {
	    expected += period;
	    err = rt_task_wait_period();

	    if (err)
		overrun++;
	    else
		{
		dt = (int)(rt_timer_tsc() - expected);

		if (dt > maxj)
		    {
		    maxj = dt;
#ifdef CONFIG_RTAI_OPT_TIMESTAMPS
		    xnpod_get_timestamps(&ts);
		    tsflag = 1;
#endif /* CONFIG_RTAI_OPT_TIMESTAMPS */
		    }

		if (dt < minj)
		    minj = dt;

		sumj += dt;
		}
	    }

	minjitter = minj;
	maxjitter = maxj;
	avgjitter = sumj / SAMPLE_COUNT;

	msg = rt_pipe_alloc(sizeof(struct rtai_latency_stat));

	if (!msg)
	    {
	    xnarch_logerr("latency: cannot allocate pipe message\n");
	    continue;
	    }

	s = (struct rtai_latency_stat *)RT_PIPE_MSGPTR(msg);
	s->minjitter = rt_timer_ticks2ns(minjitter);
	s->maxjitter = rt_timer_ticks2ns(maxjitter);
	s->avgjitter = rt_timer_ticks2ns(avgjitter);
	s->overrun = overrun;
#ifdef CONFIG_RTAI_OPT_TIMESTAMPS
	if (tsflag)
	    {
	    s->has_timestamps = 1;
	    s->timer_prologue = rt_timer_ticks2ns(ts.timer_handler - ts.timer_entry);
	    s->timer_exec = rt_timer_ticks2ns(ts.timer_handled - ts.timer_handler);
	    s->timer_overall = rt_timer_ticks2ns(ts.timer_exit - ts.timer_entry);
	    s->timer_epilogue = rt_timer_ticks2ns(ts.intr_resched - ts.timer_exit);
	    s->timer_drift = rt_timer_ticks2ns(ts.timer_drift);
	    s->timer_drift2 = rt_timer_ticks2ns(ts.timer_drift2);
	    s->resume_time = rt_timer_ticks2ns(ts.resume_exit - ts.resume_entry);
	    s->switch_time = rt_timer_ticks2ns(ts.switch_in - ts.switch_out);
	    s->periodic_wakeup = rt_timer_ticks2ns(ts.periodic_wakeup - ts.switch_in);
	    s->periodic_epilogue = rt_timer_ticks2ns(ts.periodic_exit - ts.periodic_wakeup);
	    s->tick_overall = rt_timer_ticks2ns(ts.periodic_exit - ts.timer_entry);
	    tsflag = 0;
	    }
	else
	    s->has_timestamps = 0;
#endif /* CONFIG_RTAI_OPT_TIMESTAMPS */

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

    err = rt_task_create(&latency_task,"sampling",0,1,0);

    if (err)
	{
	xnarch_logerr("latency: failed to create latency task, code %d\n",err);
	return 1;
	}

    err = rt_pipe_open(&pipe,0);

    if (err)
	{
	xnarch_logerr("latency: failed to open real-time pipe, code %d\n",err);
	return 1;
	}

    err = rt_task_start(&latency_task,&latency,NULL);

    if (err)
	{
	xnarch_logerr("latency: failed to start latency task, code %d\n",err);
	return 1;
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
