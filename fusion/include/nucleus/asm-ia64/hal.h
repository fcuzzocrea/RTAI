/**
 *   @ingroup hal
 *   @file
 *
 *   Real-Time Hardware Abstraction Layer for the ia64 architecture.
 *
 *   Copyright &copy; 2002-2004 Philippe Gerum
 *   Copyright &copy; 2004 The HYADES project <http://www.hyades-itea.org>
 *
 *   RTAI/fusion is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License as
 *   published by the Free Software Foundation, Inc., 675 Mass Ave,
 *   Cambridge MA 02139, USA; either version 2 of the License, or (at
 *   your option) any later version.
 *
 *   RTAI/fusion is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with RTAI/fusion; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *   02111-1307, USA.
 */

#ifndef _RTAI_ASM_IA64_HAL_H
#define _RTAI_ASM_IA64_HAL_H

#include <nucleus/asm-generic/hal.h>	/* Read the generic bits. */

typedef unsigned long long rthal_time_t;

static inline unsigned long long rthal_ullmul(const unsigned long m0, 
					      const unsigned long m1)
{
    return (unsigned long long) m0 * m1;
}

static inline unsigned long long rthal_ulldiv (unsigned long long ull,
					       const unsigned long uld,
					       unsigned long *const rp)
{
    unsigned long long result = ull / uld;

    if (rp)
        *rp = ull % uld;

    return result;
}

#define rthal_uldivrem(ull,ul,rp) ((u_long) rthal_ulldiv((ull),(ul),(rp)))

static inline int rthal_imuldiv (int i, int mult, int div)
{
    return ((long long)i * mult) / div;
}

static inline long long rthal_llimd (long long op,
                                     unsigned long m,
                                     unsigned long d)
{
    long long h, l, qh, ql, rh;

    /* (ll * mult) may need 96 bits, so we split it into two parts. We use / and
       % on purpose, not shift operations, to handle correctly negative numbers.
    */
    h = (op / (1LL << 32)) * m;
    l = (op % (1LL << 32)) * m;
    h += l / (1LL << 32);
    l %= (1LL << 32);

    rh = (h % d) * (1LL << 32);
    qh = (h / d) * (1LL << 32);
    ql = (rh + l) / d;

    return qh + ql;
}

static inline __attribute_const__ unsigned long ffnz (unsigned long ul)
{
    unsigned long r;
    asm ("popcnt %0=%1" : "=r" (r) : "r" ((ul-1) & ~ul));
    return r;
}

#if defined(__KERNEL__) && !defined(__cplusplus)
#include <asm/system.h>
#include <nucleus/asm/atomic.h>
#include <asm/processor.h>
#include <asm/delay.h>          /* For ia64_get_itc / ia64_set_itm */

#define RTHAL_TIMER_VECTOR      ADEOS_SERVICE_VECTOR3
#define RTHAL_TIMER_IRQ         ADEOS_SERVICE_IPI3
#define RTHAL_HOST_TIMER_VECTOR IA64_TIMER_VECTOR
#define RTHAL_HOST_TIMER_IRQ    __ia64_local_vector_to_irq(IA64_TIMER_VECTOR)

#define rthal_irq_descp(irq)  irq_descp(irq)

static inline unsigned long long rthal_rdtsc (void)
{
    unsigned long long t;
    adeos_hw_tsc(t);
    return t;
}

static inline struct task_struct *rthal_root_host_task (int cpuid)
{
    return current;
}

static inline struct task_struct *rthal_current_host_task (int cpuid)
{
    return current;
}

static inline void rthal_timer_program_shot (unsigned long delay)
{
    unsigned long flags;
    if (!delay) { delay = 10; }
    rthal_hw_lock(flags);
    ia64_set_itm(ia64_get_itc() + delay);
    rthal_hw_unlock(flags);
}

    /* Private interface -- Internal use only */

void rthal_switch_context(void *out_tcb,
			  void *in_tcb);

void rthal_prepare_stack(unsigned long stackbase);

static const char *const rthal_fault_labels[] = {
    [0] = "General exception",
    [1] = "FPU disabled",
    [2] = "NaT consumption",
    [3] = "Unsupported data reference",
    [4] = "Debug",
    [5] = "FPU fault",
    [6] = "Unimplemented instruction address",
    [7] = "ia32 exception",
    [8] = "Generic fault",
    [9] = "Page fault",
    [10] = NULL
};

#endif /* __KERNEL__ && !__cplusplus */

#endif /* !_RTAI_ASM_IA64_HAL_H */
