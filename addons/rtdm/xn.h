/*
 * Copyright (C) 2005 Paolo Mantegazza <mantegazza@aero.polimi.it>
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


// A few wrappers and inlines to avoid too much an editing. 
// The core stuff is just RTAI in disguise.

#ifndef _RTAI_XNSTUFF_H
#define _RTAI_XNSTUFF_H

#include <linux/proc_fs.h>
#include <asm/uaccess.h>

//recursive smp locks, as for RTAI global stuff + a name

#define XNARCH_LOCK_UNLOCKED  (xnlock_t) { 0 }

typedef unsigned long spl_t;
typedef struct { volatile unsigned long lock; } xnlock_t;

extern xnlock_t nklock;

#ifdef CONFIG_SMP

static inline void xnlock_init(xnlock_t *lock)
{
	*lock = XNARCH_LOCK_UNLOCKED;
}

static inline spl_t __xnlock_get_irqsave(xnlock_t *lock)
{
	unsigned long flags;

	barrier();
	rtai_save_flags_and_cli(flags);
	flags &= (1 << RTAI_IFLAG);
	if (!test_and_set_bit(hal_processor_id(), &lock->lock)) {
		while (test_and_set_bit(31, &lock->lock)) {
			cpu_relax();
		}
		barrier();
		return flags | 1;
	}
	barrier();
	return flags;
}

#define xnlock_get_irqsave(lock, flags)  \
	do { flags = __xnlock_get_irqsave(lock); } while (0)

static inline void xnlock_put_irqrestore(xnlock_t *lock, spl_t flags)
{
	barrier();
	rtai_cli();
	if (test_and_clear_bit(0, &flags)) {
		if (test_and_clear_bit(hal_processor_id(), &lock->lock)) {
			test_and_clear_bit(31, &lock->lock);
			cpu_relax();
		}
	} else {
		if (!test_and_set_bit(hal_processor_id(), &lock->lock)) {
			while (test_and_set_bit(31, &lock->lock)) {
				cpu_relax();
			}
		}
	}
	if (flags) {
		rtai_sti();
	}
	barrier();
}

#else /* !CONFIG_SMP */

#define xnlock_init(lock)                   do { } while(0)
#define xnlock_get_irqsave(lock, flags)     rtai_save_flags_and_cli(flags)
#define xnlock_put_irqrestore(lock, flags)  rtai_restore_flags(flags)

#endif /* CONFIG_SMP */

// memory allocation

#define xnmalloc  rt_malloc
#define xnfree    rt_free

// in kernel printing (taken from fusion)

#define XNARCH_PROMPT "RTDM: "

#define xnprintf(fmt, args...)  printk(KERN_INFO XNARCH_PROMPT fmt, ##args)
#define xnlogerr(fmt, args...)  printk(KERN_ERR  XNARCH_PROMPT fmt, ##args)
#define xnlogwarn               xnlogerr

// user space access, taken from Linux

#define __xn_access_ok(task, type, addr, size) \
	(access_ok(type, addr, size))

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#define __xn_copy_from_user(task, dstP, srcP, n) \
	({ long err = __copy_from_user(dstP, srcP, n); err; })

#define __xn_copy_to_user(task, dstP, srcP, n) \
	({ long err = __copy_to_user(dstP, srcP, n); err; })
#else
#define __xn_copy_from_user(task, dstP, srcP, n) \
	({ long err = __copy_from_user_inatomic(dstP, srcP, n); err; })

#define __xn_copy_to_user(task, dstP, srcP, n) \
	({ long err = __copy_to_user_inatomic(dstP, srcP, n); err; })
#endif

#define __xn_strncpy_from_user(task, dstP, srcP, n) \
	({ long err = __strncpy_from_user(dstP, srcP, n); err; })

// interrupt setup/management

#define XN_ISR_NONE   	 0x1
#define XN_ISR_HANDLED	 0x2

#define XN_ISR_PROPAGATE 0x100
#define XN_ISR_NOENABLE  0x200
#define XN_ISR_BITMASK	 ~0xff

#define XN_ISR_SHARED	 0x1
#define XN_ISR_EDGE	 0x2

#define XN_ISR_ATTACHED	 0x10000

struct xnintr;

typedef int (*xnisr_t)(struct xnintr *intr);

typedef int (*xniack_t)(unsigned irq);

typedef long xnflags_t;

typedef struct xnintr {
    struct xnintr *next;
    xnisr_t isr;
    void *cookie;
    unsigned long hits;
    xnflags_t flags;
    unsigned irq;
    xniack_t iack;
    const char *name;
} xnintr_t;

int xnintr_shirq_attach(xnintr_t *intr, void *cookie);
int xnintr_shirq_detach(xnintr_t *intr);

static inline int xnintr_init (xnintr_t *intr, const char *name, unsigned irq, xnisr_t isr, xniack_t iack, xnflags_t flags)
{
	*intr = (xnintr_t) { NULL, isr, NULL, 0, flags, irq, iack, name };
	return 0;
}

static inline int xnintr_attach (xnintr_t *intr, void *cookie)
{
	intr->hits = 0;
	intr->cookie = cookie;
	return xnintr_shirq_attach(intr, cookie);
}

static inline int xnintr_detach (xnintr_t *intr)
{
	return xnintr_shirq_detach(intr);
}

static inline int xnintr_destroy (xnintr_t *intr)
{
	xnintr_detach(intr);
	return 0;
}

static int xnintr_enable (xnintr_t *intr)
{
	rt_enable_irq(intr->irq);
	return 0;
}

static int xnintr_disable (xnintr_t *intr)
{
	rt_disable_irq(intr->irq);
	return 0;
}

#ifdef CONFIG_SMP

typedef struct xnintr_shirq {
	xnintr_t *handlers;
	atomic_t active;
} xnintr_shirq_t;

static inline void xnintr_shirq_lock(xnintr_shirq_t *shirq)
{
    atomic_inc(&shirq->active);
}

static inline void xnintr_shirq_unlock(xnintr_shirq_t *shirq)
{
    atomic_dec(&shirq->active);
}

static inline void xnintr_shirq_spin(xnintr_shirq_t *shirq)
{
	while (atomic_read(&shirq->active)) {
		cpu_relax();
	}
}

#else /* !CONFIG_SMP */

typedef struct xnintr_shirq {
	xnintr_t *handlers;
} xnintr_shirq_t;

static inline void xnintr_shirq_lock(xnintr_shirq_t *shirq)   { }

static inline void xnintr_shirq_unlock(xnintr_shirq_t *shirq) { }

static inline void xnintr_shirq_spin(xnintr_shirq_t *shirq)   { }

#endif /* CONFIG_SMP */

#define testbits(flags, mask)  ((flags) & (mask))
#define setbits(flags, mask)   do { (flags) |= (mask);  } while(0)
#define clrbits(flags, mask)   do { (flags) &= ~(mask); } while(0)

#endif /* !_RTAI_XNSTUFF_H */
