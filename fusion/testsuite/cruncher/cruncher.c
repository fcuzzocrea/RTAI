#include <sys/time.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <semaphore.h>
#include <pthread.h>
#include <nucleus/fusion.h>

#define SAMPLING_PERIOD_US 500	/* 2Khz sampling period. */

static sem_t semX, semA, semB;

static int sample_count;

static int has_fusion, dim;

static double ref;

int do_histogram = 0, finished = 0;

#define HISTOGRAM_CELLS 1000

unsigned long histogram[HISTOGRAM_CELLS];

static inline void add_histogram (long addval)

{
    long inabs = (addval >= 0 ? addval : -addval); /* 0.1 percent steps */
    histogram[inabs < HISTOGRAM_CELLS ? inabs : HISTOGRAM_CELLS-1]++;
}

static inline void get_time_us (suseconds_t *tp)

{
    if (has_fusion)
	{
	/* We need a better resolution than the one gettimeofday()
	   provides here. */
	nanotime_t t;
	pthread_cputime_rt(&t);
	*tp = t / 1000;
	}
    else
	{
	struct timeval tv;
	gettimeofday(&tv,NULL);
	*tp = tv.tv_sec * 1000000 + tv.tv_usec;
	}
}

static inline double compute (void)

{
#define ndims 5000
#define ival  (3.14 * 10000)
    static double a[ndims] = { [ 0 ... ndims - 1 ] = ival },
	          b[ndims] = { [ 0 ... ndims - 1] = ival };
    int j, k;
    double s;

    for (j = 0; j < 1000; j++)
	for (k = dim - 1, s = 0.0; k >= 0; k--)
	    s += a[k] * b[k];

    return s;
}

void dump_histogram (void)

{
    int n;
  
    for (n = 0; n < HISTOGRAM_CELLS; n++)
	{
	long hits = histogram[n];

	if (hits)
	    fprintf(stderr,"%d.%d - %d.%d%%: %ld\n",n / 10,n % 10,(n + 1) / 10,(n + 1) % 10,hits);
	}
}

void *cruncher_thread (void *arg)

{
    struct sched_param param;
    double result;

    param.sched_priority = 99;

    if (pthread_setschedparam(pthread_self(),SCHED_FIFO,&param) != 0)
	perror("pthread_setschedparam()");

    if (has_fusion)
	pthread_init_rt("cruncher",NULL,NULL);

    for (;;)
	{
	sem_wait(&semA);
        result = compute();

        if (result != ref)
            {
            fprintf(stderr, "Compute returned %f instead of %f, aborting.\n",
                    result, ref);
            exit(EXIT_FAILURE);
            }

	sem_post(&semB);
	}
}

#define IDEAL      10000
#define MARGIN       200
#define FIRST_DIM    300

void *sampler_thread (void *arg)

