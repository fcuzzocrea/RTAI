/*
 * /include/asm/arch/mach-clps711x/timer.h
 *
 * Don't include directly - it's included through asm-arm/rtai.h
 *
 * Copyright (c) 2002 Thomas Gleixner, autronix automation <gleixner@autronix.de>
 * Copyright (c) 2002, Alex Züpke, SYSGO RTS GmbH (azu@sysgo.de)
 * 
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
 *
 * This are the mach_clps711x (ARM7) timer inlines for 
 * RTAI Real Time Application Interface
 *
 * Thanks to Guennadi Liakhovetski, DSA GmbH (gl@dsa-ac.de) for review and 
 * a helping hand in unterstanding RTAI. 
 *
 * TODO's:	Beautify, test, test, test
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
#include <linux/sched.h>

#define CPU_FREQ (tuned.cpu_freq)
#define CHECK_TC2_BOUNDS
#define DEBUG_INLINE 0
#if DEBUG_INLINE > 0
#define DI1(x) x
#else
#define DI1(x)
#endif

extern unsigned long rtai_lasttsc;
extern unsigned long rtai_TC2latch;
extern union rtai_tsc rtai_tsc;

union rtai_tsc {
	unsigned long long tsc;
	unsigned long hltsc[2];
};

static inline int timer_irq_ack( void )
{
	return 0;
}

/*
*	rdtsc reads RTAI's timestampcounter. We use Timer/Counter TC1 on
*	cpls711x (Cirrus Logic) ARM7 cpu's. This timer is not used by 
*	Linux, but it maybe used by other drivers. Check this out first.
*	The timer runs free counting down,with a wrap around every 128 ms
*	If we don't get here within 64ms, we have killed our timebase.
*	But if we miss this for 64ms our box is killed anyway -)
*
*/
static inline unsigned long long rdtsc(void)
{
        unsigned long flags, ticks, act;
 
        // we read the 16 bit timer and calc ts on this value
        hard_save_flags_and_cli(flags);
        act = ( unsigned long) (clps_readl(TC1D) & 0xffff);
	/* take care of underflows */
	ticks = (rtai_lasttsc < act) ? (0x10000 - act + rtai_lasttsc) : (rtai_lasttsc - act);
	rtai_lasttsc = act;
        rtai_tsc.tsc += (unsigned long long) ticks;
        hard_restore_flags(flags);
        return rtai_tsc.tsc;
}


/*
*	set the timer latch for timer/counter TC2 
*	we check the bounds, as long as we are in testphase
*	Switch this off by undef CHECK_TC2_BOUNDS
*/ 
static inline void rt_set_timer_latch(unsigned long delay)
{
        unsigned long flags;
        RTIME diff;
 
        // set 16 bit LATCH
        hard_save_flags_cli(flags);

        diff = rt_times.intr_time - rdtsc();
	/* we have missed the deadline already */
	if (diff < 0)	
		diff = 1;	    

	DI1( if (delay > LATCH) {
		printk("rt_set_timer_latch delay > LATCH :%ld\n",delay);
		delay = LATCH;
	});	

        rtai_TC2latch = (!delay ? ((unsigned long*)(&diff))[0] : delay);
#ifdef CHECK_TC2_BOUNDS
	if (rtai_TC2latch > LATCH) {
		DI1(printk("rt_set_timer_latch > LATCH :%ld\n",rtai_TC2latch));
		rtai_TC2latch = LATCH;
	}	
#endif
	/* This bound check is essential, remove it and you get in trouble */
	if (!rtai_TC2latch) {
		DI1(printk("rt_set_timer_latch < 1 :%ld\n",rtai_TC2latch));
		rtai_TC2latch = 1;
	}	
	clps_writel(0,TC2EOI);
        clps_writel(rtai_TC2latch, TC2D);
	clps_writel((clps_readl(INTMR1)|0x200), INTMR1);
        hard_restore_flags(flags);
}   

#define rt_set_timer_delay(x)  rt_set_timer_latch(x)

#endif
