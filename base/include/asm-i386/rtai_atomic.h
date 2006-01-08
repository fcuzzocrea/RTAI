/*
 * Copyright (C) 2003 Philippe Gerum <rpm@xenomai.org>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _RTAI_ASM_I386_ATOMIC_H
#define _RTAI_ASM_I386_ATOMIC_H

#include <linux/bitops.h>

#ifdef __KERNEL__

#include <asm/atomic.h>
#undef atomic_cmpxchg /* they might have decided to use one of our names */

#include <asm/system.h>

typedef struct { volatile unsigned long val; } rtai_atomic_t;

/* #define atomic_xchg(ptr, v)  xchg(ptr, v) */ /*previous define */
#define atomic_xchg(ptr, v) \
	((__typeof__(*(ptr)))xchg(&(((rtai_atomic_t *)ptr)->val),v))

/* #define atomic_cmpxchg(ptr, o, n)  cmpxchg(ptr, o, n) */ /*previous define */
#define atomic_cmpxchg(ptr, o, n) \
	((__typeof__(*(ptr)))cmpxchg(&(((rtai_atomic_t *)ptr)->val), o, n))

#else /* !__KERNEL__ */

#ifndef likely
#if __GNUC__ == 2 && __GNUC_MINOR__ < 96
#define __builtin_expect(x, expected_value) (x)
#endif
#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)
#endif /* !likely */

#include <asm/atomic.h>
#undef atomic_cmpxchg /* they might have decided to use one of our names */

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

/* Depollute the namespace a bit. */
#undef ADDR

#endif /* __KERNEL__ */

#endif /* !_RTAI_ASM_I386_ATOMIC_H */
