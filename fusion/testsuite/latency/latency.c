#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <rtai/task.h>
#include <rtai/timer.h>
#include <rtai/sem.h>

RT_TASK latency_task, display_task;

RT_SEM display_sem;

long minjitter = 10000000,
     maxjitter = -10000000,
     avgjitter = 0,
     overrun = 0;

int sampling_period = 0;

#define SAMPLE_COUNT (1000000000 / sampling_period)

#define HISTOGRAM_CELLS 100

unsigned long histogram[HISTOGRAM_CELLS];

int do_histogram = 0, finished = 0;

static inline void add_histogram (long addval)

{
    long inabs = rt_timer_ticks2ns(addval >= 0 ? addval : -addval) / 1000; /* us steps */
    histogram[inabs < HISTOGRAM_CELLS ? inabs : HISTOGRAM_CELLS-1]++;
}

void latency (void *cookie)

{
    long minj = 10000000, maxj = -10000000, dt, sumj;
    int err, count, nsamples;
    RTIME expected, period;

    err = rt_timer_start(TM_ONESHOT);

    if (err)
	{
	printf("latency: cannot start timer, code %d\n",err);
	return;
	}

    nsamples = 1000000000 / sampling_period;
    period = rt_timer_ns2ticks(sampling_period);
    expected = rt_timer_tsc();
    err = rt_task_set_periodic(NULL,TM_NOW,sampling_period);

    if (err)
	{
	printf("latency: failed to set periodic, code %d\n",err);
	return;
	}

    for (;;)
	{
	for (count = sumj = 0; count < nsamples; count++)
	    {
	    expected += period;
	    err = rt_task_wait_period();

	    if (err)
		{
		if (err != -ETIMEDOUT)
		    rt_task_delete(NULL); /* Timer stopped. */

		overrun++;
		}

	    dt = (long)(rt_timer_tsc() - expected);
	    if (dt > maxj) maxj = dt;
	    if (dt < minj) minj = dt;
	    sumj += dt;

	    if (do_histogram && !finished)
		add_histogram(dt);
	    }

	minjitter = minj;
	maxjitter = maxj;
	avgjitter = sumj / nsamples;
	rt_sem_v(&display_sem);
	}
}

void display (void *cookie)

{
    int err;
	int n = 0;

    err = rt_sem_create(&display_sem,"dispsem",0,S_FIFO);

    if (err)
	{
        printf("latency: cannot create semaphore: %s\n",strerror(-err));
	return;
	}

    for (;;)
	{
	err = rt_sem_p(&display_sem,TM_INFINITE);

	if (err)
	    {
	    if (err != -EIDRM)
		printf("latency: failed to pend on semaphore, code %d\n",err);

	    rt_task_delete(NULL);
	    }

		if ((n++ % 21)==0)
			printf("RTH|%12s|%12s|%12s|%12s\n", "lat min","lat avg","lat max","overrun");

		printf("RTD|%12Ld|%12Ld|%12Ld|%12ld\n",
	       rt_timer_ticks2ns(minjitter),
	       rt_timer_ticks2ns(avgjitter),
				rt_timer_ticks2ns(maxjitter),
	       overrun);
	}
}

void dump_histogram (void)

{
    int n;
  
    for (n = 0; n < HISTOGRAM_CELLS; n++)
	{
	long hits = histogram[n];

	if (hits)
	    fprintf(stderr,"%d - %d us: %ld\n",n,n + 1,hits);
	}
}

void cleanup_upon_sig(int sig __attribute__((unused)))

{
    rt_timer_stop();
    finished = 1;
    rt_sem_delete(&display_sem);

    if (do_histogram)
	dump_histogram();

    exit(0);
}

int main (int argc, char **argv)

{
    int err, c;

    while ((c = getopt(argc,argv,"hp:")) != EOF)
	switch (c)
	    {
	    case 'h':
		/* ./latency --h[istogram] */
		do_histogram = 1;
		break;

	    case 'p':

		sampling_period = atoi(optarg) * 1000;
		break;

	    default:
		
		fprintf(stderr,"usage: latency [-h][-p <period_us>]\n");
		exit(2);
	    }

    if (sampling_period == 0)
	sampling_period = 100000; /* ns */

    signal(SIGINT, cleanup_upon_sig);
    signal(SIGTERM, cleanup_upon_sig);

    setlinebuf(stdout);

    printf("== Sampling period: %d us\n",sampling_period / 1000);

    err = rt_task_create(&display_task,"display",0,2,0);

    if (err)
	{
	printf("latency: failed to create display task, code %d\n",err);
	return 0;
	}

    err = rt_task_start(&display_task,&display,NULL);

    if (err)
	{
	printf("latency: failed to start display task, code %d\n",err);
	return 0;
	}

    err = rt_task_create(&latency_task,"sampling",0,1,T_FPU);

    if (err)
	{
	printf("latency: failed to create latency task, code %d\n",err);
	return 0;
	}

    err = rt_task_start(&latency_task,&latency,NULL);

    if (err)
	{
	printf("latency: failed to start latency task, code %d\n",err);
	return 0;
	}

    pause();

    return 0;
}
