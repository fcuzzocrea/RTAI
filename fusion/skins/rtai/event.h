/**
 * @file
 * This file is part of the RTAI project.
 *
 * @note Copyright (C) 2004 Philippe Gerum <rpm@xenomai.org> 
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
 *
 * As a special exception, the RTAI project gives permission
 * for additional uses of the text contained in its release of
 * Xenomai.
 *
 * The exception is that, if you link the Xenomai libraries with other
 * files to produce an executable, this does not by itself cause the
 * resulting executable to be covered by the GNU General Public License.
 * Your use of that executable is in no way restricted on account of
 * linking the Xenomai libraries code into it.
 *
 * This exception does not however invalidate any other reasons why
 * the executable file might be covered by the GNU General Public
 * License.
 *
 * This exception applies only to the code released by the
 * RTAI project under the name Xenomai.  If you copy code from other
 * RTAI project releases into a copy of Xenomai, as the General Public
 * License permits, the exception does not apply to the code that you
 * add in this way.  To avoid misleading anyone as to the status of
 * such modified files, you must delete this exception notice from
 * them.
 *
 * If you write modifications of your own for Xenomai, it is your
 * choice whether to permit this exception to apply to your
 * modifications. If you do not wish that, delete this exception
 * notice.
 */

#ifndef _RTAI_EVENT_H
#define _RTAI_EVENT_H

#include <nucleus/synch.h>
#include <rtai/types.h>

/* Creation flags. */
#define EV_PRIO  XNSYNCH_PRIO	/* Pend by task priority order. */
#define EV_FIFO  XNSYNCH_FIFO	/* Pend by FIFO order. */

/* Operation flags. */
#define EV_ANY  0x1	/* Disjunctive wait. */
#define EV_ALL  0x0	/* Conjunctive wait. */

typedef struct rt_event_info {

    unsigned long value; /* !< Current event group value. */

    int nwaiters;	/* !< Number of pending tasks. */

    char name[XNOBJECT_NAME_LEN]; /* !< Symbolic name. */

} RT_EVENT_INFO;

typedef struct rt_event_placeholder {
    rt_handle_t opaque;
} RT_EVENT_PLACEHOLDER;

#if defined(__KERNEL__) || defined(__RTAI_SIM__)

#define RTAI_EVENT_MAGIC 0x55550404

typedef struct rt_event {

    unsigned magic;   /* !< Magic code - must be first */

    xnsynch_t synch_base; /* !< Base synchronization object. */

    unsigned long value; /* !< Event group value. */

    rt_handle_t handle;	/* !< Handle in registry -- zero if unregistered. */

    char name[XNOBJECT_NAME_LEN]; /* !< Symbolic name. */

} RT_EVENT;

#ifdef __cplusplus
extern "C" {
#endif

int __event_pkg_init(void);

void __event_pkg_cleanup(void);

#ifdef __cplusplus
}
#endif

#else /* !(__KERNEL__ || __RTAI_SIM__) */

typedef RT_EVENT_PLACEHOLDER RT_EVENT;

int rt_event_bind(RT_EVENT *event,
		  const char *name);

static inline int rt_event_unbind (RT_EVENT *event) {

    event->opaque = RT_HANDLE_INVALID;
    return 0;
}

#endif /* __KERNEL__ || __RTAI_SIM__ */

#ifdef __cplusplus
extern "C" {
#endif

/* Public interface. */

int rt_event_create(RT_EVENT *event,
		    const char *name,
		    unsigned long ivalue,
		    int mode);

int rt_event_delete(RT_EVENT *event);

int rt_event_post(RT_EVENT *event,
		  unsigned long mask);

int rt_event_pend(RT_EVENT *event,
		  unsigned long mask,
		  unsigned long *mask_r,
		  int mode,
		  RTIME timeout);

int rt_event_inquire(RT_EVENT *event,
		     RT_EVENT_INFO *info);

#ifdef __cplusplus
}
#endif

#endif /* !_RTAI_EVENT_H */
