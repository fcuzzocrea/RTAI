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

typedef struct rt_intr_info {

    unsigned irq;	/* !< Interrupt request number. */

    int nwaiters;	/* !< Number of pending tasks. */

} RT_INTR_INFO;

typedef struct rt_intr_placeholder {
    rt_handle_t opaque;
} RT_INTR_PLACEHOLDER;

#if defined(__KERNEL__) || defined(__RTAI_SIM__)

#define RTAI_INTR_MAGIC 0x55550a0a

typedef struct rt_intr {

    unsigned magic;   /* !< Magic code - must be first */

    xnholder_t link;	/* !< Link in global interrupt queue. */

#define link2intr(laddr) \
((RT_INTR *)(((char *)laddr) - (int)(&((RT_INTR *)0)->link)))

    xnintr_t intr_base;   /* !< Base interrupt object. */

#define intr2rtintr(iaddr) \
((iaddr) ? ((RT_INTR *)(((char *)(iaddr)) - (int)(&((RT_INTR *)0)->intr_base))) : NULL)

    xnsynch_t synch_base; /* !< Base synchronization object. */

    rt_handle_t handle;	/* !< Handle in registry -- zero if unregistered. */

    int pending;	/* !< Pending hits to wait for. */

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    int source;		/* !< Creator's space. */
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

} RT_INTR;

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

#endif /* __KERNEL__ || __RTAI_SIM__ */

#ifdef __cplusplus
extern "C" {
#endif

/* Public interface. */

int rt_intr_create(RT_INTR *intr,
		   unsigned irq);

int rt_intr_delete(RT_INTR *intr);

int rt_intr_wait(RT_INTR *intr,
		 RTIME timeout);

int rt_intr_inquire(RT_INTR *intr,
		    RT_INTR_INFO *info);

#ifdef __cplusplus
}
#endif

#endif /* !_RTAI_INTR_H */
