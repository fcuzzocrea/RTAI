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

#ifndef _RTAI_REGISTRY_H
#define _RTAI_REGISTRY_H

#include <rtai/types.h>

#define RT_REGISTRY_SELF  RT_HANDLE_INVALID

#ifdef __KERNEL__

#include <nucleus/synch.h>
#include <nucleus/thread.h>

#define RT_REGISTRY_RECHECK   XNTHREAD_SPARE0

typedef struct rt_object {

    xnholder_t link;
#define link2rtobj(laddr) \
((RT_OBJECT *)(((char *)laddr) - (int)(&((RT_OBJECT *)0)->link)))

    void *objaddr;

    const char *key;	/* !< Hash key. */

    xnsynch_t safesynch; /* !< Safe synchronization object. */

    u_long safelock;	 /* !< Safe lock count. */

    u_long cstamp;	/* !< Creation stamp. */

} RT_OBJECT;

typedef struct rt_hash {

    RT_OBJECT *object;

    struct rt_hash *next;	/* !< Next in h-table */

} RT_HASH;

extern RT_OBJECT __rtai_obj_slots[];

#ifdef __cplusplus
extern "C" {
#endif

int __registry_pkg_init(void);

void __registry_pkg_cleanup(void);

/* Public interface. */

int rt_registry_enter(const char *key,
		      void *objaddr,
		      rt_handle_t *phandle);

int rt_registry_bind(const char *key,
		     RTIME timeout,
		     rt_handle_t *phandle);

int rt_registry_remove(rt_handle_t);

void *rt_registry_get(rt_handle_t handle);

void *rt_registry_fetch(rt_handle_t handle);

u_long rt_registry_put(rt_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* __KERNEL__ */

#endif /* !_RTAI_REGISTRY_H */
