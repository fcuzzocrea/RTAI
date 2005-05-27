/*
 * Copyright &copy; 2003 Philippe Gerum <rpm@xenomai.org>.
 * Copyright &copy; 2004 The HYADES project <http://www.hyades-itea.org>
 *
 * RTAI/fusion is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * RTAI/fusion is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with RTAI/fusion; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _RTAI_ASM_IA64_ATOMIC_H
#define _RTAI_ASM_IA64_ATOMIC_H

#ifdef __KERNEL__

#include <linux/bitops.h>
#include <asm/atomic.h>
#include <asm/system.h>

#define atomic_xchg(ptr,v)       ia64_xchg8(ptr,v)
#define atomic_cmpxchg(ptr,o,n)  ia64_cmpxchg8_acq(ptr,o,n)
#define xnarch_memory_barrier()  smp_mb()

#else /* !__KERNEL__ */

#ifndef likely
#if __GNUC__ == 2 && __GNUC_MINOR__ < 96
#define __builtin_expect(x, expected_value) (x)
#endif
#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)
#endif /* !likely */

#define fls(x) generic_fls(x)

#include <linux/bitops.h>
#include <asm/atomic.h>

struct __rtai_xchg_dummy { unsigned long a[100]; };
#define __rtai_xg(x) ((struct __rtai_xchg_dummy *)(x))

static inline unsigned long atomic_xchg (volatile void *ptr,
					 unsigned long x)
{
	__u64 ia64_intri_res;						
	asm __volatile ("xchg8 %0=[%1],%2" : "=r" (ia64_intri_res)	
			    : "r" (ptr), "r" (x) : "memory");		
	return ia64_intri_res;
}

static inline unsigned long atomic_cmpxchg (volatile void *ptr,
					    unsigned long o,
					    unsigned long n)
{
    __u64 ia64_intri_res;

    asm volatile ("mov ar.ccv=%0;;" :: "rO"(o));
    asm volatile ("cmpxchg8.acq %0=[%1],%2,ar.ccv":			
                  "=r"(ia64_intri_res) : "r"(ptr), "r"(n) : "memory");
        
    return ia64_intri_res;
}

#define xnarch_memory_barrier()  __asm__ __volatile__("": : :"memory")

/* Depollute the namespace a bit. */
#undef ADDR

#endif /* __KERNEL__ */

typedef atomic_t atomic_counter_t;
typedef unsigned long atomic_flags_t;

#define xnarch_atomic_set(pcounter,i)          atomic_set(pcounter,i)
#define xnarch_atomic_get(pcounter)            atomic_read(pcounter)
#define xnarch_atomic_inc(pcounter)            atomic_inc(pcounter)
#define xnarch_atomic_dec(pcounter)            atomic_dec(pcounter)
#define xnarch_atomic_inc_and_test(pcounter)   atomic_inc_and_test(pcounter)
#define xnarch_atomic_dec_and_test(pcounter)   atomic_dec_and_test(pcounter)

/* These functions actually only work on the first 32 bits word of the
   64 bits status word whose address is addr. But the bit fields used
   by the nucleus have to work on 32 bits architectures anyway. */
static inline void atomic_set_mask(unsigned mask, unsigned long *addr)
{
    __u32 old, new;
    volatile __u32 *m;
    CMPXCHG_BUGCHECK_DECL
        
    m = (volatile __u32 *) addr;
    do {
        CMPXCHG_BUGCHECK(m);
        old = *m;
        new = old | mask;
    } while (cmpxchg_acq(m, old, new) != old);
}

static inline void atomic_clear_mask(unsigned mask, unsigned long *addr)
{
    __u32 old, new;
    volatile __u32 *m;
    CMPXCHG_BUGCHECK_DECL
        
    m = (volatile __u32 *) addr;
    do {
        CMPXCHG_BUGCHECK(m);
        old = *m;
        new = old & ~mask;
    } while (cmpxchg_acq(m, old, new) != old);
}

#define xnarch_atomic_set_mask(pflags,mask)    atomic_set_mask(mask,pflags)
#define xnarch_atomic_clear_mask(pflags,mask)  atomic_clear_mask(mask,pflags)

#define xnarch_atomic_xchg(ptr,x) atomic_xchg(ptr,x)

#endif /* !_RTAI_ASM_IA64_ATOMIC_H */
