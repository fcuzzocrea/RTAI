#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <getopt.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <semaphore.h>

static int sampling_period = 2000; /* Defaults to 2ms */

static sem_t semA, semB;

suseconds_t t0, t1, t2, tschedmin = 99999999, tschedmax = -99999999,
                        tsleepmin = 99999999, tsleepmax = -99999999;

time_t start_time;

static inline void get_time_us (suseconds_t *tp)

{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC,&ts);
    *tp = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

void *threadA (void *arg)

{
    struct sched_param param = { .sched_priority = 98 };
    struct timespec ts;

    pthread_setschedparam(pthread_self(),SCHED_FIFO,&param);

    for (;;)
	{
	sem_wait(&semA);
	ts.tv_sec = 0;
	ts.tv_nsec = sampling_period * 1000;
        get_time_us(&t0);
	clock_nanosleep(CLOCK_MONOTONIC,0,&ts,NULL);
        get_time_us(&t1);
	sem_post(&semB);
	}

    pthread_exit(NULL);
}

void *threadB (void *arg)

{
    struct sched_param param = { .sched_priority = 99 };
    suseconds_t dt;

    pthread_setschedparam(pthread_self(),SCHED_FIFO,&param);

    start_time = time(NULL);

    for (;;)
	{
	sem_post(&semA);
	sem_wait(&semB);
        get_time_us(&t2);
	
	dt = t2 - t1;

	if (tschedmin > dt)
	    tschedmin = dt;

	if (tschedmax < dt)
	    tschedmax = dt;

	dt = t1 - t0;

	if (tsleepmin > dt)
	    tsleepmin = dt;

	if (tsleepmax < dt)
	    tsleepmax = dt;
	}

    pthread_exit(NULL);
}

void cleanup_upon_sig(int sig __attribute__((unused)))

{
    time_t end_time = time(NULL), dt;

    dt = end_time - start_time;
    
    printf("   test duration: %.2ld:%.2ld:%.2ld\n",
	   dt / 3600,(dt / 60) % 60,dt % 60);
    printf("   nanosleep accuracy: jitter min = %ld us, jitter max = %ld us\n",
	   tsleepmin-sampling_period,tsleepmax-sampling_period);
    printf("   semaphore wakeup: switch min = %ld us, switch max = %ld us\n",
	   tschedmin,tschedmax);

    exit(0);
}

int main (int argc, char **argv)

{
    struct sched_param paramA = { .sched_priority = 98 };
    struct sched_param paramB = { .sched_priority = 99 };
    pthread_attr_t thattrA, thattrB;
    pthread_t thidA, thidB;
    struct timespec ts;
    time_t now;
    int err, c;

    while ((c = getopt(argc,argv,"p:")) != EOF)
        switch (c)
	    {
	    case 'p':

		sampling_period = atoi(optarg);

		if (sampling_period > 0)
		    break;

            default:

                fprintf(stderr, "usage: xx [options]\n"
                        "  [-p <period_us>]             # sampling period\n");
                exit(2);
	    }

    time(&now);

    clock_getres(CLOCK_MONOTONIC,&ts);
    printf("Starting latency measurements at %s\n",ctime(&now));
    printf("Sampling period = %d us\n",sampling_period);
    printf("Clock resolution = %ld us\n",ts.tv_sec * 1000000 + ts.tv_nsec / 1000);
    printf("Hit ^C to get the results.\n");

    mlockall(MCL_CURRENT|MCL_FUTURE);

    signal(SIGINT, cleanup_upon_sig);
    signal(SIGTERM, cleanup_upon_sig);
    signal(SIGHUP, cleanup_upon_sig);

    sem_init(&semA,0,0);
    sem_init(&semB,0,0);

    pthread_attr_init(&thattrA);
    pthread_attr_setdetachstate(&thattrA,PTHREAD_CREATE_DETACHED);
    pthread_attr_setinheritsched(&thattrA,PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&thattrA,SCHED_FIFO);
    pthread_attr_setschedparam(&thattrA,&paramA);
    err = pthread_create(&thidA,&thattrA,&threadA,NULL);

    if (err)
	goto fail;

    pthread_attr_init(&thattrB);
    pthread_attr_setdetachstate(&thattrB,PTHREAD_CREATE_DETACHED);
    pthread_attr_setinheritsched(&thattrB,PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&thattrB,SCHED_FIFO);
    pthread_attr_setschedparam(&thattrB,&paramB);
    err = pthread_create(&thidB,&thattrB,&threadB,NULL);

    if (err)
	goto fail;

    pause();

    return 0;

 fail:

    fprintf(stderr,"failed to create threads: %s\n",strerror(err));

    return 1;
}
