/* 020219 /rtai/include/asm-arm/rtai_sched.h
Copyright (c) 2001 Alex Züpke, SYSGO RTS GmbH (azu@sysgo.de)
COPYRIGHT (C) 2002 Wolfgang Müller (wolfgang.mueller@dsa-ac.de)

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

#ifndef _ASM_ARM_RTAI_SCHED_H_
#define _ASM_ARM_RTAI_SCHED_H_

#define rt_switch_to(new_task) up_task_sw(&rt_current, (new_task), &rt_linux_task);

#define rt_exchange_tasks(old_task, new_task) up_task_sw(&(old_task), (new_task), &rt_linux_task);

static inline unsigned long current_domain_access_control(void)
{
	unsigned long domain_access_control;
	asm volatile ("mrc p15, 0, %0, c3, c0" : "=r" (domain_access_control));
	return domain_access_control;
}

#define	init_arch_stack()						\
do {									\
	unsigned long init_arch_flags;					\
	unsigned long init_domain_access_control;			\
									\
	hard_save_flags(init_arch_flags);				\
	init_domain_access_control = current_domain_access_control();	\
									\
	task->stack -=	+15;						\
									\
	*(task->stack	+14) = (int) rt_startup;			\
	*(task->stack	+03) = (int) data;				\
	*(task->stack	+02) = (int) rt_thread;				\
	*(task->stack	+01) = (int) init_arch_flags;			\
	*(task->stack	+00) = (int) init_domain_access_control;	\
} while (0)
//	previous		now our stack frame looks like this : (refer also to up_task_sw)
//	task->stack	+15			remember startup code in up-scheduler :
//	|		+14	r14=lr		void rt_startup(void(*rt_tread)(int), int data)
//	|		+13	r11=fp		{	//	^^ = r0		      ^^ = r1
//	|		+12	r10=sl			hard_sti();
//	|		+11	r9			rt_thread(data);
//	|		+10	r8			rt_task_delete(rt_current);
//	|		+09	r7		}
//	|		+08	r6		
//	|		+07	r5		
//	|		+06	r4		
//	|		+05	r3		
//	|		+04	r2		
//	|		+03	r1		int data		// user defined
//	|		+02	r0		void rt_thread(int data) { user defined }
//	V		+01	cpsr_SVC	unsigned long init_arch_flags
//	task->stack	+00	domain_..._...	unsigned long init_domain_access_control

#define DEFINE_LINUX_CR0

#define DEFINE_LINUX_SMP_CR0

#ifdef CONFIG_RTAI_FP_SUPPORT
#define init_fp_env() \
do { \
	memset(&task->fpu_reg, 0, sizeof(task->fpu_reg)); \
}while(0)
#else
#define init_fp_env() 
#endif

static inline void *get_stack_pointer(void)
{
	void *sp;
	asm volatile ("mov %0, sp" : "=r" (sp));
	return sp;
}

#define RT_SET_RTAI_TRAP_HANDLER(x)

/* Handle spurious timer interrupts */
#define DO_TIMER_PROPER_OP() \
	if ( timer_irq_ack() < 0 ) \
		return

#endif

/* eof */
