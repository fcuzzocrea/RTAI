/* 020222 asm-arm/rtai_debug.h

Copyright (c) 2003, Thomas Gleixner, <tglx@linutronix.de)
COPYRIGHT (C) 2002 Guennadi Liakhovetski, DSA GmbH (gl@dsa-ac.de)

This program is free software; you can redistribute it and/or modify
it under the terms of version 2 of the GNU General Public License as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
/*
--------------------------------------------------------------------------
Acknowledgements
- Paolo Mantegazza	(mantegazza@aero.polimi.it)
	creator of RTAI 
*/

#ifndef _ASM_RTAI_DEBUG_H_
#define _ASM_RTAI_DEBUG_H_

#define RT_DEBUG_LEVEL 0
#if RT_DEBUG_LEVEL > 0
#define D1(x) x
#else
#define D1(x)
#endif
#if RT_DEBUG_LEVEL > 1
#define D2(x) x
#else
#define D2(x)
#endif

#define LL_SPLIT(x) ((unsigned long*)&(x))[1], ((unsigned long*)&(x))[0]

#define RTAI_SCHED_PROC_DEBUG_DEF \
	union { \
		RTIME tsc; \
		unsigned long hltsc[2]; \
	} tsc

#define RT_TIMES_DEBUG(fmt, args...) fmt \
		"\n OSCR 0X%08X, tsc 0X%08lX%08lX," \
		"\n intr_time 0X%08lX%08lX, tick_time 0X%08lX%08lX," \
		"\n OSMR0: 0X%08X, periodic_tick 0X%08X," \
		"\n linux_time 0X%08lX%08lX, linux_tick 0X%08X\n" , \
		## args, OSCR, rtai_tsc.hltsc[1], rtai_tsc.hltsc[0], \
		((unsigned long*)&rt_times.intr_time)[1], \
		((unsigned long*)&rt_times.intr_time)[0], \
		((unsigned long*)&rt_times.tick_time)[1], \
		((unsigned long*)&rt_times.tick_time)[0], \
		OSMR0, rt_times.periodic_tick, \
		((unsigned long*)&rt_times.linux_time)[1], \
		((unsigned long*)&rt_times.linux_time)[0], \
		rt_times.linux_tick

#define RTAI_SCHED_PROC_DEBUG \
	PROC_PRINT( RT_TIMES_DEBUG( "Saved OSCR 0X%08lX, next_match 0X%08lX, " \
		"timer crashes %lu, latency %d,\n" \
		"setup_time_TIMER_CPUNIT %d, setup_time_TIMER_UNIT %d,", \
		_oscr, next_match, rtai_timer_crash, \
		tuned.latency, tuned.setup_time_TIMER_CPUNIT, \
		tuned.setup_time_TIMER_UNIT ) )

#endif
