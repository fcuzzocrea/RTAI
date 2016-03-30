/*
 * Copyright (C) 2016 Paolo Mantegazza <mantegazza@aero.polimi.it>
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

#define _GNU_SOURCE
#include <stdio.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <rtai_lxrt.h>

static inline RTIME rt_rdtsc(void)
{
#ifdef __i386__
        unsigned long long t;
        __asm__ __volatile__ ("rdtsc" : "=A" (t));
       return t;
#else
        union { unsigned int __ad[2]; RTIME t; } t;
        __asm__ __volatile__ ("rdtsc" : "=a" (t.__ad[0]), "=d" (t.__ad[1]));
        return t.t;
#endif
}

int main(int argc, char **argv)
{
#define WARMUP 50
	RT_TASK *usrcal;
	RTIME start_time, resume_time;
	int loop, max_loops, period, ulat = -1, klat = -1;
	long latency = 0, ovrns = 0;
	FILE *file;

	period = atoi(argv[2]);
	if (period <= 0) {
		if (!period && !access(argv[1], F_OK)) {
			file = fopen(argv[1], "r");
			fscanf(file, "%d %d %d", &klat, &ulat, &period);
			fclose(file);
		}
		rt_sched_latencies(klat > 0 ? nano2count(klat) : klat, ulat > 0 ? nano2count(ulat) : ulat, period > 0 ? nano2count(period) : period);
	} else {
 		if (!(usrcal = rt_thread_init(nam2num("USRCAL"), 0, 0, SCHED_FIFO, 0xF))) {
			return 1;
		}
		if ((file = fopen(argv[1], "w"))) {
			klat = atoi(argv[3]);
			max_loops = CONFIG_RTAI_LATENCY_SELF_CALIBRATION_TIME*(rt_get_cpu_freq()/period);
			mlockall(MCL_CURRENT | MCL_FUTURE);
			rt_make_hard_real_time();
			start_time = rt_rdtsc();
			resume_time = start_time + 5*period;
			rt_task_make_periodic(usrcal, resume_time, period);
			for (loop = 1; loop <= (max_loops + WARMUP); loop++) {
				resume_time += period;
				if (!rt_task_wait_period()) {
					latency += (long)(rt_rdtsc() - resume_time);
	               		        if (loop == WARMUP) {
                                		latency = 0;
		                        }
				} else {
					ovrns++;
				}
			}
			rt_make_soft_real_time();
			ulat = latency/max_loops;
			fprintf(file, "%lld %lld %lld\n", count2nano(klat), count2nano(ulat), count2nano(period));
			rt_sched_latencies(klat, ulat, period);
			fclose(file);
		}
		rt_thread_delete(usrcal);
	}
	return 0;
}
