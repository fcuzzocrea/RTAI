/*
 * COPYRIGHT (C) 2000  Paolo Mantegazza (mantegazza@aero.polimi.it)
 * COPYRIGHT (C) 2001  Steve Papacharalambous (stevep@lineo.com)
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
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 *                        */
#ifndef _RTAI_ASM_MIPS_RTAI_SCHED_H_
#define _RTAI_ASM_MIPS_RTAI_SCHED_H_

#include <asm/rtai_stackframe.h>

extern void up_task_sw(void *, void *);


#define rt_switch_to(newtask) up_task_sw(&rt_current, (newtask));

#define rt_exchange_tasks(oldtask, newtask) up_task_sw(&(oldtask), (newtask));

#define init_arch_stack() \
do { \
	task->stack -= (RT_SIZE / sizeof(int));				\
	*(task->stack + (RT_R5 / sizeof(int))) = data;			\
	*(task->stack + (RT_R4 / sizeof(int))) = (int)rt_thread;	\
	*(task->stack + (RT_R31 / sizeof(int))) = (int)rt_startup;	\
} while(0)

#define DEFINE_LINUX_CR0
#define DEFINE_LINUX_SMP_CR0

#define init_fp_env() \
do { \
	memset(&task->fpu_reg, 0, sizeof(task->fpu_reg));	\
} while(0)

static inline void *get_stack_pointer(void)
{

	void *sp;

	__asm__ __volatile__ (
		"move\t%0,$29\n\t"
		: "=r" (sp));
	return(sp);

}  /* End function - get_stack_pointer */

#define RT_SET_RTAI_TRAP_HANDLER(x)

#define DO_TIMER_PROPER_OP()

#endif /* _RTAI_ASM_MIPS_RTAI_SCHED_H_ */

