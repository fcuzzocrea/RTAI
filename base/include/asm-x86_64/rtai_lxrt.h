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

//#define USE_LINUX_SYSCALL

#define RTAI_SYSCALL_NR      0x70000000
#define RTAI_SYSCALL_CODE    rdi
#define RTAI_SYSCALL_ARGS    rsi
#define RTAI_SYSCALL_RETPNT  rdx

#define RTAI_FAKE_LINUX_SYSCALL  39

#define LINUX_SYSCALL_NR      orig_rax
#define LINUX_SYSCALL_REG1    rdi
#define LINUX_SYSCALL_REG2    rsi
#define LINUX_SYSCALL_REG3    rdx
#define LINUX_SYSCALL_REG4    r10
#define LINUX_SYSCALL_REG5    r8
#define LINUX_SYSCALL_REG6    r9
#define LINUX_SYSCALL_RETREG  rax

#define SET_LXRT_RETVAL_IN_SYSCALL(retval) \
	do { \
                if (r->RTAI_SYSCALL_RETPNT) { \
			rt_copy_to_user((void *)r->RTAI_SYSCALL_RETPNT, &retval, sizeof(retval)); \
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

static inline void _lxrt_context_switch (struct task_struct *prev, struct task_struct *next, int cpuid)
{
	extern void context_switch(void *, void *, void *);
	context_switch(0, prev, next);
}

#if 0
#define IN_INTERCEPT_IRQ_ENABLE()   do { rtai_hw_sti(); } while (0)
#define IN_INTERCEPT_IRQ_DISABLE()  do { rtai_hw_cli(); } while (0)
#else
#define IN_INTERCEPT_IRQ_ENABLE()   do { } while (0)
#define IN_INTERCEPT_IRQ_DISABLE()  do { } while (0)
#endif

#if 0 // optimised (?)
static inline void kthread_fun_set_jump(struct task_struct *lnxtsk)
{
	lnxtsk->rtai_tskext(2) = kmalloc(sizeof(struct thread_struct)/* + sizeof(struct thread_info)*/ + (lnxtsk->thread.rsp & ~(THREAD_SIZE - 1)) + THREAD_SIZE - lnxtsk->thread.rsp, GFP_KERNEL);
	*((struct thread_struct *)lnxtsk->rtai_tskext(2)) = lnxtsk->thread;
//	memcpy(lnxtsk->rtai_tskext(2) + sizeof(struct thread_struct), (void *)(lnxtsk->thread.rsp & ~(THREAD_SIZE - 1)), sizeof(struct thread_info));
	memcpy(lnxtsk->rtai_tskext(2) + sizeof(struct thread_struct)/* + sizeof(struct thread_info)*/, (void *)(lnxtsk->thread.rsp), (lnxtsk->thread.rsp & ~(THREAD_SIZE - 1)) + THREAD_SIZE - lnxtsk->thread.rsp);
}

static inline void kthread_fun_long_jump(struct task_struct *lnxtsk)
{
	lnxtsk->thread = *((struct thread_struct *)lnxtsk->rtai_tskext(2));
//	memcpy((void *)(lnxtsk->thread.rsp & ~(THREAD_SIZE - 1)), lnxtsk->rtai_tskext(2) + sizeof(struct thread_struct), sizeof(struct thread_info));
	memcpy((void *)lnxtsk->thread.rsp, lnxtsk->rtai_tskext(2) + sizeof(struct thread_struct)/* + sizeof(struct thread_info)*/, (lnxtsk->thread.rsp & ~(THREAD_SIZE - 1)) + THREAD_SIZE - lnxtsk->thread.rsp);
}
#else  // brute force
static inline void kthread_fun_set_jump(struct task_struct *lnxtsk)
{
	lnxtsk->rtai_tskext(2) = kmalloc(sizeof(struct thread_struct) + THREAD_SIZE, GFP_KERNEL);
	*((struct thread_struct *)lnxtsk->rtai_tskext(2)) = lnxtsk->thread;
	memcpy(lnxtsk->rtai_tskext(2) + sizeof(struct thread_struct), (void *)(lnxtsk->thread.rsp & ~(THREAD_SIZE - 1)), THREAD_SIZE);
}

static inline void kthread_fun_long_jump(struct task_struct *lnxtsk)
{
	lnxtsk->thread = *((struct thread_struct *)lnxtsk->rtai_tskext(2));
	memcpy((void *)(lnxtsk->thread.rsp & ~(THREAD_SIZE - 1)), lnxtsk->rtai_tskext(2) + sizeof(struct thread_struct), THREAD_SIZE);
}
#endif

#define rt_copy_from_user  __copy_from_user_inatomic
#define rt_copy_to_user    __copy_to_user_inatomic
#define rt_put_user        __put_user

#else /* !__KERNEL__ */

/* NOTE: Keep the following routines unfold: this is a compiler
   compatibility issue. */

#include <sys/syscall.h>
#include <unistd.h>

static union rtai_lxrt_t _rtai_lxrt(long srq, void *arg)
{
	union rtai_lxrt_t retval;
#ifdef USE_LINUX_SYSCALL
	syscall(RTAI_SYSCALL_NR, srq, arg, &retval);
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
