#include <sys/mman.h>
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

#define ONE_BILLION  1000000000
#define TEN_MILLION    10000000

long minjitter = TEN_MILLION,
     maxjitter = -TEN_MILLION,
     avgjitter = 0,
     overrun = 0;

int sampling_period = 0;
int test_duration = 0;		// sec of testing = 60 * -T <min>, 0 is inf

#define MEASURE_PERIOD ONE_BILLION
#define SAMPLE_COUNT (MEASURE_PERIOD / sampling_period)

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
    long minj = TEN_MILLION, maxj = -TEN_MILLION, dt, sumj;
    int err, count, nsamples;
    RTIME expected, period;

    err = rt_timer_start(TM_ONESHOT);

    if (err)
	{
	printf("latency: cannot start timer, code %d\n",err);
	return;
	}

    nsamples = ONE_BILLION / sampling_period;
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

	minjitter = rt_timer_ticks2ns(minj);
	maxjitter = rt_timer_ticks2ns(maxj);
	avgjitter = rt_timer_ticks2ns(sumj / nsamples);
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

	printf("RTD|%12ld|%12ld|%12ld|%12ld\n",
	       minjitter,
	       avgjitter,
	       maxjitter,
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

    while ((c = getopt(argc,argv,"hHp:T:")) != EOF)
	switch (c)
	    {
	    case 'h':
	    case 'H':
		/* ./latency --h[istogram] */
		do_histogram = 1;
		break;

	    case 'p':

		sampling_period = atoi(optarg) * 1000;
		break;

	    case 'T':

		test_duration = atoi(optarg) * 60;
		alarm(test_duration);
		break;

	    default:
		
		fprintf(stderr,"usage: latency [-h]"
			" [-p <period_us>]"
			" [-T <test_duration_minutes>]\n");
		exit(2);
	    }

    if (sampling_period == 0)
	sampling_period = 100000; /* ns */

    signal(SIGINT, cleanup_upon_sig);
    signal(SIGTERM, cleanup_upon_sig);
    signal(SIGHUP, cleanup_upon_sig);
    signal(SIGALRM, cleanup_upon_sig);

    setlinebuf(stdout);

    printf("== Sampling period: %d us\n",sampling_period / 1000);

    mlockall(MCL_CURRENT|MCL_FUTURE);

    err = rt_task_create(&display_task,"display",0,98,0);

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

    err = rt_task_create(&latency_task,"sampling",0,99,T_FPU);

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
