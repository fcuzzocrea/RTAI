#include <sys/types.h>
#include <sys/time.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include "latency.h"

#define HISTOGRAM_CELLS 200
unsigned long histogram[HISTOGRAM_CELLS];

int do_histogram = 0, finished = 0;

static inline void add_histogram (long addval)
{
    /* us steps */
    long inabs = (addval >= 0 ? addval : -addval) / 10;
    histogram[inabs < HISTOGRAM_CELLS ? inabs : HISTOGRAM_CELLS-1]++;
}

void dump_histogram (void)
{
    int n, total_hits = 0;
  
    for (n = 0; n < HISTOGRAM_CELLS; n++) {

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
    finished = 1;

    if (do_histogram)
	dump_histogram();

    fflush(stdout);	/* finish histogram before unloading modules */
    exit(0);
}

int main (int argc, char **argv)

{
    const char *const communication_channel = "/dev/rtp0";
    int n = 0, c, fd, err, data_lines = 21;
    struct rtai_latency_stat s;
    time_t start;
    ssize_t sz;

    while ((c = getopt(argc,argv,"hl:T:")) != EOF)
	switch (c)
	    {
	    case 'h':
		/*./klatency --h[istogram] */
		do_histogram = 1;
		break;
		
	    case 'l':

		data_lines = atoi(optarg);
		break;
		
	    case 'T':

		alarm(atoi(optarg));
		break;
		
	    default:
		
		fprintf(stderr, "usage: klatency [options]\n"
			"  [-h]				# prints histogram of latencies\n"
			"  [-l <data-lines per header>]	# default=21, 0 supresses header\n"
			"  [-T <seconds_to_test>]	# default=0, so ^C to end\n");
		exit(2);
	    }

    signal(SIGINT, cleanup_upon_sig);
    signal(SIGTERM, cleanup_upon_sig);
    signal(SIGHUP, cleanup_upon_sig);
    signal(SIGALRM, cleanup_upon_sig);

    setlinebuf(stdout);
    
    fd = open(communication_channel, O_RDWR);
    
    if (fd < 0)
        {
        fprintf(stderr, "open(%s): %m\n", communication_channel);
        exit(1);
        }

    time(&start);

    for (;;)
        {
        sz = read(fd,&s,sizeof(s));

        if (!sz)
            break;
        
        if (sz != sizeof(s))
            {
            perror("read");
            exit(1);
            }

	if (do_histogram && !finished)
	    add_histogram(s.maxjitter);

        if (data_lines && (n++ % data_lines)==0)
	    {
	    time_t now, dt;
	    time(&now);
	    dt = now - start;
	    printf("RTH|%12s|%12s|%12s|%12s|  %.2ldh%.2ldm%.2lds\n",
		   "lat min","lat avg","lat max","overrun",
		   dt / 3600,(dt / 60) % 60,dt % 60);
	    }

        printf("RTD|%12d|%12d|%12d|%12d\n",
               s.minjitter,
               s.avgjitter,
               s.maxjitter,
               s.overrun);
        }
    if ((err = close(fd))) {
	perror("close");
	exit(1);
    }
    return 0;
}

