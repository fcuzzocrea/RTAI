/*
 * Copyright (C) 1999-2003 Paolo Mantegazza <mantegazza@aero.polimi.it>
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

#include <asm/rtai_vectors.h>

#define LOW   1
#define HIGH  0

#define LINUX_SYSCALL_NR      gpr[0]
#define LINUX_SYSCALL_REG1    gpr[1]
#define LINUX_SYSCALL_REG2    gpr[2]
#define LINUX_SYSCALL_REG3    gpr[3]
#define LINUX_SYSCALL_REG4    gpr[4]
#define LINUX_SYSCALL_REG5    gpr[5]
#define LINUX_SYSCALL_REG6    gpr[6]
#define LINUX_SYSCALL_RETREG  gpr[0]

#ifdef __KERNEL__

#include <asm/segment.h>
#include <asm/mmu_context.h>

#include <linux/interrupt.h>

#define USE_LINUX_TIMER
#define TIMER_NAME "8254-PIT"
#define TIMER_FREQ RTAI_FREQ_8254
#define TIMER_LATENCY RTAI_LATENCY_8254
#define TIMER_SETUP_TIME RTAI_SETUP_TIME_8254
#define ONESHOT_SPAN (0x7FFF*(CPU_FREQ/TIMER_FREQ))
#define update_linux_timer(cpuid) adeos_pend_uncond(TIMER_8254_IRQ, cpuid)

#define IN_INTERCEPT_IRQ_ENABLE()       do { /* nop */ } while (0)

#define RTAI_SYSCALL_NR      gpr[0]

static inline void _lxrt_context_switch (struct task_struct *prev, struct task_struct *next, int cpuid)
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

    switch_to(prev, next, prev);
    barrier();
}

#else /* !__KERNEL__ */

union rtai_lxrt_t { RTIME rt; int i[2]; void *v[2]; };

static inline union rtai_lxrt_t rtai_lxrt(short dynx, short lsize, unsigned long srq, void *arg)
{
    /* LXRT is not yet available on PPC. */
    union rtai_lxrt_t retval;
    retval.i[0] = -1;
    retval.i[1] = -1;
    return retval;
}

#define rtai_iopl()  do { /* nop */ } while (0)

#endif /* !__KERNEL__ */

#endif /* !_RTAI_ASM_PPC_LXRT_H */
