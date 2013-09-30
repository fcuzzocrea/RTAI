/*
 * Copyright (C) 2013 Paolo Mantegazza <mantegazza@aero.polimi.it>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#include <stdio.h>
#include <getopt.h>

#include <rtai_lxrt.h>

struct option options[] = {
	{ "help",       0, 0, 'h' },
	{ "period",     1, 0, 'p' },
	{ "time",       1, 0, 't' },
	{ NULL,         0, 0,  0  }
};

void print_usage(void)
{
    fputs(
	("\n*** KERNEL-USER SPACE LATENCY CALIBRATIONS ***\n"
	 "\n"
	 "OPTIONS:\n"
	 "  -h, --help\n"
	 "      print usage\n"
	 "  -p <period (us)>, --period <period (us)>\n"
	 "      the period of the hard real time calibrator tasks, default 200 (us)\n"
	 "  -t <duration (s)>, --time <duration (s)>\n"
	 "      the duration of the requested calibration, default 2 (s)\n"
	 "\n")
	, stderr);
}

static int period = 200 /* us */, loops = 2 /* s */, user_latency;

int user_calibrator(long loops)
{
	RTIME expected;

 	if (!rt_thread_init(nam2num("USRCAL"), 0, 0, SCHED_FIFO, 0xF)) {
		printf("*** CANNOT INIT USER LATENCY CALIBRATOR TASK ***\n");
		return 1;
	}
	mlockall(MCL_CURRENT | MCL_FUTURE);

	rt_make_hard_real_time();
	expected = rt_get_time() + 10*period;
	rt_task_make_periodic(NULL, expected, period);
	while(loops--) {
		expected += period;
		rt_task_wait_period();
		user_latency += (int)count2nano(rt_get_time() - expected);
	}
	
	rt_make_soft_real_time();

	rt_thread_delete(NULL);
	period = 0;
	return 0;
}

int main(int argc, char *argv[])
{
	int c, kern_latency;

        while (1) {
		if ((c = getopt_long(argc, argv, "hp:t:", options, NULL)) > 0) {
			switch(c) {
				case 'h': { print_usage();         return 0; } 
				case 'p': { period = atoi(optarg);    break; }
				case 't': { loops  = atoi(optarg);    break; }
			}
		}
	}

 	if (!rt_thread_init(nam2num("CALTSK"), 10, 0, SCHED_FIFO, 0xF)) {
		printf("*** CANNOT INIT CALIBRATION TASK ***\n");
		return 1;
	}
	loops  = (1000000 + period/2)/period;

	start_rt_timer(0);
	period = nano2count(1000*period);
	kern_latency = kernel_calibrator(period, loops);
	rt_thread_create((void *)user_calibrator, (void *)loops, 0);
	while (period) rt_sleep(nano2count(100000000));
	stop_rt_timer();

	kern_latency = (kern_latency + loops/2)/loops;
	printf("*** KERNEL SPACE LATENCY (RETURN DELAY) TO BE ADDED (SUBTRACTED) TO THE ONE CONFIGURED: %d. ***\n", kern_latency);
	user_latency = (user_latency + loops/2)/loops;
	printf("***   USER SPACE LATENCY (RETURN DELAY) TO BE ADDED (SUBTRACTED) TO THE ONE CONFIGURED: %d. ***\n", user_latency);
	rt_thread_delete(NULL);
	return 0;
}