{
    suseconds_t mint1 = 10000000, maxt1 = 0, sumt1 = 0;
    suseconds_t mint2 = 10000000, maxt2 = 0, sumt2 = 0;
    suseconds_t t, t0, t1, ideal;
    struct sched_param param;
    struct timespec ts;
    int count, policy;

    dim = FIRST_DIM;
    ref = compute();

    param.sched_priority = 99;

    if (pthread_setschedparam(pthread_self(),SCHED_FIFO,&param) != 0)
	perror("pthread_setschedparam()");

    /* Paranoid check. */
    if (pthread_getschedparam(pthread_self(),&policy,&param) != 0)
	perror("pthread_getschedparam()");

    if (has_fusion)
	pthread_init_rt("sampler",NULL,NULL);

    printf("Calibrating cruncher...");

    for(;;) {
        fflush(stdout);
        sleep(1);               /* Let the terminal display the previous
                                   message. */

        get_time_us(&t0);

        for (count = 0; count < 100; count++)
	    {
            sem_post(&semA);
            sem_wait(&semB);
            }

        get_time_us(&t1);

        ideal = (t1 - t0) / count;

        if(dim == ndims || ((ideal > IDEAL - MARGIN) &&
                            (ideal < IDEAL + MARGIN)))
            break;

        printf("%ld, ", ideal);

        dim = dim*IDEAL/ideal;
        if(dim > ndims)
            dim = ndims;
        ref = compute();
    }

    printf("done -- ideal computation time = %ld us.\n",ideal);

    printf("%d samples, %d hz freq (pid=%d, policy=%s, prio=%d)\n",
	   sample_count,
	   has_fusion ? 1000000 / SAMPLING_PERIOD_US : 1000,
	   getpid(),
	   policy == SCHED_FIFO ? "FIFO" : policy == SCHED_RR ? "RR" : "NORMAL",
	   param.sched_priority);

    for (count = 0; count < sample_count; count++)
	{
	/* Wait for SAMPLING_PERIOD_US. */
	ts.tv_sec = 0;
	ts.tv_nsec = SAMPLING_PERIOD_US * 1000;
	get_time_us(&t0);
	nanosleep(&ts,NULL);
	get_time_us(&t1);

	t = t1 - t0;
	if (t > maxt1) maxt1 = t;
	if (t < mint1) mint1 = t;
	sumt1 += t;
	
	if (has_fusion)
	    /* Not required, but ensures that we won't be charged for
	       the cost of migrating to the Linux domain. */
	    pthread_migrate_rt(FUSION_LINUX_DOMAIN);

	/* Run the computational loop. */
	get_time_us(&t0);
	sem_post(&semA);
	sem_wait(&semB);
	get_time_us(&t1);

	t = t1 - t0;
	if (t > maxt2) maxt2 = t;
	if (t < mint2) mint2 = t;
	sumt2 += t;

	if (do_histogram && !finished)
	    add_histogram((t - ideal) * 1000 / ideal);
	}

    printf("--------\nNanosleep jitter: min = %ld us, max = %ld us, avg = %ld us\n",
	   mint1 - SAMPLING_PERIOD_US,
	   maxt1 - SAMPLING_PERIOD_US,
	   (sumt1 / sample_count) - SAMPLING_PERIOD_US);

    printf("Execution jitter: min = %ld us (%ld%%), max = %ld us (%ld%%), avg = %ld us (%ld%%)\n--------\n",
	   mint2 - ideal,
	   (mint2 - ideal) * 100 / ideal,
	   maxt2 - ideal,
	   (maxt2 - ideal) * 100 / ideal,
	   (sumt2 / sample_count) - ideal,
	   ((sumt2 / sample_count) - ideal) * 100 / ideal);

    if (do_histogram)
	dump_histogram();

    sem_post(&semX);

    return NULL;
}

void cleanup_upon_sig(int sig __attribute__((unused)))

{
    finished = 1;

    if (do_histogram)
	dump_histogram();

    exit(0);
}

int main (int ac, char **av)

{
    pthread_t sampler_thid, cruncher_thid;
    pthread_attr_t thattr;
    xnsysinfo_t info;

    signal(SIGINT, cleanup_upon_sig);
    signal(SIGTERM, cleanup_upon_sig);

    if (mlockall(MCL_CURRENT|MCL_FUTURE))
	perror("mlockall");

    if (ac > 1)
	{
	/* ./cruncher --h[istogram] [sample_count] */

	if (strncmp(av[1],"--h",3) == 0)
	    {
	    do_histogram = 1;

	    if (ac > 2)
		sample_count = atoi(av[2]);
	    }
	else
	    sample_count = atoi(av[1]);
	}

    if (sample_count == 0)
	sample_count = 1000;

    if (pthread_info_rt(&info) == 0)
	{
	printf("RTAI/fusion detected.\n");
	has_fusion = 1;

	if (pthread_start_timer_rt(FUSION_APERIODIC_TIMER))
	    {
	    fprintf(stderr,"failed to start real-time timer.\n");
	    exit(1);
	    }
	}
    else
	{
	FILE *fp = popen("grep 'cpu MHz' /proc/cpuinfo | cut -d: -f2","r");
	char buf[BUFSIZ];

	if (!fp || !fgets(buf,sizeof(buf),fp))
	    {
	    fprintf(stderr,"uhh? cannot determine CPU frequency reading /proc/cpuinfo?\n");
	    exit(2);
	    }

	pclose(fp);
	printf("RTAI/fusion *not* detected.\n");
	has_fusion = 0;
	}

    sem_init(&semA,0,0);
    sem_init(&semB,0,0);
    sem_init(&semX,0,0);

    pthread_attr_init(&thattr);
    pthread_attr_setdetachstate(&thattr,PTHREAD_CREATE_DETACHED);
    pthread_create(&cruncher_thid,&thattr,&cruncher_thread,NULL);
    pthread_create(&sampler_thid,&thattr,&sampler_thread,NULL);

    sem_wait(&semX);

    return 0;
}
