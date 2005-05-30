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

#ifndef _RTAI_ASM_X8664_LXRT_H
#define _RTAI_ASM_X8664_LXRT_H

#include <asm/rtai_vectors.h>

#define RTAI_SYSCALL_NR      rax
#define RTAI_SYSCALL_ARGS    rcx
#define RTAI_SYSCALL_RETPNT  rdx

#define LINUX_SYSCALL_NR      rax
#define LINUX_SYSCALL_REG1    rdi
#define LINUX_SYSCALL_REG2    rsi
#define LINUX_SYSCALL_REG3    rdx
#define LINUX_SYSCALL_REG4    r8
#define LINUX_SYSCALL_REG5    r10
#define LINUX_SYSCALL_REG6    r9
#define LINUX_SYSCALL_RETREG  rax

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
    long i[1];
    void *v[1];
};

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifdef __KERNEL__

#include <asm/segment.h>
#include <asm/mmu_context.h>

#if 0
#define switch_to_x86_64(prev,next,last) \
	asm volatile( \
        "cli\n" \
                "pushfq\n" \
                "pushq %%rdi\n" \
                "pushq %%rsi\n" \
                "pushq %%rdx\n" \
                "pushq %%rcx\n" \
                "pushq %%rax\n" \
                "pushq %%r8\n" \
                "pushq %%r9\n" \
                "pushq %%r10\n" \
                "pushq %%r11\n" \
                "pushq %%rbx\n" \
                "pushq %%rbp\n" \
                "pushq %%r12\n" \
                "pushq %%r13\n" \
                "pushq %%r14\n" \
                "pushq %%r15\n" \
		"movq %%rsp,%P[threadrsp](%[prev])\n\t" /* save RSP */ \
		"movq %P[threadrsp](%[next]),%%rsp\n\t" /* restore RSP */ \
		"call __switch_to\n\t" \
        "popq %%r15\n" \
        "popq %%r14\n" \
        "popq %%r13\n" \
        "popq %%r12\n" \
        "popq %%rbp\n" \
        "popq %%rbx\n" \
        "popq %%r11\n" \
        "popq %%r10\n" \
        "popq %%r9\n" \
        "popq %%r8\n" \
        "popq %%rax\n" \
        "popq %%rcx\n" \
        "popq %%rdx\n" \
        "popq %%rsi\n" \
        "popq %%rdi\n" \
        "popfq\n" \
        "sti\n" \
		: "=a" (last) \
		: [next] "S" (next), [prev] "D" (prev),  \
		[threadrsp] "i" (offsetof(struct task_struct, thread.rsp)), \
		[ti_flags] "i" (offsetof(struct thread_info, flags)), \
		[tif_fork] "i" (TIF_FORK), \
		[thread_info] "i" (offsetof(struct task_struct, thread_info)), \
		[pda_pcurrent] "i" (offsetof(struct x8664_pda, pcurrent)) \
		: "memory", "cc" __EXTRA_CLOBBER)
#endif

#define switch_to_x86_64(prev,next,last) \
	asm volatile(SAVE_CONTEXT						    \
		     "movq %%rsp,%P[threadrsp](%[prev])\n\t" /* save RSP */	  \
		     "movq %P[threadrsp](%[next]),%%rsp\n\t" /* restore RSP */	  \
		     "call __switch_to\n\t"					  \
		     ".globl thread_return\n"					\
		     "thread_return:\n\t"					    \
		     "movq %%gs:%P[pda_pcurrent],%%rsi\n\t"			  \
		     "movq %P[thread_info](%%rsi),%%r8\n\t"			  \
		     "btr  %[tif_fork],%P[ti_flags](%%r8)\n\t"			  \
		     "movq %%rax,%%rdi\n\t" 					  \
		     RESTORE_CONTEXT						    \
		     : "=a" (last)					  	  \
		     : [next] "S" (next), [prev] "D" (prev),			  \
		       [threadrsp] "i" (offsetof(struct task_struct, thread.rsp)), \
		       [ti_flags] "i" (offsetof(struct thread_info, flags)),\
		       [tif_fork] "i" (TIF_FORK),			  \
		       [thread_info] "i" (offsetof(struct task_struct, thread_info)), \
		       [pda_pcurrent] "i" (offsetof(struct x8664_pda, pcurrent))   \
		     : "memory", "cc" __EXTRA_CLOBBER)
#if 0		     
#define fake_ret_from_fork  __asm__ __volatile__ (\
	".global ret_from_fork\n" \
	"ret_from_fork:" \
	RESTORE_CONTEXT)
#endif

static inline void _lxrt_context_switch (struct task_struct *prev, struct task_struct *next, int cpuid)
{
	struct mm_struct *oldmm = prev->active_mm;

	switch_mm(oldmm,next->active_mm,next);
	if (!next->mm) enter_lazy_tlb(oldmm,next);
	switch_to_x86_64(prev, next, prev);
//	switch_to(prev, next, prev);
//	fake_ret_from_fork;
	
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

static union rtai_lxrt_t _rtai_lxrt(long srq, void *arg)
{
	union rtai_lxrt_t retval;
#ifdef USE_LINUX_SYSCALL
	RTAI_DO_TRAP(SYSCALL_VECTOR, retval, srq, arg);
#else
	RTAI_DO_TRAP(RTAI_SYS_VECTOR, retval, srq, arg);
#endif
	return retval;
}

static inline union rtai_lxrt_t rtai_lxrt(long dynx, long lsize, long srq, void *arg)
{
	return _rtai_lxrt(ENCODE_LXRT_REQ(dynx, srq, lsize), arg);
}

#define rtai_iopl()  do { extern int iopl(int); iopl(3); } while (0)

#endif /* __KERNEL__ */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* !_RTAI_ASM_X8664_LXRT_H */
