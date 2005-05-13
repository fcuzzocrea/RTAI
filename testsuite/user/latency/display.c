/*
COPYRIGHT (C) 2000  Paolo Mantegazza (mantegazza@aero.polimi.it)

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
*/


#include <stdio.h>
#include <stdlib.h>
#include <sys/poll.h>
#include <rtai_mbx.h>
#include <rtai_msg.h>

static RT_TASK *task;

int main(void)
{
	unsigned int msg;
	MBX *mbx;
	struct sample { long long min; long long max; int index, ovrn, cnt; } samp;
 	long long max = 0;
 	int n = 0;
	struct pollfd pfd = { fd: 0, events: POLLIN|POLLERR|POLLHUP, revents: 0 };

 	setlinebuf(stdout);

 	if (!(task = rt_task_init(nam2num("LATCHK"), 20, 0, 0))) {
		printf("CANNOT INIT MASTER TASK\n");
		exit(1);
	}

 	if (!(mbx = rt_get_adr(nam2num("LATMBX")))) {
		printf("CANNOT FIND MAILBOX\n");
		exit(1);
	}

  	printf("RTAI Testsuite - LXRT latency (all data in nanoseconds)\n");
	while (1) {
  		if ((n++ % 21)==0)
  			printf("RTH|%11s|%11s|%11s|%11s|%11s|%11s\n", "lat min","lat avg","lat max","ovl max", "overruns", "freq.cntr");
		rt_mbx_receive(mbx, &samp, sizeof(samp));
 		if (max < samp.max) max = samp.max;
  		printf("RTD|%11lld|%11d|%11lld|%11lld|%11d|%11d\n", samp.min, samp.index, samp.max, max, samp.ovrn, samp.cnt);
		if (poll(&pfd, 1, 20) > 0 && (pfd.revents & (POLLIN|POLLERR|POLLHUP)) != 0)
		    break;
	}

	rt_rpc(rt_get_adr(nam2num("LATCAL")), msg, &msg);
	rt_task_delete(task);
	exit(0);
}
