#include <sys/mman.h>
#include <sys/time.h>
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
int test_duration = 0;		/* sec of testing = 60 * -T <min>, 0 is inf */
int data_lines = 21;		/* lines of data per header line */

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
	fprintf(stderr,"latency: cannot start timer, code %d\n",err);
	return;
	}

    nsamples = ONE_BILLION / sampling_period;
    period = rt_timer_ns2ticks(sampling_period);
    expected = rt_timer_tsc();
    err = rt_task_set_periodic(NULL,TM_NOW,sampling_period);

    if (err)
	{
	fprintf(stderr,"latency: failed to set periodic, code %d\n",err);
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
    int err, n = 0;
    time_t start;

    err = rt_sem_create(&display_sem,"dispsem",0,S_FIFO);

    if (err)
	{
        fprintf(stderr,"latency: cannot create semaphore: %s\n",strerror(-err));
	return;
	}

    time(&start);

    for (;;)
	{
	err = rt_sem_p(&display_sem,TM_INFINITE);

	if (err)
	    {
	    if (err != -EIDRM)
		fprintf(stderr,"latency: failed to pend on semaphore, code %d\n",err);

	    rt_task_delete(NULL);
	    }

	if (data_lines && (n++ % data_lines)==0)
	    {
	    time_t now, dt;
	    time(&now);
	    dt = now - start;
	    printf("RTH|%12s|%12s|%12s|%12s|  %.2ldh%.2ldm%.2lds\n",
		   "lat min","lat avg","lat max","overrun",
		   dt / 3600,(dt / 60) % 60,dt % 60);
	    }

	printf("RTD|%12ld|%12ld|%12ld|%12ld\n",
	       minjitter,
	       avgjitter,
	       maxjitter,
	       overrun);
	}
}

void dump_histogram (void)

{
    int n, total_hits = 0;
  
    for (n = 0; n < HISTOGRAM_CELLS; n++)
        {
	long hits = histogram[n];

	if (hits) {
	    fprintf(stderr,"HSD|%3d-%3d|%ld\n",n,n + 1,hits);
	    total_hits += hits;
	}
    }
    fprintf(stderr,"HST|%d\n",total_hits);
}

void cleanup_upon_sig(int sig __attribute__((unused)))

{
    rt_timer_stop();
    rt_sem_delete(&display_sem);
    finished = 1;

    if (do_histogram)
	dump_histogram();

    fflush(stdout);

    exit(0);
}

int main (int argc, char **argv)

{
    int c, err;

    while ((c = getopt(argc,argv,"hp:l:T:")) != EOF)
	switch (c)
	    {
	    case 'h':
		/* ./latency --h[istogram] */
		do_histogram = 1;
		break;

	    case 'p':

		sampling_period = atoi(optarg) * 1000;
		break;

	    case 'l':

		data_lines = atoi(optarg);
		break;
		
	    case 'T':

		test_duration = atoi(optarg);
		alarm(test_duration);
		break;

	    default:
		
		fprintf(stderr, "usage: latency [options]\n"
			"  [-h]				# print histogram of scheduling latency\n"
			"  [-p <period_us>]		# sampling period\n"
			"  [-l <data-lines per header>]	# default=21, 0 to supress headers\n"
			"  [-T <test_duration_seconds>]	# default=0, so ^C to end\n");
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
	fprintf(stderr,"latency: failed to create display task, code %d\n",err);
	return 0;
	}

    err = rt_task_start(&display_task,&display,NULL);

    if (err)
	{
	fprintf(stderr,"latency: failed to start display task, code %d\n",err);
	return 0;
	}

    err = rt_task_create(&latency_task,"sampling",0,99,T_FPU);

    if (err)
	{
	fprintf(stderr,"latency: failed to create latency task, code %d\n",err);
	return 0;
	}

    err = rt_task_start(&latency_task,&latency,NULL);

    if (err)
	{
	fprintf(stderr,"latency: failed to start latency task, code %d\n",err);
	return 0;
	}

    pause();

    return 0;
}

