/* 020222 asm-arm/arch-sa1100/timer.h - ARM/SA1100 specific timer
Don't include directly - it's included through asm-arm/rtai.h

COPYRIGHT (C) 2002 Guennadi Liakhovetski, DSA GmbH (gl@dsa-ac.de)
COPYRIGHT (C) 2002 Wolfgang Müller (wolfgang.mueller@dsa-ac.de)
Copyright (c) 2001 Alex Züpke, SYSGO RTS GmbH (azu@sysgo.de)

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

#ifndef _ASM_ARCH_RTAI_TIMER_H_
#define _ASM_ARCH_RTAI_TIMER_H_

#include <asm/proc/hard_system.h>
#include <linux/time.h>
#include <linux/timex.h>

#define CPU_FREQ (tuned.cpu_freq)

static inline int timer_irq_ack( void )
{
	OSSR = OSSR_M0;
	rt_unmask_irq(TIMER_8254_IRQ);
	if ( (int)(OSCR - OSMR0) < 0 )
		/* This is what just happened: we were setting a next timer interrupt
		   in the scheduler, while a previously configured timer-interrupt
		   happened, so, just restore the correct value of the match register and
		   proceed as usual. Yes, we will have to re-calculate the next interrupt. */
		OSMR0 = OSCR - 1;
	return 0;
}

union rtai_tsc {
	unsigned long long tsc;
	unsigned long hltsc[2];
};
extern volatile union rtai_tsc rtai_tsc;

static inline RTIME rdtsc(void)
{
	RTIME ts;
	unsigned long flags, count;

	hard_save_flags_and_cli(flags);

	if ( ( count = OSCR ) < rtai_tsc.hltsc[0] )
		rtai_tsc.hltsc[1]++;
	rtai_tsc.hltsc[0] = count;
	ts = rtai_tsc.tsc;
	hard_restore_flags(flags);

	return ts;
}

#define PROTECT_TIMER

#if 0
#define REG_BASE  0xfa000000
#define REG_OSMR0 0x00000000
#define REG_OSCR  0x00000010
#define REG_OSSR  0x00000014
#define REG_ICMR  0x00050004
static inline void rt_set_timer_match_reg(int delay)
{
	unsigned long flags;

	hard_save_flags_cli( flags );

	__asm__ __volatile__ (
	"mov	r2, #0xfa000000		@ REG_BASE\n"
#ifdef PROTECT_TIMER
"	cmp	%0, %2			@ if ( delay > LATCH || delay < 0 )"
"	movhi	%0, %2			@	delay = LATCH;\n"
#endif
"	teq	%0, #0			@ if ( delay == 0 ) {"
"	ldreq	r1, [r2, #0]		@	r1 = OSMR0;"
"	addeq	r0, r1, %1		@	r0 = r1 + rt_times.periodic_tick;"
"	ldrne	r0, [r2, #0x10]		@ } else { r0 = OSCR;"
"	addne	r0, r0, %0		@	r0 = r0 + delay; }"
"	str	r0, [r2, #0]		@ OSMR0 = r0;\n"
		:
		: "r" (delay), "r" (rt_times.periodic_tick), "r" (LATCH)
		: "r0", "r1", "r2", "memory", "cc" );

	hard_restore_flags( flags );
}

#else
/* Current version */
static inline void rt_set_timer_match_reg(int delay)
{
	unsigned long flags;
	unsigned long next_match;

#ifdef PROTECT_TIMER
	if ( delay > LATCH )
		delay = LATCH;
#endif

	hard_save_flags_cli(flags);

	if ( delay )
		next_match = OSMR0 = delay + OSCR;
//		next_match = OSMR0 = delay + rtai_tsc.hltsc[0];
	else
		next_match = ( OSMR0 += rt_times.periodic_tick );
//		next_match = OSMR0 = ((unsigned long *)&rt_times.intr_time)[0];

#ifdef PROTECT_TIMER
	while ((int)(next_match - OSCR) < SETUP_TIME_TICKS ) {
		OSSR = OSSR_M0;  /* Clear match on timer 0 */
		next_match = OSMR0 = OSCR + 2 * SETUP_TIME_TICKS;
	}
#endif

	hard_restore_flags(flags);
}
#endif

#define rt_set_timer_delay(x)  rt_set_timer_match_reg(x)

#endif
