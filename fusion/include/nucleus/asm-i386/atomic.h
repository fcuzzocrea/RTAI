/*
 * Copyright (C) 2003 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _RTAI_ASM_I386_ATOMIC_H
#define _RTAI_ASM_I386_ATOMIC_H

#include <linux/bitops.h>
#include <asm/atomic.h>

#ifdef __KERNEL__

#include <asm/system.h>

#define atomic_xchg(ptr,v)       xchg(ptr,v)
#define atomic_cmpxchg(ptr,o,n)  cmpxchg(ptr,o,n)
#define xnarch_memory_barrier()  smp_mb()

#else /* !__KERNEL__ */

struct __rtai_xchg_dummy { unsigned long a[100]; };
#define __rtai_xg(x) ((struct __rtai_xchg_dummy *)(x))

static inline unsigned long atomic_xchg (volatile void *ptr,
					 unsigned long x)
{
    __asm__ __volatile__(LOCK_PREFIX "xchgl %0,%1"
			 :"=r" (x)
			 :"m" (*__rtai_xg(ptr)), "0" (x)
			 :"memory");
    return x;
}

static inline unsigned long atomic_cmpxchg (volatile void *ptr,
					    unsigned long o,
					    unsigned long n)
{
    unsigned long prev;

    __asm__ __volatile__(LOCK_PREFIX "cmpxchgl %1,%2"
			 : "=a"(prev)
			 : "q"(n), "m" (*__rtai_xg(ptr)), "0" (o)
			 : "memory");

    return prev;
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
#define xnarch_atomic_set_mask(pflags,mask)    atomic_set_mask(mask,pflags)
#define xnarch_atomic_clear_mask(pflags,mask)  atomic_clear_mask(mask,pflags)

#define xnarch_atomic_xchg(ptr,x) atomic_xchg(ptr,x)

#endif /* !_RTAI_ASM_I386_ATOMIC_H */
