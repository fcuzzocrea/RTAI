/*
 * Copyright (C) 2006 Paolo Mantegazza <mantegazza@aero.polimi.it>
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
 *
 */

#ifndef _RTAI_SIGNAL_H_
#define _RTAI_SIGNAL_H_

#define RTAI_SIGNALS_IDX  2

#define SIGNAL_HELPER   0
#define SIGNAL_WAITSIG  1
#define SIGNAL_REQUEST  2
#define SIGNAL_RELEASE  3 
#define SIGNAL_ENABLE   4
#define SIGNAL_DISABLE  5
#define SIGNAL_TRIGGER  6

#define SIGNAL_TASK_INIPRIO     0

struct sigsuprt_t { RT_TASK *sigtask; RT_TASK *task; long signal; void (*sighdl)(long, RT_TASK *); unsigned long cpuid; };

#ifdef __KERNEL__

#define MAXSIGNALS  16

#define SIGNAL_ENBIT   0
#define SIGNAL_PNDBIT  1

#define SIGNAL_TASK_STACK_SIZE  8192

int rt_request_signal(long signal, void (*sighdl)(long, RT_TASK *));

int rt_release_signal(long signal, RT_TASK *task);

void rt_enable_signal(long signal, RT_TASK *task);

void rt_disable_signal(long signal, RT_TASK *task);

void rt_trigger_signal(long signal, RT_TASK *task);

#else /* !__KERNEL__ */

#include <sys/mman.h>

#include <rtai_lxrt.h>

#define SIGNAL_TASK_STACK_SIZE  8192

#ifndef __SIGNAL_SUPPORT_FUN__
#define __SIGNAL_SUPPORT_FUN__

static void signal_suprt_fun(struct sigsuprt_t *funarg)
{		
	struct sigtsk_t { RT_TASK *sigtask; RT_TASK *task; };
	struct sigreq_t { RT_TASK *sigtask; RT_TASK *task; long signal; void (*sighdl)(int, RT_TASK *); };
	struct sigsuprt_t arg = *funarg;

	if ((arg.sigtask = rt_thread_init(rt_get_name(0), SIGNAL_TASK_INIPRIO, 0, SCHED_FIFO, 1 << arg.cpuid))) {
		if (!rtai_lxrt(RTAI_SIGNALS_IDX, sizeof(struct sigreq_t), SIGNAL_REQUEST, &arg).i[LOW]) {
			mlockall(MCL_CURRENT | MCL_FUTURE);
			rt_make_hard_real_time();
			while (rtai_lxrt(RTAI_SIGNALS_IDX, sizeof(struct sigtsk_t), SIGNAL_WAITSIG, &arg).i[LOW]) {
				arg.sighdl(arg.signal, arg.task);
			}
			rt_make_soft_real_time();
		}
		rt_task_delete(arg.sigtask);
	}
}

#endif /* __SIGNAL_SUPPORT_FUN__ */

static inline int rt_request_signal(long signal, void (*sighdl)(long, RT_TASK *))
{
	if (signal >= 0 && sighdl) {
		struct sigsuprt_t arg = { NULL, rt_buddy(), signal, sighdl, rtai_lxrt(RTAI_SIGNALS_IDX, sizeof(void *), SIGNAL_HELPER, &arg.sigtask).i[LOW] };
		if (rt_clone(signal_suprt_fun, &arg, SIGNAL_TASK_STACK_SIZE, 0) > 0) {
			return rtai_lxrt(RTAI_SIGNALS_IDX, sizeof(RT_TASK *), SIGNAL_HELPER, &arg.task).i[LOW];
		}
	}
	return -EINVAL;
}

static inline int rt_release_signal(long signal, RT_TASK *task)
{
	struct { long signal; RT_TASK *task; } arg = { signal, task };
	return rtai_lxrt(RTAI_SIGNALS_IDX, SIZARG, SIGNAL_RELEASE, &arg).i[LOW];
}

static inline void rt_enable_signal(long signal, RT_TASK *task)
{
	struct { long signal; RT_TASK *task; } arg = { signal, task };
	rtai_lxrt(RTAI_SIGNALS_IDX, SIZARG, SIGNAL_ENABLE, &arg);
}

static inline void rt_disable_signal(long signal, RT_TASK *task)
{
	struct { long signal; RT_TASK *task; } arg = { signal, task };
	rtai_lxrt(RTAI_SIGNALS_IDX, SIZARG, SIGNAL_DISABLE, &arg);
}

static inline void rt_trigger_signal(long signal, RT_TASK *task)
{
	struct { long signal; RT_TASK *task; } arg = { signal, task };
	rtai_lxrt(RTAI_SIGNALS_IDX, SIZARG, SIGNAL_TRIGGER, &arg);
}

#endif /* __KERNEL__ */

#endif /* !_RTAI_SIGNAL_H_ */
