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

#ifndef _RTAI_ALARM_H
#define _RTAI_ALARM_H

#include <nucleus/timer.h>
#include <nucleus/synch.h>
#include <rtai/types.h>

typedef struct rt_alarm_info {

    RTIME expiration;		/* !< Expiration date. */

    unsigned long nexpiries;	/* !< Number of expiries. */

    char name[XNOBJECT_NAME_LEN]; /* !< Symbolic name. */

} RT_ALARM_INFO;

typedef struct rt_alarm_placeholder {
    rt_handle_t opaque;
} RT_ALARM_PLACEHOLDER;

#if defined(__KERNEL__) || defined(__RTAI_SIM__)

#define RTAI_ALARM_MAGIC 0x55550909

typedef struct rt_alarm {

    unsigned magic;   /* !< Magic code - must be first */

    xntimer_t timer_base; /* !< Base timer object. */

    rt_handle_t handle;	/* !< Handle in registry -- zero if unregistered. */

    rt_alarm_t handler;		/* !< Alarm handler. */
    
    void *cookie;		/* !< Opaque cookie. */

    unsigned long nexpiries;	/* !< Number of expiries. */

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)

    int source;			/* !< Creator's space. */

    xnsynch_t synch_base;	/* !< Synch. base for user-space tasks. */

#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

    char name[XNOBJECT_NAME_LEN]; /* !< Symbolic name. */

} RT_ALARM;

#ifdef __cplusplus
extern "C" {
#endif

int __alarm_pkg_init(void);

void __alarm_pkg_cleanup(void);

int rt_alarm_create(RT_ALARM *alarm,
		    const char *name,
		    rt_alarm_t handler,
		    void *cookie);

#ifdef __cplusplus
}
#endif

#else /* !(__KERNEL__ || __RTAI_SIM__) */

typedef RT_ALARM_PLACEHOLDER RT_ALARM;

int rt_alarm_create(RT_ALARM *alarm,
		    const char *name);

int rt_alarm_wait(RT_ALARM *alarm);

/* No binding for alarms. */

#endif /* __KERNEL__ || __RTAI_SIM__ */

#ifdef __cplusplus
extern "C" {
#endif

/* Public interface. */

int rt_alarm_delete(RT_ALARM *alarm);

int rt_alarm_start(RT_ALARM *alarm,
		   RTIME value,
		   RTIME interval);

int rt_alarm_stop(RT_ALARM *alarm);

int rt_alarm_inquire(RT_ALARM *alarm,
		     RT_ALARM_INFO *info);

#ifdef __cplusplus
}
#endif

#endif /* !_RTAI_ALARM_H */
