#include <sys/types.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "latency.h"

int main (int argc, char **argv)

{
    struct rtai_latency_stat s;
    ssize_t sz;
    int fd;
    
    fd = open("/dev/rtp0",O_RDWR);
    
    if (fd < 0)
	{
	perror("open");
	exit(1);
	}

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

	printf("min = %d ns, max = %d ns, avg = %d ns, overrun = %d\n",
	       s.minjitter,
	       s.maxjitter,
	       s.avgjitter,
	       s.overrun);

#ifdef CONFIG_RTAI_OPT_TIMESTAMPS
	if (s.has_timestamps)
	    {
	    printf(">>> TIMER: prologue = %d, exec = %d, epilogue = %d, overall = %d\n",
		   s.timer_prologue,
		   s.timer_exec,
		   s.timer_epilogue,
		   s.timer_overall);
	    printf("    RESUME: exec = %d\n",
		   s.resume_time);
	    printf("    SWITCH: exec = %d\n",
		   s.switch_time);
	    printf("    PERIODIC: wakeup = %d, epilogue = %d\n",
		   s.periodic_wakeup,
		   s.periodic_epilogue);
	    printf("    TICK: overall = %d, drift = %d, drift2 = %d\n",
		   s.tick_overall,
		   s.timer_drift,
		   s.timer_drift2);
	    }
#endif /* CONFIG_RTAI_OPT_TIMESTAMPS */
	}

    return 0;
}
