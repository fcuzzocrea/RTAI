/* rtai/arch/arm/mach-sa1100/timer.c
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

#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <asm/mach/irq.h>
#include <asm/system.h>
#include <rtai.h>
#define do_leds()
#define do_set_rtc()
#define do_profile(x)
extern struct irqaction timer_irq;
extern unsigned long (*gettimeoffset)(void);
extern int (*set_rtc)(void);
#include <asm-arm/arch/time.h>
#include <asm-arm/arch/rtai_timer.h>
#include <rtai_trace.h>

void rtai_pxa_GPIO_2_80_demux( int irq, void *dev_id, struct pt_regs *regs )
{
	pxa_GPIO_2_80_demux( IRQ_GPIO_2_80, dev_id, regs );
	/* No pending to linux! */
}

/*
 * hacked from linux/include/asm-arm/arch-sa1100/time.h
 * static void pxa_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
 */
void linux_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	pxa_timer_interrupt(irq, dev_id, regs);
#if 0
	long flags;
	int next_match;


	do {
		//do_leds();
		save_flags_cli(flags);
		do_timer(regs);
		OSSR = OSSR_M0;
		next_match = (OSMR0 += LATCH);
		restore_flags(flags);
	} while((signed long)(next_match - OSCR) <= 0);
#endif
}

void soft_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	long flags;

/*
	hard_save_flags_cli(flags);
	rthal.tsc.hltsc[1] += ((count = OSCR) < rthal.tsc.hltsc[0]);
	rthal.tsc.hltsc[0] = count;
	hard_restore_flags(flags);
*/
	rdtsc(); /* update tsc - make sure we don't miss a wrap */
	//do_leds();
	save_flags_cli(flags);
	do_timer(regs);
	rthal.timer_match = OSCR + LATCH;
	restore_flags(flags);
}

void rt_request_timer(void (*handler)(void), unsigned int tick, int unused)
{
	RTIME t;
	unsigned long flags;

	TRACE_RTAI_TIMER(TRACE_RTAI_EV_TIMER_REQUEST, handler, tick);
	flags = hard_lock_all();
/*
 * sync w/jiffie, wait for a OS timer 0 match and clear the match bit
 */
	rtai_tsc.tsc = 0;
	/* wait for a timer match 0 and clear the match bit */
	do {
//		t = rdtsc();
	} while ( (signed long)(OSMR0 - OSCR) >= 0 );
        OSSR = OSSR_M0;

	/* set up rt_times structure */
	rt_times.linux_tick = LATCH;
	rt_times.periodic_tick = tick > 0 && tick < (RTIME)rt_times.linux_tick ? tick : rt_times.linux_tick;
	rt_times.tick_time  = t = rdtsc();
	rt_times.intr_time  = t + (RTIME)rt_times.periodic_tick;
	rt_times.linux_time = t + (RTIME)rt_times.linux_tick;

	/* Trick the scheduler - set this our way. */
//	tuned.setup_time_TIMER_CPUNIT = (int)(~(~0 >> 1)) + 1; /* smallest negative + 1 - for extra safety:-) */

	/* update Match-register */
	rt_set_timer_match_reg(rt_times.periodic_tick);

	irq_desc[TIMER_8254_IRQ].action->handler = soft_timer_interrupt;

	rt_free_global_irq(TIMER_8254_IRQ);
	rt_request_global_irq(TIMER_8254_IRQ, handler);

/*
 * pend linux timer irq to handle current jiffie 
 */
 	rt_pend_linux_irq(TIMER_8254_IRQ);
 
	hard_unlock_all(flags);

	return;
}

void rt_free_timer(void)
{
	unsigned long flags;

	TRACE_RTAI_TIMER(TRACE_RTAI_EV_TIMER_FREE, 0, 0);

	flags = hard_lock_all();

	rt_free_global_irq(TIMER_8254_IRQ);
	irq_desc[TIMER_8254_IRQ].action->handler = linux_timer_interrupt;

	hard_unlock_all(flags);
}

