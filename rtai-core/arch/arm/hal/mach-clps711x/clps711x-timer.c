/*
 *  /arch/arm/mach-clps711x/timer.c
 *
 * (c) 2002 Thomas Gleixner, autronix automation <gleixner@autronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * This is the timer handling for RTAI Real Time Application Interface
 * COPYRIGHT (C) 2001  Paolo Mantegazza (mantegazza@aero.polimi.it)
 *
 * Thanks to Guennadi Liakhovetski, DSA GmbH (gl@dsa-ac.de) for review and 
 * a helping hand in unterstanding RTAI. 
 *
 * TODO's:	Enable profile and leds in Timer interrupt
 *		Is a call to the original timer handler 
 *		wrapped in save_flags_cli the easier thing ???
 */
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <asm/mach/irq.h>
#include <rtai.h>

unsigned long rtai_lasttsc;
unsigned long rtai_TC2latch;

#if DEBUG_LEVEL > 1
static	long	dotimercnt = 0;
#endif

/*
*	linux_timer_interrupt is the handler, when RTAI is mounted,
*	but the timer was not requested by a realtime task
*/
void linux_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	long flags;

	rdtsc(); /* update tsc */
	D2(if (++dotimercnt == 1000) {
		D2(printk("linux_timer_int, jiffies %ul\n",jiffies));
		dotimercnt = 0;
	});
	save_flags_cli(flags);
//	do_leds();
	do_timer(regs);
//	do_profile(regs);
	restore_flags(flags);
}

/*
*	sotf_timer_interrupt is the handler, when RTAI is mounted,
*	the timer is requested by a realtime task and is invoked by
*	the scheduler when there is time to do it
*/
void soft_timer_interrupt(rtai_irq_t irq, void *dev_id, struct pt_regs *regs)
{
	long flags;
	
	rdtsc(); /* update tsc */
	D2(if (++dotimercnt == 1000) {
		D2(printk("soft_timer_int, jiffies %ul\n",jiffies));
		dotimercnt = 0;
	});
	save_flags_cli(flags);
	do_timer(regs);
	restore_flags(flags);
}

/*
*	rt_request_timer set's the new timer latch value, replaces 
*	linux_timer_handler with soft_timer_handler, set's the new
*	timer latch value and free's + requests the global timer 
*	irq as realtime interrupt.
*/
void rt_request_timer(void (*handler)(void), unsigned int tick, int unused)
{

	unsigned long flags;

	flags = hard_lock_all();

	/* wait for a timer underflow and clear the int. bit */
	do {
		;
	} while ( !(clps_readl(INTSR1) & 0x200) );
        clps_writel(0,TC2EOI);

	/* set up rt_times structure */
	rt_times.linux_tick = LATCH;
	rt_times.periodic_tick = (tick > 0 && tick < rt_times.linux_tick) ? tick : rt_times.linux_tick;
	rt_times.tick_time  = rdtsc();
	rt_times.intr_time  = rt_times.tick_time + (RTIME) rt_times.periodic_tick;
	rt_times.linux_time = rt_times.tick_time + (RTIME) rt_times.linux_tick;

	/* update Match-register */

	D1( if (rt_times.periodic_tick > LATCH)
		printk("Periodic tick > LATCH\n");
	  );
	
	rt_set_timer_latch((unsigned long)rt_times.periodic_tick);

	IRQ_DESC[TIMER_8254_IRQ].action->handler = soft_timer_interrupt;

	rt_free_global_irq(TIMER_8254_IRQ);
	rt_request_global_irq(TIMER_8254_IRQ, handler);

	hard_unlock_all(flags);

	return;
}

/*
*	Switch back to linux_timer_handler, set the timer latch to
*	(linux) LATCH, release global interupt.
*/
void rt_free_timer(void)
{
	unsigned long flags;

	flags = hard_lock_all();

	rt_free_global_irq(TIMER_8254_IRQ);
	IRQ_DESC[TIMER_8254_IRQ].action->handler = linux_timer_interrupt;

	/* wait for a timer underflow and clear the int. bit */
	do {
		;
	} while ( !(clps_readl(INTSR1) & 0x200) );
        clps_writel(0,TC2EOI);

	/* set up rt_times structure */
	rt_times.linux_tick = LATCH;
	rt_times.periodic_tick = LATCH;
	rt_times.tick_time  = rdtsc();
	rt_times.intr_time  = rt_times.tick_time + (RTIME) rt_times.periodic_tick;
	rt_times.linux_time = rt_times.tick_time + (RTIME) rt_times.linux_tick;

	/* update Match-register */
	rt_set_timer_latch((unsigned long)rt_times.periodic_tick);

	hard_unlock_all(flags);
}

