/*
 * Copyright (C) 1999-2007 Paolo Mantegazza <mantegazza@aero.polimi.it>
 *		   2000 Pierre Cloutier <pcloutier@poseidoncontrols.com>
		   2002 Steve Papacharalambous <stevep@zentropix.com>
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

#ifndef _RTAI_ASM_PPC_LXRT_H
#define _RTAI_ASM_PPC_LXRT_H

#include <linux/version.h>

#include <asm/rtai_vectors.h>

#ifdef CONFIG_RTAI_LXRT_USE_LINUX_SYSCALL
#define USE_LINUX_SYSCALL
#else
#undef USE_LINUX_SYSCALL
#endif

#define RTAI_SYSCALL_NR      0x70000000
#define RTAI_SYSCALL_CODE    rdi
#define RTAI_SYSCALL_ARGS    rsi
#define RTAI_SYSCALL_RETPNT  rdx

#define RTAI_FAKE_LINUX_SYSCALL  39

#define NR_syscalls __NR_syscall_max

#define LINUX_SYSCALL_NR      gpr[0]
#define LINUX_SYSCALL_REG1    gpr[1]
#define LINUX_SYSCALL_REG2    gpr[2]
#define LINUX_SYSCALL_REG3    gpr[3]
#define LINUX_SYSCALL_REG4    gpr[4]
#define LINUX_SYSCALL_REG5    gpr[5]
#define LINUX_SYSCALL_REG6    gpr[6]
#define LINUX_SYSCALL_RETREG  gpr[0]
#define LINUX_SYSCALL_FLAGS   msr

#define LXRT_DO_IMMEDIATE_LINUX_SYSCALL(regs) \
        do { \
        } while (0)

#define SET_LXRT_RETVAL_IN_SYSCALL(regs, retval) \
        do { \
        } while (0)

#define LOW   1
#define HIGH  0

#define USE_LINUX_TIMER
#define TIMER_NAME        "DECREMENTER"
#define TIMER_FREQ        RTAI_FREQ_8254
#define TIMER_LATENCY     RTAI_LATENCY_8254
#define TIMER_SETUP_TIME  RTAI_SETUP_TIME_8254
#define ONESHOT_SPAN      ((0x7FFF*(CPU_FREQ/TIMER_FREQ))/(CONFIG_RTAI_CAL_FREQS_FACT + 1)) //(0x7FFF*(CPU_FREQ/TIMER_FREQ))
#define update_linux_timer(cpuid)  hal_pend_uncond(TIMER_8254_IRQ, cpuid)

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
        extern void *context_switch(void *, void *, void *);
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18)
        prev->fpu_counter = 0;
#endif
        context_switch(NULL, prev, next);
}

#define IN_INTERCEPT_IRQ_ENABLE()   do { } while (0)
#define IN_INTERCEPT_IRQ_DISABLE()  do { } while (0)

static inline void kthread_fun_set_jump(struct task_struct *lnxtsk)
{
}

static inline void kthread_fun_long_jump(struct task_struct *lnxtsk)
{
}

#define rt_copy_from_user(a, b, c)  \
	( { int ret = __copy_from_user_inatomic(a, b, c); ret; } )

#define rt_copy_to_user(a, b, c)  \
	( { int ret = __copy_to_user_inatomic(a, b, c); ret; } )

#define rt_put_user  __put_user
#define rt_get_user  __get_user

#define rt_strncpy_from_user(a, b, c)  \
	( { int ret = strncpy_from_user(a, b, c); ret; } )

#else /* !__KERNEL__ */

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

#endif /* !_RTAI_ASM_PPC_LXRT_H */
