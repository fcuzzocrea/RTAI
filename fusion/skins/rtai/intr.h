/**
 * @file
 * This file is part of the RTAI project.
 *
 * @note Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org> 
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

#ifndef _RTAI_INTR_H
#define _RTAI_INTR_H

#include <nucleus/synch.h>
#include <nucleus/intr.h>
#include <rtai/types.h>

/* Creation flag. */
#define I_AUTOENA  XN_ISR_ENABLE /* Auto-enable interrupt channel
				    after each IRQ. */
typedef struct rt_intr_info {

    unsigned irq;	/* !< Interrupt request number. */

    unsigned long hits;	/* !< Number of receipts (since attachment). */

    char name[XNOBJECT_NAME_LEN]; /* !< Symbolic name. */

} RT_INTR_INFO;

typedef struct rt_intr_placeholder {
    rt_handle_t opaque;
} RT_INTR_PLACEHOLDER;

#if defined(__KERNEL__) || defined(__RTAI_SIM__)

#define RTAI_INTR_MAGIC 0x55550a0a

#define RT_INTR_HANDLED XN_ISR_HANDLED
#define RT_INTR_CHAINED XN_ISR_CHAINED
#define RT_INTR_ENABLE  XN_ISR_ENABLE

#define I_DESC(xintr)  ((RT_INTR *)(xintr)->cookie)

typedef struct rt_intr {

    unsigned magic;   /* !< Magic code - must be first */

    xnholder_t link;	/* !< Link in global interrupt queue. */

#define link2intr(laddr) \
((RT_INTR *)(((char *)laddr) - (int)(&((RT_INTR *)0)->link)))

    xnintr_t intr_base;   /* !< Base interrupt object. */

    int mode;		/* !< Interrupt control mode. */

    void *private_data;	/* !< Private user-defined data. */

    rt_handle_t handle;	/* !< Handle in registry -- zero if unregistered. */

    char name[XNOBJECT_NAME_LEN]; /* !< Symbolic name. */

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)

    int pending;	/* !< Pending hits to wait for. */

    xnsynch_t synch_base; /* !< Base synchronization object. */

    int source;		/* !< Creator's space. */

#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

} RT_INTR;

#define rt_intr_save(x)    splhigh(x)
#define rt_intr_restore(x) splexit(x)
#define rt_intr_unmask()   splnone()
#define rt_intr_flags(x)   splget(x)

#ifdef __cplusplus
extern "C" {
#endif

int __intr_pkg_init(void);

void __intr_pkg_cleanup(void);

#ifdef __cplusplus
}
#endif

#else /* !(__KERNEL__ || __RTAI_SIM__) */

typedef RT_INTR_PLACEHOLDER RT_INTR;

int rt_intr_bind(RT_INTR *intr,
		 unsigned irq);

static inline int rt_intr_unbind (RT_INTR *intr)

{
    intr->opaque = RT_HANDLE_INVALID;
    return 0;
}

int rt_intr_wait(RT_INTR *intr,
		 RTIME timeout);

#endif /* __KERNEL__ || __RTAI_SIM__ */

#ifdef __cplusplus
extern "C" {
#endif

/* Public interface. */

int rt_intr_create(RT_INTR *intr,
		   unsigned irq,
		   rt_isr_t isr,
		   int mode);

int rt_intr_delete(RT_INTR *intr);

int rt_intr_enable(RT_INTR *intr);

int rt_intr_disable(RT_INTR *intr);

int rt_intr_inquire(RT_INTR *intr,
		    RT_INTR_INFO *info);

#ifdef __cplusplus
}
#endif

#endif /* !_RTAI_INTR_H */
