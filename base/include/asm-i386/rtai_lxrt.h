/*
 * Copyright (C) 1999-2003 Paolo Mantegazza <mantegazza@aero.polimi.it>
 * extensions for user space modules are jointly copyrighted (2000) with:
 *		Pierre Cloutier <pcloutier@poseidoncontrols.com>,
 *		Steve Papacharalambous <stevep@zentropix.com>.
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
 */

#ifndef _RTAI_ASM_I386_LXRT_H
#define _RTAI_ASM_I386_LXRT_H

#include <linux/version.h>

#include <asm/rtai_vectors.h>

//#define USE_LINUX_SYSCALL

#define RTAI_SYSCALL_NR      orig_eax
#define RTAI_SYSCALL_ARGS    ecx
#define RTAI_SYSCALL_RETPNT  edx

#define LINUX_SYSCALL_NR      orig_eax
#define LINUX_SYSCALL_REG1    ebx
#define LINUX_SYSCALL_REG2    ecx
#define LINUX_SYSCALL_REG3    edx
#define LINUX_SYSCALL_REG4    esi
#define LINUX_SYSCALL_REG5    edi
#define LINUX_SYSCALL_REG6    ebp
#define LINUX_SYSCALL_RETREG  eax

#define SET_LXRT_RETVAL_IN_SYSCALL(retval) \
	do { \
                if (r->RTAI_SYSCALL_RETPNT) { \
			copy_to_user((void *)r->RTAI_SYSCALL_RETPNT, &retval, sizeof(retval)); \
		} \
	} while (0)

#define LOW  0
#define HIGH 1

#ifdef CONFIG_X86_LOCAL_APIC

#define TIMER_NAME "APIC"
#define FAST_TO_READ_TSC
#define TIMER_FREQ RTAI_FREQ_APIC
#define TIMER_LATENCY RTAI_LATENCY_APIC
#define TIMER_SETUP_TIME RTAI_SETUP_TIME_APIC
#define ONESHOT_SPAN (0x7FFFFFFFLL*(CPU_FREQ/TIMER_FREQ))
#define update_linux_timer(cpuid)

#else /* !CONFIG_X86_LOCAL_APIC */

#define USE_LINUX_TIMER
#define TIMER_NAME "8254-PIT"
#define TIMER_FREQ RTAI_FREQ_8254
#define TIMER_LATENCY RTAI_LATENCY_8254
#define TIMER_SETUP_TIME RTAI_SETUP_TIME_8254
#define ONESHOT_SPAN (0x7FFF*(CPU_FREQ/TIMER_FREQ))
#define update_linux_timer(cpuid) adeos_pend_uncond(TIMER_8254_IRQ, cpuid)

#endif /* CONFIG_X86_LOCAL_APIC */

union rtai_lxrt_t {

    RTIME rt;
    int i[2];
    void *v[2];
};

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifdef __KERNEL__

#include <asm/segment.h>
#include <asm/mmu_context.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#define __LXRT_GET_DATASEG(reg) "movl $" STR(__KERNEL_DS) ",%" #reg "\n\t"
#else /* KERNEL_VERSION >= 2.6.0 */
#define __LXRT_GET_DATASEG(reg) "movl $" STR(__USER_DS) ",%" #reg "\n\t"
#endif  /* KERNEL_VERSION < 2.6.0 */

static inline void _lxrt_context_switch (struct task_struct *prev,
					 struct task_struct *next,
					 int cpuid)
{
    struct mm_struct *oldmm = prev->active_mm;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
    switch_mm(oldmm,next->active_mm,next,cpuid);

    if (!next->mm)
	enter_lazy_tlb(oldmm,next,cpuid);
#else /* >= 2.6.0 */
    switch_mm(oldmm,next->active_mm,next);

    if (!next->mm) enter_lazy_tlb(oldmm,next);
#endif /* < 2.6.0 */

/* NOTE: Do not use switch_to() directly: this is a compiler
   compatibility issue. */

/* It might be so but, with 2.6.xx at least, only for the inlined case. 
   Compiler compatibility issues related to inlines can appear anywhere.
   This case seems to be solved by staticalising without inlining, see LXRT.
   So let's experiment a bit more, simple Linux reuse is better (Paolo) */

#if 1
    switch_to(prev, next, prev);
#else
    __asm__ __volatile__(						\
		 "pushfl\n\t"				       		\
		 "cli\n\t"				       		\
		 "pushl %%esi\n\t"				        \
		 "pushl %%edi\n\t"					\
		 "pushl %%ebp\n\t"					\
		 "movl %%esp,%0\n\t"	/* save ESP */			\
		 "movl %3,%%esp\n\t"	/* restore ESP */		\
		 "movl $1f,%1\n\t"	/* save EIP */			\
		 "pushl %4\n\t"		/* restore EIP */		\
		 "jmp "SYMBOL_NAME_STR(__switch_to)"\n"			\
		 "1:\t"							\
		 "popl %%ebp\n\t"					\
		 "popl %%edi\n\t"					\
		 "popl %%esi\n\t"					\
		 "popfl\n\t"						\
		 :"=m" (prev->thread.esp),"=m" (prev->thread.eip),	\
		  "=b" (prev)						\
		 :"m" (next->thread.esp),"m" (next->thread.eip),	\
		  "a" (prev), "d" (next),				\
		  "b" (prev));						
#endif
    barrier();
}

#if 0
#define IN_INTERCEPT_IRQ_ENABLE()   do { rtai_hw_sti(); } while (0)
#define IN_INTERCEPT_IRQ_DISABLE()  do { rtai_hw_cli(); } while (0)
#else
#define IN_INTERCEPT_IRQ_ENABLE()   do { } while (0)
#define IN_INTERCEPT_IRQ_DISABLE()  do { } while (0)
#endif

#else /* !__KERNEL__ */

/* NOTE: Keep the following routines unfold: this is a compiler
   compatibility issue. */

static union rtai_lxrt_t _rtai_lxrt(int srq, void *arg)
{
	union rtai_lxrt_t retval;
#ifdef USE_LINUX_SYSCALL
	RTAI_DO_TRAP(SYSCALL_VECTOR, retval, srq, arg);
#else
	RTAI_DO_TRAP(RTAI_SYS_VECTOR, retval, srq, arg);
#endif
	return retval;
}

static inline union rtai_lxrt_t rtai_lxrt(short int dynx, short int lsize, int srq, void *arg)
{
	return _rtai_lxrt(ENCODE_LXRT_REQ(dynx, srq, lsize), arg);
}

#define rtai_iopl()  do { extern int iopl(int); iopl(3); } while (0)

#endif /* __KERNEL__ */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* !_RTAI_ASM_I386_LXRT_H */
