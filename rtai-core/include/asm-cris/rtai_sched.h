/*
Copyright (C) 2002,2003 Axis Communications AB

Authors: Martin P Andersson (martin.andersson@linux.nu)
         Jens-Henrik Lindskov (mumrick@linux.nu)

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

--------------------------------------------------------------------------
Acknowledgements
- Paolo Mantegazza	(mantegazza@aero.polimi.it)
	creator of RTAI 
--------------------------------------------------------------------------
*/

#ifndef _RTAI_ASM_CRIS_RTAI_SCHED_H_
#define _RTAI_ASM_CRIS_RTAI_SCHED_H_

extern void up_task_sw(void *, void *);
#define rt_switch_to(new_task) up_task_sw(&rt_current, (new_task));
#define rt_exchange_tasks(old_task, new_task) up_task_sw(&(old_task), (new_task));

#define init_arch_stack()                                       \
do {                                                            \
	*(task->stack - 12) = data;	        		\
	*(task->stack - 11) = (int)rt_thread;			\
	*(task->stack - 0) = (int)rt_startup;			\
        task->stack -= 12;                                      \
} while(0)

/* Initial stack frame:
 *
 * r11   data     : second argument
 * r10   rt_thread: first argument
 * r9
 * |
 * |
 * |
 * |
 * |
 * |
 * |
 * V
 * r0
 * srp   rt_startup: function to call
 *
 * After the initial setup, only r0-r9 and srp are saved. 
 */

/* FIX if floating point is ever needed */
#define DEFINE_LINUX_CR0
#define DEFINE_LINUX_SMP_CR0
#define init_fp_env()

/* read the current stackpointer */
static inline void *get_stack_pointer(void)
{
	void *sp;
        __asm__ __volatile__("move.d $sp,%0" : "=rm" (sp));
	return sp;
}

#define RT_SET_RTAI_TRAP_HANDLER(x) rt_set_rtai_trap_handler(x)

#define DO_TIMER_PROPER_OP()

#endif




