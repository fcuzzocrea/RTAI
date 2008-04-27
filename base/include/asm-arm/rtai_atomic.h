/*
 * Atomic operations.
 *
 * Original RTAI/x86 layer implementation:
 *   Copyright (c) 2000 Paolo Mantegazza (mantegazza@aero.polimi.it)
 *   Copyright (c) 2000 Steve Papacharalambous (stevep@zentropix.com)
 *   Copyright (c) 2000 Stuart Hughes
 *   and others.
 *
 * RTAI/x86 rewrite over Adeos:
 *   Copyright (c) 2002 Philippe Gerum (rpm@xenomai.org)
 *
 * Original RTAI/ARM RTHAL implementation:
 *   Copyright (c) 2000 Pierre Cloutier (pcloutier@poseidoncontrols.com)
 *   Copyright (c) 2001 Alex Züpke, SYSGO RTS GmbH (azu@sysgo.de)
 *   Copyright (c) 2002 Guennadi Liakhovetski DSA GmbH (gl@dsa-ac.de)
 *   Copyright (c) 2002 Steve Papacharalambous (stevep@zentropix.com)
 *   Copyright (c) 2002 Wolfgang Müller (wolfgang.mueller@dsa-ac.de)
 *   Copyright (c) 2003 Bernard Haible, Marconi Communications
 *   Copyright (c) 2003 Thomas Gleixner (tglx@linutronix.de)
 *   Copyright (c) 2003 Philippe Gerum (rpm@xenomai.org)
 *
 * RTAI/ARM over Adeos rewrite:
 *   Copyright (c) 2004-2005 Michael Neuhauser, Firmix Software GmbH (mike@firmix.at)
 *
 *  RTAI/ARM over Adeos rewrite for PXA255_2.6.7:
 *   Copyright (c) 2005 Stefano Gafforelli (stefano.gafforelli@tiscali.it)
 *   Copyright (c) 2005 Luca Pizzi (lucapizzi@hotmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge MA 02139, USA; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#ifndef _RTAI_ASM_ARM_ATOMIC_H
#define _RTAI_ASM_ARM_ATOMIC_H

#include <linux/version.h>
#include <linux/bitops.h>
#include <rtai_config.h>
#include <asm/rtai_hal.h>

#ifdef __KERNEL__

#include <asm/system.h>
#include <asm/atomic.h>

#define atomic_xchg(ptr,v)      xchg(ptr,v)

/* Poor man's cmpxchg(). */
#define atomic_cmpxchg(p, o, n) ({	\
	typeof(*(p)) __o = (o);		\
	typeof(*(p)) __n = (n);		\
	typeof(*(p)) __prev;		\
	unsigned long flags;		\
	rtai_hw_lock(flags);		\
	__prev = *(p);			\
	if (__prev == __o)		\
		*(p) = __n;		\
	rtai_hw_unlock(flags);		\
	__prev; })

#else /* !__KERNEL__ */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#include <asm/proc/system.h>
#else
#include <asm/system.h>
#endif

typedef struct { volatile int counter; } atomic_t;

static inline unsigned long
atomic_xchg(volatile void *ptr, unsigned long x)
{
    asm volatile(
	"swp %0, %1, [%2]"
	: "=&r" (x)
	: "r" (x), "r" (ptr)
	: "memory"
    );
    return x;
}

static inline unsigned long
atomic_cmpxchg(volatile void *ptr, unsigned long o, unsigned long n)
{
    unsigned long prev;
    unsigned long flags;
    adeos_hw_local_irq_save(flags);
    prev = *(unsigned long*)ptr;
    if (prev == o)
	*(unsigned long*)ptr = n;
    adeos_hw_local_irq_restore(flags);
    return prev;
}

#endif /* __KERNEL__ */
#endif /* !_RTAI_ASM_ARM_ATOMIC_H */
