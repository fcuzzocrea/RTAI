/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _RTAI_NUCLEUS_TYPES_H
#define _RTAI_NUCLEUS_TYPES_H

#include <linux/config.h>
#include <linux/errno.h>

#ifdef CONFIG_PREEMPT_RT
#define linux_semaphore compat_semaphore
#else /* CONFIG_PREEMPT_RT */
#define linux_semaphore semaphore
#endif /* !CONFIG_PREEMPT_RT */

#include <nucleus/asm/system.h>

typedef unsigned long xnsigmask_t;

typedef unsigned long long xnticks_t;

typedef long long xnsticks_t;

typedef unsigned long long xntime_t; /* ns */

typedef long long xnstime_t;

struct xnintr;

typedef int (*xnisr_t)(struct xnintr *intr);

#define XN_INFINITE   (0)
#define XN_NONBLOCK   ((xnticks_t)-1)

#define XN_APERIODIC_TICK  0
#define XN_NO_TICK         ((xnticks_t)-1)

#define testbits(flags,mask) ((flags) & (mask))
#define setbits(flags,mask)  xnarch_atomic_set_mask(&(flags),mask)
#define clrbits(flags,mask)  xnarch_atomic_clear_mask(&(flags),mask)
#define __testbits(flags,mask) testbits(flags,mask)
#define __setbits(flags,mask)  do { (flags) |= (mask); } while(0)
#define __clrbits(flags,mask)  do { (flags) &= ~(mask); } while(0)

typedef atomic_flags_t xnflags_t;

#ifndef NULL
#define NULL 0
#endif

#define XNOBJECT_NAME_LEN 32

static inline void xnobject_copy_name (char *dst,
				       const char *src)
{
    if (src)
	{
	const char *rp = src;
	char *wp = dst;
	do
	    *wp++ = *rp;
	while (*rp && rp++ - src < XNOBJECT_NAME_LEN);
	}
    else
	*dst = '\0';
}

#define minval(a,b) ((a) < (b) ? (a) : (b))
#define maxval(a,b) ((a) > (b) ? (a) : (b))

#define XN_NBBY 8
#ifndef __KERNEL__
#ifndef BITS_PER_LONG
#define BITS_PER_LONG (sizeof(long) * XN_NBBY)
#endif /* BITS_PER_LONG */
#endif /* !__KERNEL__ */

#ifdef __cplusplus
extern "C" {
#endif

const char *xnpod_fatal_helper(const char *format, ...);

int __fusion_user_init(void);

void __fusion_user_exit(void);

#ifdef __cplusplus
}
#endif

#define xnpod_fatal(format,args...) \
do { \
   const char *panic = xnpod_fatal_helper(format,##args); \
   xnarch_halt(panic); \
} while (0)

#define root_thread_init __fusion_user_init
#define root_thread_exit __fusion_user_exit

#endif /* !_RTAI_NUCLEUS_TYPES_H */
